
#include "ns3/log.h"
#include "tor-bktap.h"

using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorBktapApp");
NS_OBJECT_ENSURE_REGISTERED (TorBktapApp);
NS_OBJECT_ENSURE_REGISTERED (SeqQueue);


UdpChannel::UdpChannel ()
{
  NS_LOG_FUNCTION (this);
  this->m_socket = 0;
}

UdpChannel::UdpChannel (Address remote, int conntype)
{
  m_remote = remote;
  m_conntype = conntype;
  this->m_socket = 0;
}

void
UdpChannel::SetSocket (Ptr<Socket> socket)
{
  this->m_socket = socket;
}

uint8_t
UdpChannel::GetType ()
{
  return m_conntype;
}

bool
UdpChannel::SpeaksCells ()
{
  return m_conntype == RELAYEDGE;
}

void
UdpChannel::Flush ()
{
  while (m_flushQueue.size () > 0)
    {
      if (SpeaksCells () && m_devQlimit <= m_devQ->GetNPackets ())
        {
          return;
        }
      Ptr<Packet> data = Create<Packet> ();
      while (m_flushQueue.size () > 0 && data->GetSize () + m_flushQueue.front ()->GetSize () <= 1400)
        {
          data->AddAtEnd (m_flushQueue.front ());
          m_flushQueue.pop ();
        }
      m_socket->SendTo (data,0,m_remote);
    }
}


void
UdpChannel::ScheduleFlush ()
{
  if (m_flushQueue.size () > 1)
    {
      m_flushEvent.Cancel ();
      Flush ();
    }
  else
    {
      m_flushEvent = Simulator::Schedule (MilliSeconds (1), &UdpChannel::Flush, this);
    }
}

BktapCircuit::BktapCircuit (uint16_t id) : BaseCircuit (id)
{
  inboundQueue = CreateObject<SeqQueue> ();
  outboundQueue = CreateObject<SeqQueue> ();
}

CellDirection
BktapCircuit::GetDirection (Ptr<UdpChannel> ch)
{
  if (this->outbound == ch)
    {
      return OUTBOUND;
    }
  else
    {
      return INBOUND;
    }
}

Ptr<UdpChannel>
BktapCircuit::GetChannel (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->outbound;
    }
  else
    {
      return this->inbound;
    }
}

Ptr<SeqQueue>
BktapCircuit::GetQueue (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->outboundQueue;
    }
  else
    {
      return this->inboundQueue;
    }
}


TorBktapApp::TorBktapApp ()
{
  NS_LOG_FUNCTION (this);
}

TorBktapApp::~TorBktapApp ()
{
  NS_LOG_FUNCTION (this);
}

TypeId
TorBktapApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorBktapApp")
    .SetParent<TorBaseApp> ()
    .AddConstructor<TorBktapApp> ()
    .AddTraceSource ("NewServerSocket",
                     "Trace indicating that a new pseudo server socket has been installed.",
                     MakeTraceSourceAccessor (&TorBktapApp::m_triggerNewPseudoServerSocket),
                     "ns3::TorApp::TorNewPseudoServerSocketCallback");
  return tid;
}

void
TorBktapApp::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                         Ptr<PseudoClientSocket> clientSocket)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype);

  // ensure unique circ_id
  NS_ASSERT (circuits[id] == 0);

  Ptr<BktapCircuit> circ = Create<BktapCircuit> (id);
  circuits[id] = circ;
  baseCircuits[id] = circ;

  circ->inbound = AddChannel (InetSocketAddress (p_ip,9001),p_conntype);
  circ->inbound->circuits.push_back (circ);
  circ->inbound->SetSocket (clientSocket);

  circ->outbound = AddChannel (InetSocketAddress (n_ip,9001),n_conntype);
  circ->outbound->circuits.push_back (circ);

}

Ptr<UdpChannel>
TorBktapApp::AddChannel (Address remote, int conntype)
{
  // find existing or create new channel-object
  Ptr<UdpChannel> ch = channels[remote];
  if (!ch)
    {
      ch = Create<UdpChannel> (remote, conntype);
      channels[remote] = ch;
    }
  return ch;
}

void
TorBktapApp::StartApplication (void)
{
  //tor proposal #183: smooth bursts & get queued data out earlier
  m_refilltime = MilliSeconds (10);
  TorBaseApp::StartApplication ();
  m_readbucket.SetRefilledCallback (MakeCallback (&TorBktapApp::RefillReadCallback, this));
  m_writebucket.SetRefilledCallback (MakeCallback (&TorBktapApp::RefillWriteCallback, this));

  circit = circuits.begin ();

  m_devQ = GetNode ()->GetDevice (0)->GetObject<PointToPointNetDevice> ()->GetQueue ();
  UintegerValue limit;
  m_devQ->GetAttribute ("MaxPackets", limit);
  m_devQlimit = limit.Get ();

  if (m_socket == 0)
    {
      m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
      m_socket->Bind (m_local);
    }

  m_socket->SetRecvCallback (MakeCallback (&TorBktapApp::ReadCallback, this));
  m_socket->SetDataSentCallback (MakeCallback (&TorBktapApp::SocketWriteCallback, this));

  Ipv4Mask ipmask = Ipv4Mask ("255.0.0.0");

  // iterate over all neighboring channels
  map<Address,Ptr<UdpChannel> >::iterator it;
  for ( it = channels.begin (); it != channels.end (); it++ )
    {
      Ptr<UdpChannel> ch = it->second;
      NS_ASSERT (ch);

      if (ch->SpeaksCells ())
        {
          ch->SetSocket (m_socket);
          ch->m_devQ = m_devQ;
          ch->m_devQlimit = m_devQlimit;
        }

      // PseudoSockets only
      if (ipmask.IsMatch (InetSocketAddress::ConvertFrom (ch->m_remote).GetIpv4 (), Ipv4Address ("127.0.0.1")) )
        {
          if (ch->GetType () == SERVEREDGE)
            {
              Ptr<Socket> socket = CreateObject<PseudoServerSocket> ();
              socket->SetRecvCallback (MakeCallback (&TorBktapApp::ReadCallback, this));
              ch->SetSocket (socket);
              m_triggerNewPseudoServerSocket(this, DynamicCast<PseudoServerSocket>(socket));
            }

          if (ch->GetType () == PROXYEDGE)
            {
              if (!ch->m_socket)
                {
                  ch->m_socket = CreateObject<PseudoClientSocket> ();
                }
              ch->m_socket->SetRecvCallback (MakeCallback (&TorBktapApp::ReadCallback, this));
            }
        }
    }
}

void
TorBktapApp::StopApplication (void)
{
  m_socket->Close ();
  m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
  m_socket->SetDataSentCallback (MakeNullCallback<void, Ptr<Socket>, uint32_t > ());
}

void
TorBktapApp::RefillReadCallback (int64_t prev_read_bucket)
{
  vector<Ptr<Socket> > v;
  map<Address,Ptr<UdpChannel> >::iterator it;
  for ( it = channels.begin (); it != channels.end (); it++ )
    {
      Ptr<Socket> socket = it->second->m_socket;
      if (std::find (v.begin (), v.end (),socket) == v.end ())
        {
          Simulator::Schedule (Seconds (0), &TorBktapApp::ReadCallback, this, it->second->m_socket);
          v.push_back (socket);
        }
    }
}

void
TorBktapApp::RefillWriteCallback (int64_t prev_read_bucket)
{
  if (prev_read_bucket <= 0 && writeevent.IsExpired ())
    {
      writeevent = Simulator::ScheduleNow (&TorBktapApp::WriteCallback, this);
    }
}

void
TorBktapApp::ReadCallback (Ptr<Socket> socket)
{
  uint32_t read_bytes = 0;
  if (socket->GetSocketType () == NS3_SOCK_STREAM)
    {
      read_bytes = ReadFromEdge (socket);
    }
  else
    {
      read_bytes = ReadFromRelay (socket);
    }

  if (writeevent.IsExpired ())
    {
      writeevent = Simulator::Schedule (NanoSeconds (read_bytes * 2),&TorBktapApp::WriteCallback, this);
    }
}

uint32_t
TorBktapApp::ReadFromRelay (Ptr<Socket> socket)
{
  uint32_t read_bytes = 0;
  while (socket->GetRxAvailable () > 0)
    {
      Ptr<Packet> data;
      Address from;
      if (data = socket->RecvFrom (from))
        {
          data->RemoveAllPacketTags (); //Fix for ns3 PacketTag Bug
          read_bytes += data->GetSize ();
          Ptr<UdpChannel> ch = channels[from];
          NS_ASSERT (ch);
          while (data->GetSize () > 0)
            {
              BaseCellHeader header;
              data->PeekHeader (header);
              Ptr<BktapCircuit> circ = circuits[header.circId];
              NS_ASSERT (circ);
              CellDirection direction = circ->GetDirection (ch);
              CellDirection oppdir = circ->GetOppositeDirection (direction);
              if (header.cellType == FDBK)
                {
                  FdbkCellHeader h;
                  data->RemoveHeader (h);
                  circ->IncrementStats (oppdir,h.GetSerializedSize (),0);
                  if (h.flags & ACK)
                    {
                      ReceivedAck (circ,direction,h);
                    }
                  if (h.flags & FWD)
                    {
                      ReceivedFwd (circ,direction,h);
                    }
                }
              else
                {
                  Ptr<Packet> cell = data->CreateFragment (0,CELL_PAYLOAD_SIZE + UDP_CELL_HEADER_SIZE);
                  data->RemoveAtStart (CELL_PAYLOAD_SIZE + UDP_CELL_HEADER_SIZE);
                  circ->IncrementStats (oppdir,cell->GetSize (),0);
                  ReceivedRelayCell (circ,oppdir,cell);
                }
            }
        }
    }
  return read_bytes;
}

void
TorBktapApp::ReceivedRelayCell (Ptr<BktapCircuit> circ, CellDirection direction, Ptr<Packet> cell)
{
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  UdpCellHeader header;
  cell->PeekHeader (header);
  bool newseq = queue->Add (cell, header.seq);
  if (newseq) {
    m_readbucket.Decrement(cell->GetSize());
  }
  CellDirection oppdir = circ->GetOppositeDirection (direction);
  SendFeedbackCell (circ, oppdir, ACK, queue->tailSeq + 1);
}


void
TorBktapApp::ReceivedAck (Ptr<BktapCircuit> circ, CellDirection direction, FdbkCellHeader header)
{
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  if (header.ack == queue->headSeq)
    {
      // DupACK. Do fast retransmit.
      ++queue->dupackcnt;
      if (m_writebucket.GetSize () >= CELL_PAYLOAD_SIZE && queue->dupackcnt > 2)
        {
          FlushPendingCell (circ,direction,true);
          queue->dupackcnt = 0;
        }
    }
  else if (header.ack > queue->headSeq)
    {
      //NewAck
      queue->dupackcnt = 0;
      queue->DiscardUpTo (header.ack);
      Time rtt = queue->actRtt.EstimateRtt (header.ack);
      ScheduleRto (circ,direction,true);
    }
  else
    {
      cerr << GetNodeName () << " Ignore Ack" << endl;
    }
}


void
TorBktapApp::CongestionAvoidance (Ptr<SeqQueue> queue, Time baseRtt)
{
  //Do the Vegas-thing every RTT
  if (queue->virtRtt.cntRtt > 2)
    {
      Time rtt = queue->virtRtt.currentRtt;
      double diff = queue->cwnd * (rtt.GetSeconds () - baseRtt.GetSeconds ()) / baseRtt.GetSeconds ();
      // uint32_t target = queue->cwnd * baseRtt.GetMilliSeconds() / rtt.GetMilliSeconds();

      if (diff < VEGASALPHA)
        {
          ++queue->cwnd;
        }

      if (diff > VEGASBETA)
        {
          --queue->cwnd;
        }

      if ((uint32_t) queue->cwnd < 1)
        {
          queue->cwnd = 1;
        }

      double maxexp = m_burst.GetBitRate () / 8 / CELL_PAYLOAD_SIZE * baseRtt.GetSeconds ();
      queue->cwnd = min ((uint32_t) queue->cwnd, (uint32_t) maxexp);

      queue->virtRtt.ResetCurrRtt ();

    }
  else
    {
      // Vegas falls back to Reno CA, i.e. increase per RTT
      // However, This messes up with our backlog and makes the approach too aggressive.
    }
}

void
TorBktapApp::ReceivedFwd (Ptr<BktapCircuit> circ, CellDirection direction, FdbkCellHeader header)
{
  //Received flow control feeback (FWD)
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  Ptr<UdpChannel> ch = circ->GetChannel (direction);
  Time rtt = queue->virtRtt.EstimateRtt (header.fwd);
  ch->rttEstimator.AddSample (rtt);

  if (queue->virtHeadSeq <= header.fwd)
    {
      queue->virtHeadSeq = header.fwd;
    }

  if (header.fwd > queue->begRttSeq)
    {
      queue->begRttSeq = queue->nextTxSeq;
      CongestionAvoidance (queue,ch->rttEstimator.baseRtt);
      queue->ssthresh = min ((uint32_t) queue->cwnd, (uint32_t) queue->ssthresh);
      queue->ssthresh = max ((uint32_t) queue->ssthresh, (uint32_t) queue->cwnd / 2);
    }
  else if (queue->cwnd <= queue->ssthresh)
    {
      //TODO test different slow start schemes
    }

  CellDirection oppdir = circ->GetOppositeDirection (direction);
  ch = circ->GetChannel (oppdir);
  Simulator::Schedule (Seconds (0), &TorBktapApp::ReadCallback, this, ch->m_socket);

  if (writeevent.IsExpired ())
    {
      writeevent = Simulator::Schedule (Seconds (0), &TorBktapApp::WriteCallback, this);
    }
}

uint32_t
TorBktapApp::ReadFromEdge (Ptr<Socket> socket)
{
  Ptr<UdpChannel> ch = LookupChannel (socket);
  NS_ASSERT (ch);
  Ptr<BktapCircuit> circ = ch->circuits.front ();
  NS_ASSERT (circ);
  CellDirection direction = circ->GetDirection (ch);
  CellDirection oppdir = circ->GetOppositeDirection (direction);
  Ptr<SeqQueue> queue = circ->GetQueue (oppdir);

  uint32_t max_read = ((uint32_t) queue->cwnd - queue->VirtSize () <= 0) ? 0 : (uint32_t) queue->cwnd - queue->VirtSize ();
  max_read *= CELL_PAYLOAD_SIZE;

  uint32_t read_bytes = 0;

  while (max_read - read_bytes >= CELL_PAYLOAD_SIZE && socket->GetRxAvailable () >= CELL_PAYLOAD_SIZE)
    {
      Ptr<Packet> data = socket->Recv (CELL_PAYLOAD_SIZE, 0);
      data->RemoveAllPacketTags (); //Fix for ns3 PacketTag Bug
      read_bytes += data->GetSize ();
      m_readbucket.Decrement (data->GetSize ());
      circ->IncrementStats (oppdir,data->GetSize (),0);
      PackageRelayCell (circ, oppdir, data);
    }

  return read_bytes;
}

void
TorBktapApp::PackageRelayCell (Ptr<BktapCircuit> circ, CellDirection direction, Ptr<Packet> cell)
{
  UdpCellHeader header;
  header.circId = circ->GetId ();
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  NS_ASSERT (queue);
  header.seq = queue->tailSeq + 1;
  cell->AddHeader (header);
  queue->virtRtt.SentSeq (header.seq);
  queue->actRtt.SentSeq (header.seq);
  queue->Add (cell, header.seq);
}

void
TorBktapApp::SocketWriteCallback (Ptr<Socket> s, uint32_t i)
{
  if (writeevent.IsExpired ())
    {
      writeevent = Simulator::Schedule (Seconds (0), &TorBktapApp::WriteCallback, this);
    }
}

void
TorBktapApp::WriteCallback ()
{
  uint32_t bytes_written = 0;

  if (m_writebucket.GetSize () >= CELL_PAYLOAD_SIZE)
    {
      Ptr<BktapCircuit> start = circit->second;
      Ptr<BktapCircuit> circ;

      while (bytes_written == 0 && start != circ)
        {
          circ = GetNextCircuit ();
          bytes_written += FlushPendingCell (circ,INBOUND);
          bytes_written += FlushPendingCell (circ,OUTBOUND);
        }
    }

  if (bytes_written > 0)
    {
      // try flushing more ...
      if (writeevent.IsExpired ())
        {
          writeevent = Simulator::ScheduleNow (&TorBktapApp::WriteCallback, this);
        }
    }
}



uint32_t
TorBktapApp::FlushPendingCell (Ptr<BktapCircuit> circ, CellDirection direction, bool retx)
{
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  CellDirection oppdir = circ->GetOppositeDirection (direction);
  Ptr<UdpChannel> ch = circ->GetChannel (direction);
  Ptr<Packet> cell;

  if (queue->Window () <= 0 && !retx)
    {
      return 0;
    }

  if (retx)
    {
      cell = queue->GetCell (queue->headSeq);
      queue->dupackcnt = 0;
    }
  else
    {
      cell = queue->GetNextCell ();
    }

  if (cell)
    {
      UdpCellHeader header;
      cell->PeekHeader (header);
      if (!ch->SpeaksCells ())
        {
          cell->RemoveHeader (header);
        }

      if (circ->GetChannel (oppdir)->SpeaksCells ())
        {
          queue->virtRtt.SentSeq (header.seq);
          queue->actRtt.SentSeq (header.seq);
        }

      ch->m_flushQueue.push (cell);
      int bytes_written = cell->GetSize ();
      ch->ScheduleFlush ();

      if (ch->SpeaksCells ())
        {
          ScheduleRto (circ,direction);
        }
      else
        {
          queue->DiscardUpTo (header.seq + 1);
          ++queue->virtHeadSeq;
        }

      if (queue->highestTxSeq == header.seq)
        {
          circ->IncrementStats (direction,0,bytes_written);
          SendFeedbackCell (circ, oppdir, FWD, queue->highestTxSeq + 1);
        }

      if (!queue->WasRetransmit()) {
        m_writebucket.Decrement(bytes_written);
      }

      return bytes_written;
    }
  return 0;
}

void
TorBktapApp::SendFeedbackCell (Ptr<BktapCircuit> circ, CellDirection direction, uint8_t flag, uint32_t ack)
{
  Ptr<UdpChannel> ch = circ->GetChannel (direction);
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  NS_ASSERT (ch);
  if (ch->SpeaksCells ())
    {
      if (flag & ACK)
        {
          queue->ackq.push (ack);
        }
      if (flag & FWD)
        {
          queue->fwdq.push (ack);
        }
      if (queue->ackq.size () > 0 && queue->fwdq.size () > 0)
        {
          queue->delFeedbackEvent.Cancel ();
          PushFeedbackCell (circ, direction);
        }
      else
        {
          queue->delFeedbackEvent = Simulator::Schedule (MilliSeconds (1), &TorBktapApp::PushFeedbackCell, this, circ, direction);
        }
    }
}

void
TorBktapApp::PushFeedbackCell (Ptr<BktapCircuit> circ, CellDirection direction)
{
  Ptr<UdpChannel> ch = circ->GetChannel (direction);
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  NS_ASSERT (ch);

  while (queue->ackq.size () > 0 || queue->fwdq.size () > 0)
    {
      Ptr<Packet> cell = Create<Packet> ();
      FdbkCellHeader header;
      header.circId = circ->GetId ();
      if (queue->ackq.size () > 0)
        {
          header.flags |= ACK;
          while (queue->ackq.size () > 0 && header.ack < queue->ackq.front ())
            {
              header.ack = queue->ackq.front ();
              queue->ackq.pop ();
            }
        }
      if (queue->fwdq.size () > 0)
        {
          header.flags |= FWD;
          header.fwd = queue->fwdq.front ();
          queue->fwdq.pop ();
        }
      cell->AddHeader (header);
      ch->m_flushQueue.push (cell);
      ch->ScheduleFlush ();
    }
}

void
TorBktapApp::ScheduleRto (Ptr<BktapCircuit> circ, CellDirection direction, bool force)
{
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  if (force)
    {
      queue->retxEvent.Cancel ();
    }
  if (queue->Inflight () <= 0)
    {
      return;
    }
  if (queue->retxEvent.IsExpired ())
    {
      queue->retxEvent = Simulator::Schedule (queue->actRtt.Rto (), &TorBktapApp::Rto, this, circ, direction);
    }
}

void
TorBktapApp::Rto (Ptr<BktapCircuit> circ, CellDirection direction)
{
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  queue->nextTxSeq = queue->headSeq;
  FlushPendingCell (circ,direction);
}

Ptr<BktapCircuit>
TorBktapApp::GetCircuit (uint16_t id)
{
  return circuits[id];
}

Ptr<BktapCircuit>
TorBktapApp::GetNextCircuit ()
{
  ++circit;
  if (circit == circuits.end ())
    {
      circit = circuits.begin ();
    }
  return circit->second;
}

Ptr<UdpChannel>
TorBktapApp::LookupChannel (Ptr<Socket> socket)
{
  map<Address,Ptr<UdpChannel> >::iterator it;
  for ( it = channels.begin (); it != channels.end (); it++ )
    {
      NS_ASSERT (it->second);
      if (it->second->m_socket == socket)
        {
          return it->second;
        }
    }
  return NULL;
}

void
TorBktapApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  map<uint16_t,Ptr<BktapCircuit> >::iterator i;
  for (i = circuits.begin (); i != circuits.end (); ++i)
    {
      i->second->inbound = 0;
      i->second->outbound = 0;
      i->second->inboundQueue = 0;
      i->second->outboundQueue = 0;

    }
  circuits.clear ();
  baseCircuits.clear ();
  channels.clear ();
  Application::DoDispose ();
}

} //namespace ns3
