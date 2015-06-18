
#include "ns3/log.h"
#include "tor-bktap.h"

using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorBktapApp");
NS_OBJECT_ENSURE_REGISTERED (TorBktapApp);



UdpChannel::UdpChannel ()
{
  NS_LOG_FUNCTION (this);
  this->m_rng_request = 0;
  this->m_rng_think = 0;
}

UdpChannel::UdpChannel (Address remote, int conntype)
{
  m_remote = remote;
  m_conntype = conntype;
  this->m_rng_request = 0;
  this->m_rng_think = 0;
}

void
UdpChannel::SetSocket (Ptr<Socket> socket)
{
  m_socket = socket;
}

bool
UdpChannel::IsEdge ()
{
  return m_conntype >= EDGE_CONN;
}

bool
UdpChannel::IsOr ()
{
  return m_conntype == OR_CONN;
}

bool
UdpChannel::IsServerEdge ()
{
  return m_conntype == SERVEREDGE;
}

bool
UdpChannel::IsProxyEdge ()
{
  return m_conntype == PROXYEDGE;
}

void
UdpChannel::SetTtfbCallback (void (*ttfb)(int, double, string), int id, string desc)
{
  m_ttfb_id = id;
  m_ttfb_desc = desc;
  m_ttfb_callback = ttfb;
}

void
UdpChannel::SetTtlbCallback (void (*ttlb)(int, double, string), int id, string desc)
{
  m_ttlb_id = id;
  m_ttlb_desc = desc;
  m_ttlb_callback = ttlb;
}

void
UdpChannel::RegisterCallbacks ()
{
  if (IsEdge ())
    {
      Ptr<PseudoClientSocket> csock = m_socket->GetObject<PseudoClientSocket> ();
      if (csock)
        {
          if (m_ttfb_callback)
            {
              csock->SetTtfbCallback (m_ttfb_callback, m_ttfb_id, m_ttfb_desc);
              csock->SetTtlbCallback (m_ttlb_callback, m_ttlb_id, m_ttlb_desc);
            }
        }
    }
}


BktapCircuit::BktapCircuit (uint32_t id) : BaseCircuit (id)
{
  inboundQueue = Create<SeqQueue> ();
  outboundQueue = Create<SeqQueue> ();
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
    .AddConstructor<TorBktapApp> ();
  return tid;
}

void
TorBktapApp::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype);

  // ensure unique circ_id
  NS_ASSERT (circuits[id] == 0);

  Ptr<BktapCircuit> circ = Create<BktapCircuit> (id);

  if (p_conntype == EDGE_CONN)
    {
      p_conntype = PROXYEDGE;
    }
  circ->inbound = AddChannel (InetSocketAddress (p_ip,9001),p_conntype);
  circ->inbound->circuits.push_back (circ);

  if (n_conntype == EDGE_CONN)
    {
      n_conntype = SERVEREDGE;
    }
  circ->outbound = AddChannel (InetSocketAddress (n_ip,9001),n_conntype);
  circ->outbound->circuits.push_back (circ);

  circuits[id] = circ;
}


void
TorBktapApp::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                         Ptr<RandomVariableStream> rng_request, Ptr<RandomVariableStream> rng_think)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype);
  NS_ASSERT (circuits[id] == 0);

  Ptr<BktapCircuit> circ = Create<BktapCircuit> (id);

  if (p_conntype == EDGE_CONN)
    {
      p_conntype = PROXYEDGE;
    }
  circ->inbound = AddChannel (InetSocketAddress (p_ip,9001),p_conntype);
  circ->inbound->circuits.push_back (circ);
  circ->inbound->SetRandomVariableStreams (rng_request, rng_think);

  if (n_conntype == EDGE_CONN)
    {
      n_conntype = SERVEREDGE;
    }
  circ->outbound = AddChannel (InetSocketAddress (n_ip,9001),n_conntype);
  circ->outbound->circuits.push_back (circ);

  circuits[id] = circ;
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

  Ipv4Mask ipmask = Ipv4Mask ("255.0.0.0");

  // iterate over all neighboring channels
  std::map<Address,Ptr<UdpChannel> >::iterator it;
  for ( it = channels.begin (); it != channels.end (); it++ )
    {
      Ptr<UdpChannel> ch = it->second;
      NS_ASSERT (ch);

      ch->SetSocket (m_socket);

      //TODO: works with PseudoSockets only
      if (ch->IsEdge () && ipmask.IsMatch (InetSocketAddress::ConvertFrom (ch->m_remote).GetIpv4 (), Ipv4Address ("127.0.0.1")) )
        {
          if (ch->IsServerEdge ())
            {
              Ptr<Socket> socket = CreateObject<PseudoServerSocket> ();
              socket->SetRecvCallback (MakeCallback (&TorBktapApp::ReadCallback, this));
              ch->SetSocket (socket);
            }

          if (ch->IsProxyEdge ())
            {
              Ptr<PseudoClientSocket> socket = CreateObject<PseudoClientSocket> ();
              socket->SetRecvCallback (MakeCallback (&TorBktapApp::ReadCallback, this));
              if (ch->GetRequestStream () && ch->GetThinkStream ())
                {
                  socket->SetRequestStream (ch->GetRequestStream ());
                  socket->SetThinkStream (ch->GetThinkStream ());
                }
              ch->SetSocket (socket);
              ch->RegisterCallbacks ();
              Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();
              Simulator::Schedule (Seconds (rng->GetValue (0.1,1.0)), &TorBktapApp::ReadCallback, this, socket);
            }
        }
    }
}

void
TorBktapApp::RefillReadCallback (int64_t prev_read_bucket)
{
  if (prev_read_bucket <= 0 && m_readbucket.GetSize () > 0)
    {
      std::map<Address,Ptr<UdpChannel> >::iterator it;
      for ( it = channels.begin (); it != channels.end (); it++ )
        {
          Simulator::Schedule (Seconds (0), &TorBktapApp::ReadCallback, this, it->second->m_socket);
        }
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

  //TODO: eliminate busy-reading for better run-time performance
  Time t = Time (Seconds (CELL_PAYLOAD_SIZE * 8 / static_cast<double> (m_burst.GetBitRate ())));
  if (readevent.IsExpired ())
    {
      readevent = Simulator::Schedule (t, &TorBktapApp::ReadCallback, this, socket);
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
      Ptr<Packet> cell;
      Address from;
      if (cell = socket->RecvFrom (from))
        {
          cell->RemoveAllPacketTags (); //Fix for ns3 PacketTag Bug
          m_readbucket.Decrement (cell->GetSize ());
          BaseCellHeader header;
          cell->PeekHeader (header);
          Ptr<BktapCircuit> circ = circuits[header.circId];
          NS_ASSERT (circ);
          Ptr<UdpChannel> ch = channels[from];
          NS_ASSERT (ch);
          CellDirection direction = circ->GetDirection (ch);
          CellDirection oppdir = circ->GetOppositeDirection (direction);
          circ->IncrementStats (oppdir,cell->GetSize (),0);
          read_bytes += cell->GetSize ();

          if (header.cellType == FDBK)
            {
              FdbkCellHeader h;
              cell->PeekHeader (h);
              if (h.flags & ACK)
                {
                  ReceivedAck (circ,direction,cell);
                }
              else if (h.flags & FWD)
                {
                  ReceivedFwd (circ,direction,cell);
                }
            }
          else
            {
              ReceivedRelayCell (circ,oppdir,cell);
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
  queue->Add (cell, header.seq);
  CellDirection oppdir = circ->GetOppositeDirection (direction);
  SendEmptyAck (circ, oppdir, ACK, queue->tailSeq + 1);
}


void
TorBktapApp::ReceivedAck (Ptr<BktapCircuit> circ, CellDirection direction, Ptr<Packet> cell)
{
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  FdbkCellHeader header;
  cell->PeekHeader (header);

  if (header.ack == queue->headSeq)
    {
      // DupACK. Do fast retransmit.
      ++queue->dupackcnt;
      if (m_writebucket.GetSize () >= CELL_PAYLOAD_SIZE && queue->dupackcnt > 2)
        {
          uint32_t bytes_written = FlushPendingCell (circ,direction,true);
          m_writebucket.Decrement (bytes_written);
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

      if (queue->cwnd < 1)
        {
          queue->cwnd = 1;
        }

      double maxexp = m_burst.GetBitRate () / 8 / CELL_PAYLOAD_SIZE * baseRtt.GetSeconds ();
      queue->cwnd = min (queue->cwnd, (uint32_t) maxexp);

      queue->virtRtt.ResetCurrRtt ();

    }
  else
    {
      // Vegas falls back to Reno CA, i.e. increase per RTT
      // However, This messes up with our backlog and makes the approach too aggressive.
    }
}

void
TorBktapApp::ReceivedFwd (Ptr<BktapCircuit> circ, CellDirection direction, Ptr<Packet> cell)
{
  //Received flow control feeback (FWD)
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  Ptr<UdpChannel> ch = circ->GetChannel (direction);
  FdbkCellHeader header;
  cell->PeekHeader (header);

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
      queue->ssthresh = min (queue->cwnd,queue->ssthresh);
      queue->ssthresh = max (queue->ssthresh,queue->cwnd / 2);
    }
  else if (queue->cwnd <= queue->ssthresh)
    {
      //TODO test different slow start schemes
    }

  CellDirection oppdir = circ->GetOppositeDirection (direction);
  ch = circ->GetChannel (oppdir);

  if (writeevent.IsExpired ())
    {
      writeevent = Simulator::Schedule (Seconds (0), &TorBktapApp::WriteCallback, this);
    }

  Simulator::Schedule (Seconds (0), &TorBktapApp::ReadCallback, this, ch->m_socket);
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

  uint32_t max_read = (queue->cwnd - queue->VirtSize () <= 0) ? 0 : queue->cwnd - queue->VirtSize ();
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

  Time t = Time (Seconds (CELL_PAYLOAD_SIZE * 8 / static_cast<double> (m_burst.GetBitRate ())));
  if (bytes_written > 0)
    {
      m_writebucket.Decrement (bytes_written);
      t = Seconds (0);
    }

  if (writeevent.IsExpired ())
    {
      writeevent = Simulator::Schedule (t, &TorBktapApp::WriteCallback, this);
    }
}

uint32_t
TorBktapApp::FlushPendingCell (Ptr<BktapCircuit> circ, CellDirection direction, bool retx)
{
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  CellDirection oppdir = circ->GetOppositeDirection (direction);
  Ptr<UdpChannel> ch = circ->GetChannel (direction);
  Ptr<Packet> cell;

  if (ch->IsOr () && m_devQlimit <= m_devQ->GetNPackets ())
    {
      return 0;
    }

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
      if (ch->IsEdge ())
        {
          cell->RemoveHeader (header);
        }

      if (circ->GetChannel (oppdir)->IsOr ())
        {
          queue->virtRtt.SentSeq (header.seq);
          queue->actRtt.SentSeq (header.seq);
        }

      int bytes_written = ch->m_socket->SendTo (cell,0,ch->m_remote);
      if (bytes_written > 0)
        {

          if (ch->IsEdge ())
            {
              queue->DiscardUpTo (header.seq + 1);
              ++queue->virtHeadSeq;
            }
          else
            {
              ScheduleRto (circ,direction);
            }

          if (queue->highestTxSeq == header.seq)
            {
              circ->IncrementStats (direction,0,bytes_written);
              SendEmptyAck (circ, oppdir, FWD, queue->highestTxSeq + 1);
            }

          return bytes_written;
        }
    }
  return 0;
}

void
TorBktapApp::SendEmptyAck (Ptr<BktapCircuit> circ, CellDirection direction, uint8_t flag, uint32_t ack)
{
  Ptr<UdpChannel> ch = circ->GetChannel (direction);
  Ptr<SeqQueue> queue = circ->GetQueue (direction);
  NS_ASSERT (ch);
  if (ch->IsOr ())
    {
      Ptr<Packet> cell = Create<Packet> ();
      FdbkCellHeader header;
      header.circId = circ->GetId ();
      header.flags = flag;
      if (flag & ACK)
        {
          header.ack = ack;
        }
      if (flag & FWD)
        {
          header.fwd = ack;
        }
      cell->AddHeader (header);
      queue->ackq.push (cell);
      while (m_devQ->GetNPackets () < m_devQlimit && queue->ackq.size () > 0)
        {
          ch->m_socket->SendTo (queue->ackq.front (), 0, ch->m_remote);
          queue->ackq.pop ();
        }
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
  uint32_t bytes_written = FlushPendingCell (circ,direction);
  m_writebucket.Decrement (bytes_written);
}

Ptr<BktapCircuit>
TorBktapApp::GetCircuit (uint32_t id)
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
  std::map<Address,Ptr<UdpChannel> >::iterator it;
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
UdpChannel::SetRandomVariableStreams (Ptr<RandomVariableStream> rng_request, Ptr<RandomVariableStream> rng_think)
{
  m_rng_request = rng_request;
  m_rng_think = rng_think;
}

Ptr<RandomVariableStream>
UdpChannel::GetRequestStream ()
{
  return m_rng_request;
}

Ptr<RandomVariableStream>
UdpChannel::GetThinkStream ()
{
  return m_rng_think;
}





BaseCircuit::BaseCircuit ()
{
  NS_LOG_FUNCTION (this);
}

BaseCircuit::BaseCircuit (uint32_t id)
{
  NS_LOG_FUNCTION (this);
  m_id = id;
  ResetStats ();
}

BaseCircuit::~BaseCircuit ()
{
  NS_LOG_FUNCTION (this);
}

uint32_t
BaseCircuit::GetId ()
{
  return m_id;
}

CellDirection
BaseCircuit::GetOppositeDirection (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return INBOUND;
    }
  else
    {
      return OUTBOUND;
    }
}

uint32_t
BaseCircuit::GetBytesRead (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return stats_n_bytes_read;
    }
  else
    {
      return stats_p_bytes_read;
    }
}

uint32_t
BaseCircuit::GetBytesWritten (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return stats_n_bytes_written;
    }
  else
    {
      return stats_p_bytes_written;
    }
}

void
BaseCircuit::ResetStats ()
{
  stats_p_bytes_read = 0;
  stats_n_bytes_read = 0;
  stats_p_bytes_written = 0;
  stats_n_bytes_written = 0;
}

void
BaseCircuit::IncrementStats (CellDirection direction, uint32_t read, uint32_t write)
{
  if (direction == OUTBOUND)
    {
      stats_n_bytes_read += read;
      stats_n_bytes_written += write;
    }
  else
    {
      stats_p_bytes_read += read;
      stats_p_bytes_written += write;
    }
}

void
TorBktapApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  std::map<uint16_t,Ptr<BktapCircuit> >::iterator i;
  for (i = circuits.begin (); i != circuits.end (); ++i)
    {
      i->second->inbound = 0;
      i->second->outbound = 0;
      i->second->inboundQueue = 0;
      i->second->outboundQueue = 0;

    }
  circuits.clear ();
  channels.clear ();
  Application::DoDispose ();
}

} //namespace ns3