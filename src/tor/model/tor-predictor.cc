
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include "ns3/point-to-point-net-device.h"

#include "tor-predictor.h"
#include "pytalk.hpp"

using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorPredApp");
NS_OBJECT_ENSURE_REGISTERED (TorPredApp);
NS_OBJECT_ENSURE_REGISTERED (PredCircuit);

TypeId
TorPredApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorPredApp")
    .SetParent<TorBaseApp> ()
    .AddConstructor<TorPredApp> ()
    .AddTraceSource ("NewSocket",
                     "Trace indicating that a new socket has been installed.",
                     MakeTraceSourceAccessor (&TorPredApp::m_triggerNewSocket),
                     "ns3::TorPredApp::TorNewSocketCallback")
    .AddTraceSource ("NewServerSocket",
                     "Trace indicating that a new pseudo server socket has been installed.",
                     MakeTraceSourceAccessor (&TorPredApp::m_triggerNewPseudoServerSocket),
                     "ns3::TorPredApp::TorNewPseudoServerSocketCallback");
  return tid;
}

TorPredApp::TorPredApp (void)
{
  listen_socket = 0;
  m_scheduleReadHead = 0;
  m_scheduleWriteHead = 0;
  controller = CreateObject<PredController> (this);
}

TorPredApp::~TorPredApp (void)
{
  NS_LOG_FUNCTION (this);
}

void
TorPredApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  listen_socket = 0;

  map<uint16_t,Ptr<PredCircuit> >::iterator i;
  for (i = circuits.begin (); i != circuits.end (); ++i)
    {
      i->second->DoDispose ();
    }
  circuits.clear ();
  baseCircuits.clear ();
  connections.clear ();
  Application::DoDispose ();
}


void
TorPredApp::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                    Ptr<PseudoClientSocket> clientSocket)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype);

  // ensure unique id
  NS_ASSERT (circuits[id] == 0);

  // allocate and init new circuit
  Ptr<PredConnection> p_conn = AddConnection (p_ip, p_conntype);
  Ptr<PredConnection> n_conn = AddConnection (n_ip, n_conntype);

  // set client socket of the predecessor connection, if one was given (default is 0)
  p_conn->SetSocket (clientSocket);
  m_triggerNewSocket(this, INBOUND, clientSocket);

  // inform controller about the connections (we only handle the incoming traffic)
  controller->AddInputConnection(n_conn);
  controller->AddOutputConnection(p_conn);

  Ptr<PredCircuit> circ = CreateObject<PredCircuit> (id, n_conn, p_conn);

  // add to circuit list maintained by every connection
  AddActiveCircuit (p_conn, circ);
  AddActiveCircuit (n_conn, circ);

  // add to the global list of circuits
  circuits[id] = circ;
  baseCircuits[id] = circ;
}

Ptr<PredConnection>
TorPredApp::AddConnection (Ipv4Address ip, int conntype)
{
  // find existing or create new connection
  Ptr<PredConnection> conn;
  vector<Ptr<PredConnection> >::iterator it;
  for (it = connections.begin (); it != connections.end (); ++it)
    {
      if ((*it)->GetRemote () == ip)
        {
          conn = *it;
          break;
        }
    }

  if (!conn)
    {
      conn = Create<PredConnection> (this, ip, conntype);
      connections.push_back (conn);
    }

  return conn;
}

void
TorPredApp::AddActiveCircuit (Ptr<PredConnection> conn, Ptr<PredCircuit> circ)
{
  NS_ASSERT (conn);
  NS_ASSERT (circ);
  if (conn)
    {
      if (!conn->GetActiveCircuits ())
        {
          conn->SetActiveCircuits (circ);
          circ->SetNextCirc (conn, circ);
        }
      else
        {
          Ptr<PredCircuit> temp = conn->GetActiveCircuits ()->GetNextCirc (conn);
          circ->SetNextCirc (conn, temp);
          conn->GetActiveCircuits ()->SetNextCirc (conn, circ);
        }
    }
}

void
TorPredApp::StartApplication (void)
{
  TorBaseApp::StartApplication ();
  m_readbucket.SetRefilledCallback (MakeCallback (&TorPredApp::RefillReadCallback, this));
  m_writebucket.SetRefilledCallback (MakeCallback (&TorPredApp::RefillWriteCallback, this));

  // create listen socket
  if (!listen_socket)
    {
      listen_socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
      listen_socket->Bind (m_local);
      listen_socket->Listen ();
      PredConnection::RememberName(Ipv4Address::ConvertFrom (m_ip), GetNodeName());
      cout << "remember " << Ipv4Address::ConvertFrom (m_ip) << " -> " << GetNodeName() << endl;
    }

  listen_socket->SetAcceptCallback (MakeNullCallback<bool,Ptr<Socket>,const Address &> (),
                                    MakeCallback (&TorPredApp::HandleAccept,this));

  Ipv4Mask ipmask = Ipv4Mask ("255.0.0.0");

  // iterate over all neighboring connections
  vector<Ptr<PredConnection> >::iterator it;
  for ( it = connections.begin (); it != connections.end (); it++ )
    {
      Ptr<PredConnection> conn = *it;
      NS_ASSERT (conn);

      // if m_ip smaller then connect to remote node
      if (m_ip < conn->GetRemote () && conn->SpeaksCells ())
        {
          Ptr<Socket> socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
          socket->Bind ();
          socket->Connect (Address (InetSocketAddress (conn->GetRemote (), InetSocketAddress::ConvertFrom (m_local).GetPort ())));
          // socket->SetSendCallback (MakeCallback(&TorPredApp::ConnWriteCallback, this));
          socket->SetDataSentCallback (MakeCallback (&TorPredApp::ConnWriteCallback, this));
          socket->SetRecvCallback (MakeCallback (&TorPredApp::ConnReadCallback, this));
          conn->SetSocket (socket);
          m_triggerNewSocket(this, OUTBOUND, socket);
        }

      if (ipmask.IsMatch (conn->GetRemote (), Ipv4Address ("127.0.0.1")) )
        {
          if (conn->GetType () == SERVEREDGE)
            {
              Ptr<Socket> socket = CreateObject<PseudoServerSocket> ();
              socket->SetDataSentCallback (MakeCallback (&TorPredApp::ConnWriteCallback, this));
              // socket->SetSendCallback(MakeCallback(&TorPredApp::ConnWriteCallback, this));
              socket->SetRecvCallback (MakeCallback (&TorPredApp::ConnReadCallback, this));
              conn->SetSocket (socket);
              m_triggerNewSocket(this, OUTBOUND, socket);

              int circId = conn->GetActiveCircuits () ->GetId ();
              m_triggerNewPseudoServerSocket(this, circId, DynamicCast<PseudoServerSocket>(socket));
            }

          if (conn->GetType () == PROXYEDGE)
            {
              Ptr<Socket> socket = conn->GetSocket ();
              // Create a default pseudo client if none was previously provided via AddCircuit().
              // Normally, this should not happen (a specific pseudo socket should always be
              // provided for a proxy connection).
              if (!socket)
                {
                  socket = CreateObject<PseudoClientSocket> ();
                  m_triggerNewSocket(this, INBOUND, socket);
                }

              socket->SetDataSentCallback (MakeCallback (&TorPredApp::ConnWriteCallback, this));
              // socket->SetSendCallback(MakeCallback(&TorPredApp::ConnWriteCallback, this));
              socket->SetRecvCallback (MakeCallback (&TorPredApp::ConnReadCallback, this));
              conn->SetSocket (socket);
            }
        }
    }
  
  controller->Setup ();
  Simulator::Schedule(Seconds(1), &PredController::Optimize, controller);

  m_triggerAppStart (Ptr<TorBaseApp>(this));
  NS_LOG_INFO ("StartApplication " << m_name << " ip=" << m_ip);
}

void
TorPredApp::StopApplication (void)
{
  // close listen socket
  if (listen_socket)
    {
      listen_socket->Close ();
      listen_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }

  // close all connections
  vector<Ptr<PredConnection> >::iterator it_conn;
  for ( it_conn = connections.begin (); it_conn != connections.end (); ++it_conn )
    {
      Ptr<PredConnection> conn = *it_conn;
      NS_ASSERT (conn);
      if (conn->GetSocket ())
        {
          conn->GetSocket ()->Close ();
          conn->GetSocket ()->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
          conn->GetSocket ()->SetDataSentCallback (MakeNullCallback<void, Ptr<Socket>, uint32_t > ());
          // conn->GetSocket()->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t > ());
        }
    }
}

Ptr<PredCircuit>
TorPredApp::GetCircuit (uint16_t circid)
{
  return circuits[circid];
}


void
TorPredApp::ConnReadCallback (Ptr<Socket> socket)
{
  // At one of the connections, data is ready to be read. This does not mean
  // the data has been read already, but it has arrived in the RX buffer. We
  // decide whether we want to read it or not (depending on whether the
  // connection is blocked).

  NS_ASSERT (socket);
  Ptr<PredConnection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  uint32_t base = conn->SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t max_read = RoundRobin (base, m_readbucket.GetSize ());

  // find the minimum amount of data to read safely from the socket
  max_read = min (max_read, socket->GetRxAvailable ());
  NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << conn->GetRemoteName () << "] Reading " << max_read << "/" << socket->GetRxAvailable () << " bytes");

  if (m_readbucket.GetSize() <= 0 && m_scheduleReadHead == 0)
    {
      m_scheduleReadHead = conn;
    }

  if (max_read <= 0)
    {
      return;
    }

  if (!conn->SpeaksCells ())
    {
      // TODO: how to model reading from edge?
      // max_read = min (conn->GetActiveCircuits ()->GetPackageWindow () * base,max_read);
    }

  vector<Ptr<Packet> > packet_list;
  uint32_t read_bytes = conn->Read (&packet_list, max_read);

  NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << conn->GetRemoteName () << "] Got " << packet_list.size () << " packets (read " << read_bytes << " bytes)");

  for (uint32_t i = 0; i < packet_list.size (); i++)
    {
      if (conn->SpeaksCells ())
        {
          NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << conn->GetRemoteName () << "] Handling relay cell...");
          ReceiveRelayCell (conn, packet_list[i]);
        }
      else
        {
          NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << conn->GetRemoteName () << "] Handling non-relay cell, packaging it...");
          PackageRelayCell (conn, packet_list[i]);
        }
    }

  if (read_bytes > 0)
    {
      // decrement buckets
      GlobalBucketsDecrement (read_bytes, 0);

      // try to read more
      if (socket->GetRxAvailable () > 0)
        {
          // add some virtual processing time before reading more
          Time delay = Time::FromInteger (read_bytes * 2, Time::NS);
          conn->ScheduleRead (delay);
        }
    }
}

void
TorPredApp::PackageRelayCell (Ptr<PredConnection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<PredCircuit> circ = conn->GetActiveCircuits (); // TODO why?, but ok (only one circ on this side (server)
  NS_ASSERT (circ);

  PackageRelayCellImpl (circ->GetId (), cell);

  CellDirection direction = circ->GetOppositeDirection (conn);
  AppendCellToCircuitQueue (circ, cell, direction);
  NS_LOG_LOGIC ("[" << GetNodeName() << ": circuit " << circ->GetId () << "] Appended newly packaged cell to circ queue.");

  // TODO: how to model reading from edge?
}

void
TorPredApp::PackageRelayCellImpl (uint16_t circ_id, Ptr<Packet> cell)
{
  NS_ASSERT (cell);
  CellHeader h;
  h.SetCircId (circ_id);
  h.SetCmd (RELAY_DATA);
  h.SetType (RELAY);
  h.SetLength (cell->GetSize ());
  cell->AddHeader (h);
}

void
TorPredApp::ReceiveRelayCell (Ptr<PredConnection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<PredCircuit> circ = LookupCircuitFromCell (cell);
  NS_ASSERT (circ);
  NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << conn->GetRemoteName () << "] received relay cell.");
  //NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << Ipv4Address::ConvertFrom (conn->GetRemote()) << "/" << conn->GetRemoteName () << "] received relay cell.");

  // find target connection for relaying
  CellDirection direction = circ->GetOppositeDirection (conn);
  Ptr<PredConnection> target_conn = circ->GetConnection (direction);
  NS_ASSERT (target_conn);

  AppendCellToCircuitQueue (circ, cell, direction);
}


Ptr<PredCircuit>
TorPredApp::LookupCircuitFromCell (Ptr<Packet> cell)
{
  NS_ASSERT (cell);
  CellHeader h;
  cell->PeekHeader (h);
  return circuits[h.GetCircId ()];
}


/* Add cell to the queue of circ writing to orconn transmitting in direction. */
void
TorPredApp::AppendCellToCircuitQueue (Ptr<PredCircuit> circ, Ptr<Packet> cell, CellDirection direction)
{
  NS_ASSERT (circ);
  NS_ASSERT (cell);
  queue<Ptr<Packet> > *queue = circ->GetQueue (direction);
  Ptr<PredConnection> conn = circ->GetConnection (direction);
  NS_ASSERT (queue);
  NS_ASSERT (conn);

  circ->PushCell (cell, direction);

  NS_LOG_LOGIC ("[" << GetNodeName() << ": circuit " << circ->GetId () << "] Appended cell. Queue holds " << queue->size () << " cells.");
  conn->ScheduleWrite ();
}


void
TorPredApp::ConnWriteCallback (Ptr<Socket> socket, uint32_t tx)
{
  NS_ASSERT (socket);
  Ptr<PredConnection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  uint32_t newtx = socket->GetTxAvailable ();

  int written_bytes = 0;
  uint32_t base = conn->SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t max_write = RoundRobin (base, m_writebucket.GetSize ());
  max_write = max_write > newtx ? newtx : max_write;

  NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << conn->GetRemoteName () << "] writing at most " << max_write << " bytes into Conn");

  if (m_writebucket.GetSize() <= 0 && m_scheduleWriteHead == 0) {
    m_scheduleWriteHead = conn;
  }

  if (max_write <= 0)
    {
      return;
    }

  written_bytes = conn->Write (max_write);
  NS_LOG_LOGIC ("[" << GetNodeName() << ": connection " << conn->GetRemoteName () << "] " << written_bytes << " bytes written");

  if (written_bytes > 0)
    {
      GlobalBucketsDecrement (0, written_bytes);

      /* try flushing more */
      conn->ScheduleWrite ();
    }
}



void
TorPredApp::HandleAccept (Ptr<Socket> s, const Address& from)
{
  Ptr<PredConnection> conn;
  Ipv4Address ip = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
  vector<Ptr<PredConnection> >::iterator it;
  for (it = connections.begin (); it != connections.end (); ++it)
    {
      if ((*it)->GetRemote () == ip && !(*it)->GetSocket () )
        {
          conn = *it;
          break;
        }
    }

  NS_ASSERT (conn);
  conn->SetSocket (s);
  m_triggerNewSocket(this, INBOUND, s);

  s->SetRecvCallback (MakeCallback (&TorPredApp::ConnReadCallback, this));
  // s->SetSendCallback (MakeCallback(&TorPredApp::ConnWriteCallback, this));
  s->SetDataSentCallback (MakeCallback (&TorPredApp::ConnWriteCallback, this));
  conn->ScheduleWrite();
  conn->ScheduleRead();
}



Ptr<PredConnection>
TorPredApp::LookupConn (Ptr<Socket> socket)
{
  vector<Ptr<PredConnection> >::iterator it;
  for ( it = connections.begin (); it != connections.end (); it++ )
    {
      NS_ASSERT (*it);
      if ((*it)->GetSocket () == socket)
        {
          return (*it);
        }
    }
  return NULL;
}


void
TorPredApp::RefillReadCallback (int64_t prev_read_bucket)
{
  NS_LOG_LOGIC ("[" << GetNodeName() << "] " << "read bucket was " << prev_read_bucket << ". Now " << m_readbucket.GetSize ());
  if (prev_read_bucket <= 0 && m_readbucket.GetSize () > 0)
    {
      vector<Ptr<PredConnection> >::iterator it;
      vector<Ptr<PredConnection> >::iterator headit;

      headit = connections.begin();
      if (m_scheduleReadHead != 0) {
        headit = find(connections.begin(),connections.end(),m_scheduleReadHead);
        m_scheduleReadHead = 0;
      }

      it = headit;
      do {
        Ptr<PredConnection> conn = *it;
        NS_ASSERT(conn);
        conn->ScheduleRead (Time ("10ns"));
        if (++it == connections.end ()) {
          it = connections.begin ();
        }
      } while (it != headit);
    }
}

void
TorPredApp::RefillWriteCallback (int64_t prev_write_bucket)
{
  NS_LOG_LOGIC ("[" << GetNodeName() << "] " << "write bucket was " << prev_write_bucket << ". Now " << m_writebucket.GetSize ());

  if (prev_write_bucket <= 0 && m_writebucket.GetSize () > 0)
    {
      vector<Ptr<PredConnection> >::iterator it;
      vector<Ptr<PredConnection> >::iterator headit;

      headit = connections.begin();
      if (m_scheduleWriteHead != 0) {
        headit = find(connections.begin(),connections.end(),m_scheduleWriteHead);
        m_scheduleWriteHead = 0;
      }

      it = headit;
      do {
        Ptr<PredConnection> conn = *it;
        NS_ASSERT(conn);
        conn->ScheduleWrite ();
        if (++it == connections.end ()) {
          it = connections.begin ();
        }
      } while (it != headit);
    }
}


/** We just read num_read and wrote num_written bytes
 * onto conn. Decrement buckets appropriately. */
void
TorPredApp::GlobalBucketsDecrement (uint32_t num_read, uint32_t num_written)
{
  m_readbucket.Decrement (num_read);
  m_writebucket.Decrement (num_written);
}



/** Helper function to decide how many bytes out of global_bucket
 * we're willing to use for this transaction. Yes, this is how Tor
 * implements it; no kidding. */
uint32_t
TorPredApp::RoundRobin (int base, int64_t global_bucket)
{
  uint32_t num_bytes_high = 32 * base;
  uint32_t num_bytes_low = 4 * base;
  int64_t at_most = global_bucket / 8;
  at_most -= (at_most % base);

  if (at_most > num_bytes_high)
    {
      at_most = num_bytes_high;
    }
  else if (at_most < num_bytes_low)
    {
      at_most = num_bytes_low;
    }

  if (at_most > global_bucket)
    {
      at_most = global_bucket;
    }

  if (at_most < 0)
    {
      return 0;
    }
  return at_most;
}



PredCircuit::PredCircuit (uint16_t circ_id, Ptr<PredConnection> n_conn, Ptr<PredConnection> p_conn) : BaseCircuit (circ_id)
{
  this->p_cellQ = new queue<Ptr<Packet> >;
  this->n_cellQ = new queue<Ptr<Packet> >;

  this->p_conn = p_conn;
  this->n_conn = n_conn;

  this->next_active_on_n_conn = 0;
  this->next_active_on_p_conn = 0;
}


PredCircuit::~PredCircuit ()
{
  NS_LOG_FUNCTION (this);
  delete this->p_cellQ;
  delete this->n_cellQ;
}

void
PredCircuit::DoDispose ()
{
  this->next_active_on_p_conn = 0;
  this->next_active_on_n_conn = 0;
  this->p_conn->SetActiveCircuits (0);
  this->n_conn->SetActiveCircuits (0);
}


Ptr<Packet>
PredCircuit::PopQueue (queue<Ptr<Packet> > *queue)
{
  if (queue->size () > 0)
    {
      Ptr<Packet> cell = queue->front ();
      queue->pop ();

      return cell;
    }
  return 0;
}


Ptr<Packet>
PredCircuit::PopCell (CellDirection direction)
{
  Ptr<Packet> cell;
  if (direction == OUTBOUND)
    {
      cell = this->PopQueue (this->n_cellQ);
    }
  else
    {
      cell = this->PopQueue (this->p_cellQ);
    }

  if (cell)
    {
      // TODO: only if not a special cell?
      IncrementStats (direction, 0, CELL_PAYLOAD_SIZE);
    }

  return cell;
}


void
PredCircuit::PushCell (Ptr<Packet> cell, CellDirection direction)
{
  if (cell)
    {
      Ptr<PredConnection> conn = GetConnection (direction);
      Ptr<PredConnection> opp_conn = GetOppositeConnection (direction);

      if (!conn->SpeaksCells ())
        {
          // delivery

          CellHeader h;
          cell->RemoveHeader (h);
        }

      IncrementStats (direction, CELL_PAYLOAD_SIZE, 0);
      GetQueue (direction)->push (cell);
    }
}





Ptr<PredConnection>
PredCircuit::GetConnection (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->n_conn;
    }
  else
    {
      return this->p_conn;
    }
}

Ptr<PredConnection>
PredCircuit::GetOppositeConnection (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->p_conn;
    }
  else
    {
      return this->n_conn;
    }
}

Ptr<PredConnection>
PredCircuit::GetOppositeConnection (Ptr<PredConnection> conn)
{
  if (this->n_conn == conn)
    {
      return this->p_conn;
    }
  else if (this->p_conn == conn)
    {
      return this->n_conn;
    }
  else
    {
      return 0;
    }
}



CellDirection
PredCircuit::GetDirection (Ptr<PredConnection> conn)
{
  if (this->n_conn == conn)
    {
      return OUTBOUND;
    }
  else
    {
      return INBOUND;
    }
}

CellDirection
PredCircuit::GetOppositeDirection (Ptr<PredConnection> conn)
{
  if (this->n_conn == conn)
    {
      return INBOUND;
    }
  else
    {
      return OUTBOUND;
    }
}

Ptr<PredCircuit>
PredCircuit::GetNextCirc (Ptr<PredConnection> conn)
{
  NS_ASSERT (this->n_conn);
  if (this->n_conn == conn)
    {
      return next_active_on_n_conn;
    }
  else
    {
      return next_active_on_p_conn;
    }
}


void
PredCircuit::SetNextCirc (Ptr<PredConnection> conn, Ptr<PredCircuit> circ)
{
  if (this->n_conn == conn)
    {
      next_active_on_n_conn = circ;
    }
  else
    {
      next_active_on_p_conn = circ;
    }
}


queue<Ptr<Packet> >*
PredCircuit::GetQueue (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->n_cellQ;
    }
  else
    {
      return this->p_cellQ;
    }
}


uint32_t
PredCircuit::GetQueueSize (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->n_cellQ->size ();
    }
  else
    {
      return this->p_cellQ->size ();
    }
}








PredConnection::PredConnection (TorPredApp* torapp, Ipv4Address ip, int conntype)
{
  this->torapp = torapp;
  this->remote = ip;
  this->inbuf.size = 0;
  this->outbuf.size = 0;
  this->active_circuits = 0;

  m_socket = 0;
  m_conntype = conntype;
}


PredConnection::~PredConnection ()
{
  NS_LOG_FUNCTION (this);
}

Ptr<PredCircuit>
PredConnection::GetActiveCircuits ()
{
  return active_circuits;
}

void
PredConnection::SetActiveCircuits (Ptr<PredCircuit> circ)
{
  active_circuits = circ;
}


uint8_t
PredConnection::GetType ()
{
  return m_conntype;
}

bool
PredConnection::SpeaksCells ()
{
  return m_conntype == RELAYEDGE;
}


Ptr<Socket>
PredConnection::GetSocket ()
{
  return m_socket;
}

void
PredConnection::SetSocket (Ptr<Socket> socket)
{
  // called both when establishing the connection and when accepting it
  m_socket = socket;

  // register our RTT estimator
  auto tcp = DynamicCast<TcpSocketBase> (socket);
  if (tcp)
  {
    
    auto l4proto = tcp->GetNode()->GetObject<TcpL4Protocol> ();
    cout << l4proto << endl;
    TypeIdValue default_rtt;
    l4proto->GetAttribute("RttEstimatorType", default_rtt);

    ObjectFactory rttFactory;
    rttFactory.SetTypeId (default_rtt.Get());

    rtt_estimator = rttFactory.Create<RttEstimator> ();
    tcp->SetRtt (rtt_estimator);

    tcp->TraceConnectWithoutContext("HighestSequence", MakeCallback(&PredConnection::UpdateMaxSentSeq, this));
  }
}

void
PredConnection::UpdateMaxSentSeq (SequenceNumber32 old_value, SequenceNumber32 new_value)
{
  max_sent_seq = new_value;
}

Time
PredConnection::EstimateRtt ()
{
  if (rtt_estimator && rtt_estimator->GetNSamples () > 0)
  {
    return rtt_estimator->GetEstimate ();
  }
  return MilliSeconds(200); // TODO
}

Time
PredConnection::GetBaseRtt ()
{
  return torapp->GetPeerDelay (GetRemote ());
}

uint32_t
PredConnection::GetBytesInTransit ()
{
    auto tcp = GetSocket()->GetObject<TcpSocketBase> ();
    if (tcp)
    {
      auto buffer = tcp->GetTxBuffer();

      NS_ASSERT (buffer->TailSequence() - GetHighestTxSeq() >= 0);

      if (buffer->Size() > static_cast<uint32_t>(buffer->TailSequence() - GetHighestTxSeq()))
      {
        return buffer->Size() - static_cast<uint32_t>(buffer->TailSequence() - GetHighestTxSeq());
      }
      else
      {
        return 0;
      }
      
    }
    return 0;
}

Ipv4Address
PredConnection::GetRemote ()
{
  return remote;
}

map<Ipv4Address,string> PredConnection::remote_names;

void
PredConnection::RememberName (Ipv4Address address, string name)
{
  PredConnection::remote_names[address] = name;
}

string
PredConnection::GetRemoteName ()
{
  if (Ipv4Mask ("255.0.0.0").IsMatch (GetRemote (), Ipv4Address ("127.0.0.1")) )
  {
    stringstream ss;
    // GetRemote().Print(ss);
    ss << GetActiveCircuits()->GetId();
    if(DynamicCast<PseudoServerSocket>(GetSocket()))
    {
      ss << "-server";
    }
    else
    {
      NS_ASSERT(DynamicCast<PseudoClientSocket>(GetSocket()));
      ss << "-client";
    }
    
    return string("pseudo-") + ss.str();
  }

  map<Ipv4Address,string>::const_iterator it = PredConnection::remote_names.find(GetRemote ());
  NS_ASSERT(it != PredConnection::remote_names.end() );
  return it->second;
}



uint32_t
PredConnection::Read (vector<Ptr<Packet> >* packet_list, uint32_t max_read)
{
  uint8_t raw_data[max_read + this->inbuf.size];
  memcpy (raw_data, this->inbuf.data, this->inbuf.size);
  uint32_t tmp_available = m_socket->GetRxAvailable ();
  int read_bytes = m_socket->Recv (&raw_data[this->inbuf.size], max_read, 0);

  uint32_t base = SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t datasize = read_bytes + inbuf.size;
  uint32_t leftover = datasize % base;
  int num_packages = datasize / base;

  NS_LOG_LOGIC ("[" << torapp->GetNodeName() << ": connection " << GetRemoteName () << "] " <<
      "GetRxAvailable = " << tmp_available << ", "
      "max_read = " << max_read << ", "
      "read_bytes = " << read_bytes << ", "
      "base = " << base << ", "
      "datasize = " << datasize << ", "
      "leftover = " << leftover << ", "
      "num_packages = " << num_packages
  );

  // slice data into packets
  Ptr<Packet> cell;
  for (int i = 0; i < num_packages; i++)
    {
      cell = Create<Packet> (&raw_data[i * base], base);
      packet_list->push_back (cell);
    }

  //safe leftover
  memcpy (inbuf.data, &raw_data[datasize - leftover], leftover);
  inbuf.size = leftover;

  return read_bytes;
}


uint32_t
PredConnection::Write (uint32_t max_write)
{
  uint32_t base = SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint8_t raw_data[outbuf.size + (max_write / base + 1) * base];
  memcpy (raw_data, outbuf.data, outbuf.size);
  uint32_t datasize = outbuf.size;
  int written_bytes = 0;

  // fill raw_data
  bool flushed_some = false;
  Ptr<PredCircuit> start_circ = GetActiveCircuits ();
  NS_ASSERT (start_circ);
  Ptr<PredCircuit> circ;
  Ptr<Packet> cell = Ptr<Packet> (NULL);
  CellDirection direction;

  NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": connection " << GetRemoteName () << "] Trying to write cells from circuit queues");

  while (datasize < max_write)
    {
      circ = GetActiveCircuits ();
      NS_ASSERT (circ);

      direction = circ->GetDirection (this);
      cell = circ->PopCell (direction);

      if (cell)
        {
          datasize += cell->CopyData (&raw_data[datasize], cell->GetSize ());
          flushed_some = true;
          NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": connection " << GetRemoteName () << "] Actually sending one cell from circuit " << circ->GetId ());
        }

      SetActiveCircuits (circ->GetNextCirc (this));

      if (GetActiveCircuits () == start_circ)
        {
          if (!flushed_some)
            {
              break;
            }
          flushed_some = false;
        }
    }

  // send data
  max_write = min (max_write, datasize);
  if (max_write > 0)
    {
      written_bytes = m_socket->Send (raw_data, max_write, 0);
    }
  NS_ASSERT(written_bytes >= 0);

  /* save leftover for next time */
  written_bytes = max (written_bytes,0);
  uint32_t leftover = datasize - written_bytes;
  memcpy (outbuf.data, &raw_data[datasize - leftover], leftover);
  outbuf.size = leftover;

  if(leftover > 0)
    NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": connection " << GetRemoteName () << "] " << leftover << " bytes left over for next conn write");

  return written_bytes;
}


void
PredConnection::ScheduleWrite (Time delay)
{
  if (m_socket && write_event.IsExpired ())
    {
      write_event = Simulator::Schedule (delay, &TorPredApp::ConnWriteCallback, torapp, m_socket, m_socket->GetTxAvailable ());
    }
}

void
PredConnection::ScheduleRead (Time delay)
{
  if (m_socket && read_event.IsExpired ())
    {
      read_event = Simulator::Schedule (delay, &TorPredApp::ConnReadCallback, torapp, m_socket);
    }
}


uint32_t
PredConnection::GetOutbufSize ()
{
  return outbuf.size;
}

uint32_t
PredConnection::GetInbufSize ()
{
  return inbuf.size;
}


//
// The optimization-based controller of PredicTor.
//

void
PredController::AddInputConnection (Ptr<PredConnection> conn)
{
  in_conns.insert(conn);
}

void
PredController::AddOutputConnection (Ptr<PredConnection> conn)
{
  out_conns.insert(conn);
}

void
PredController::Setup ()
{
  // Get the maximum data rate
  DataRateValue datarate_app;
  app->GetAttribute("BandwidthRate", datarate_app);

  DataRateValue datarate_dev;
  app->GetNode()->GetDevice(0)->GetObject<PointToPointNetDevice>()->GetAttribute("DataRate", datarate_dev);

  max_datarate = std::min (datarate_app.Get(), datarate_dev.Get());

  // collect the input connections...
  vector<vector<uint16_t>> inputs;

  for (auto it = in_conns.begin (); it != in_conns.end(); ++it)
  {
    // ...and their circuits
    auto conn = *it;
    vector<uint16_t> circ_ids;
    
    Ptr<PredCircuit> first_circuit = conn->GetActiveCircuits ();
    circ_ids.push_back(first_circuit->GetId());
    
    auto next_circuit = first_circuit->GetNextCirc(conn);
    while (next_circuit != first_circuit)
    {
      circ_ids.push_back(next_circuit->GetId());
      next_circuit = next_circuit->GetNextCirc(conn);
    }

    inputs.push_back(circ_ids);
  }

  // collect the output connections...
  vector<vector<uint16_t>> outputs;
  vector<double> output_delays;
  num_circuits = 0;

  for (auto it = out_conns.begin (); it != out_conns.end(); ++it)
  {
    // ...and their circuits
    auto conn = *it;
    vector<uint16_t> circ_ids;
    
    Ptr<PredCircuit> first_circuit = conn->GetActiveCircuits ();
    circ_ids.push_back(first_circuit->GetId());
    
    auto next_circuit = first_circuit->GetNextCirc(conn);
    while (next_circuit != first_circuit)
    {
      circ_ids.push_back(next_circuit->GetId());
      next_circuit = next_circuit->GetNextCirc(conn);
    }

    outputs.push_back(circ_ids);
    num_circuits += circ_ids.size();

    // calculate the expected RTT for each outgoing connection
    Time delay = conn->GetBaseRtt();

    // do not allow plain zero, even for edge relays (solver requires this)
    if (delay == Seconds(0))
      delay = MilliSeconds(1);

    output_delays.push_back(delay.GetSeconds ());
  }

  // auto obj = 
  auto result = pyscript.call(
    "setup",
    "relay", app->GetNodeName (),
    "v_max", to_packets_sec(MaxDataRate ()),
    "s_max", 30,
    "dt", TimeStep().GetSeconds(),
    "N_steps", (int) Horizon(),
    "weights", vector<string>({"control_delta", "0.1", "send", "1", "store", "0", "receive", "1"}),
    "n_in", inputs.size(),
    "input_circuits", inputs,
    "n_out", outputs.size(),
    "output_circuits", outputs,
    "output_delays", output_delays
  );

  // Since the batched executor does only return a plain pointer to the parsed
  // JSON doc, we need to free it here.
  delete result;

  // TODO: verify success
}

void
PredController::Optimize ()
{
  // Firstly, measure the necessary local information.

  // Estimate the out-conns' RTTs
  vector<double> rtts_per_conn;

  for (auto&& conn : out_conns)
  {
    rtts_per_conn.push_back(conn->EstimateRtt().GetSeconds());
  }

  // Get the circuits' queue lengthes
  vector<double> packets_per_circuit;
  // TODO: - Verify that the order of these circuits is correct!
  //       - Which order does the solver need?
  //       - Maybe use a map, instead?

  // Also, accumulate the values per connection
  vector<double> packets_per_conn;

  for (auto&& conn : out_conns)
  {
    double this_conn = 0.0;

    Ptr<PredCircuit> first_circuit = conn->GetActiveCircuits ();

    double queue_size = first_circuit->GetQueueSize(first_circuit->GetDirection(conn));
    packets_per_circuit.push_back(queue_size);
    this_conn += queue_size;
    
    auto next_circuit = first_circuit->GetNextCirc(conn);
    while (next_circuit != first_circuit)
    {
      queue_size = next_circuit->GetQueueSize(next_circuit->GetDirection(conn));
      packets_per_circuit.push_back(queue_size);
      this_conn += queue_size;
      
      next_circuit = next_circuit->GetNextCirc(conn);
    }

    packets_per_conn.push_back(this_conn);
  }

  // Get the connections' output transit packet count
  vector<double> transit_packets_per_conn;

  for (auto&& conn : out_conns)
  {
    transit_packets_per_conn.push_back((double) conn->GetBytesInTransit() / CELL_NETWORK_SIZE);
  }

  //
  // Collect the trajectories necessary for optimizing
  //

  // data we are expected to receive from our predecessors (their v_out)
  vector<Trajectory> v_in_req;
  if (false)
  {
    // TODO
    // use collected data from other relays
  }
  else
  {
    // We do not yet have data from other relays, assume that they do not send anything
    Trajectory idle{this, Simulator::Now()};
    for (unsigned int i=0; i<Horizon(); i++)
    {
      idle.Elements().push_back(0.0);
    }

    for (unsigned int i=0; i<in_conns.size() ; i++)
    {
      v_in_req.push_back(idle);
    }
  }

  // composition of each input connection
  vector<vector<vector<double>>> cv_in;
  if (false)
  {
    // TODO
    // use collected data from other relays
  }
  else
  {
    // We do not yet have data from other relays, assume the circuits share
    // the connection's traffic equally.

    // - horizon
    //   - n_in (connections)
    //     - n_circuit_in[i] (circuits)

    for (unsigned int i=0; i<Horizon(); i++)
    {
      vector<vector<double>> this_step;
      for (auto&& conn : in_conns)
      {
        int n_circuit_in = 1;
        Ptr<PredCircuit> first_circuit = conn->GetActiveCircuits ();
        
        auto next_circuit = first_circuit->GetNextCirc(conn);
        while (next_circuit != first_circuit)
        {
          n_circuit_in++;
          next_circuit = next_circuit->GetNextCirc(conn);
        }

        vector<double> composition;
        for (int i=0; i<n_circuit_in; i++)
        {
          composition.push_back (1.0 / n_circuit_in);
        }

        this_step.push_back(composition);
      }
      cv_in.push_back(this_step);
    }
  }

  // maximum outgoing data rate we were given by the successor
  vector<Trajectory> v_out_max;
  if (false)
  {
    // TODO
    // use collected data from other relays
  }
  else
  {
    // We do not yet have data from other relays, assume we can can go full blast
    Trajectory unlimited{this, Simulator::Now()};
    for (unsigned int i=0; i<Horizon(); i++)
    {
      unlimited.Elements().push_back(to_packets_sec(MaxDataRate ()));
    }

    for (unsigned int i=0; i<out_conns.size() ; i++)
    {
      v_out_max.push_back(unlimited);
    }
  }

  // future development of the successors' memory load
  vector<Trajectory> memory_load_target;
  if (false)
  {
    // TODO
    // use collected data from other relays
  }
  else
  {
    // We do not yet have data from other relays, assume that they are idle
    Trajectory idle{this, Simulator::Now()};
    for (unsigned int i=0; i<Horizon(); i++)
    {
      idle.Elements().push_back(0.0);
    }

    for (unsigned int i=0; i<out_conns.size() ; i++)
    {
      memory_load_target.push_back(idle);
    }
  }

  // future development of the predecessors' memory load
  vector<Trajectory> memory_load_source;
  if (false)
  {
    // TODO
    // use collected data from other relays
  }
  else
  {
    // We do not yet have data from other relays, assume that they are idle
    Trajectory idle{this, Simulator::Now()};
    for (unsigned int i=0; i<Horizon(); i++)
    {
      idle.Elements().push_back(0.0);
    }

    for (unsigned int i=0; i<in_conns.size() ; i++)
    {
      memory_load_source.push_back(idle);
    }
  }

  // future development of the successors' bandwidth load
  vector<Trajectory> bandwidth_load_target;
  if (false)
  {
    // TODO
    // use collected data from other relays
  }
  else
  {
    // We do not yet have data from other relays, assume that they are idle
    Trajectory idle{this, Simulator::Now()};
    for (unsigned int i=0; i<Horizon(); i++)
    {
      idle.Elements().push_back(0.0);
    }

    for (unsigned int i=0; i<out_conns.size() ; i++)
    {
      bandwidth_load_target.push_back(idle);
    }
  }

  // future development of the predecessors' bandwidth load
  vector<Trajectory> bandwidth_load_source;
  if (false)
  {
    // TODO
    // use collected data from other relays
  }
  else
  {
    // We do not yet have data from other relays, assume that they are idle
    Trajectory idle{this, Simulator::Now()};
    for (unsigned int i=0; i<Horizon(); i++)
    {
      idle.Elements().push_back(0.0);
    }

    for (unsigned int i=0; i<in_conns.size() ; i++)
    {
      bandwidth_load_source.push_back(idle);
    }
  }
  vector<double> output_delays;

  for (auto it = out_conns.begin (); it != out_conns.end(); ++it)
  {
    // ...and their circuits
    auto conn = *it;
    vector<uint16_t> circ_ids;
    
    Ptr<PredCircuit> first_circuit = conn->GetActiveCircuits ();
    circ_ids.push_back(first_circuit->GetId());
    
    auto next_circuit = first_circuit->GetNextCirc(conn);
    while (next_circuit != first_circuit)
    {
      circ_ids.push_back(next_circuit->GetId());
      next_circuit = next_circuit->GetNextCirc(conn);
    }

    // calculate the expected RTT for each outgoing connection
    Time delay = conn->GetBaseRtt();

    // do not allow plain zero, even for edge relays (solver requires this)
    if (delay == Seconds(0))
      delay = MilliSeconds(1);

    output_delays.push_back(delay.GetSeconds ());
  }
  

  // Call the optimizer
  executor.Compute([=]
    {
      return pyscript.call(
        "solve",
        "time", Simulator::Now().GetSeconds(),
        "relay", app->GetNodeName (),
        "s_buffer_0", packets_per_conn,
        "s_circuit_0", packets_per_circuit,
        "s_transit_0", transit_packets_per_conn,
        "output_delay", output_delays,

        // trajectories
        "v_in_req", transpose_to_double_vectors(v_in_req),
        "cv_in", cv_in,
        "v_out_max", transpose_to_double_vectors(v_out_max),
        "memory_load_target", transpose_to_double_vectors(memory_load_target),
        "memory_load_source", transpose_to_double_vectors(memory_load_source),
        "bandwidth_load_target", transpose_to_double_vectors(bandwidth_load_target),
        "bandwidth_load_source", transpose_to_double_vectors(bandwidth_load_source)
      );
    },
    MakeCallback(&PredController::OptimizeDone, this)
  );

  // Schedule the next optimization step
  Simulator::Schedule(TimeStep (), &PredController::Optimize, this);
}

double
PredController::to_packets_sec(DataRate rate)
{
  return rate.GetBitRate() / (8.0*double{CELL_NETWORK_SIZE});
}

DataRate
PredController::to_datarate(double pps)
{
  return DataRate { uint64_t(pps*8*CELL_NETWORK_SIZE) };
}

vector<vector<double>>
PredController::to_double_vectors(vector<Trajectory> trajectories)
{
  vector<vector<double>> result;
  for (auto&& traj : trajectories)
  {
    result.push_back(traj.Elements());
  }
  return result;
}

vector<vector<double>>
PredController::transpose_to_double_vectors(vector<Trajectory> trajectories)
{
  vector<vector<double>> result;

  NS_ASSERT (trajectories.size() > 0);
  
  const int horizon = trajectories[0].Steps();

  for (int i=0; i<horizon; i++)
  {
    vector<double> this_step;

    for (auto&& traj : trajectories)
    {
      this_step.push_back(traj.Elements().at(i));
    }

    result.push_back(this_step);
  }

  return result;
}

void
PredController::OptimizeDone(rapidjson::Document * doc)
{
  // TODO: Save the results (plans, etc.)
  Time now = Simulator::Now();
  Time next_step = now + TimeStep();

  ParseIntoTrajectories((*doc)["v_in"], pred_v_in, now, in_conns.size());
  ParseIntoTrajectories((*doc)["v_in_max"], pred_v_in_max, now, in_conns.size());
  ParseIntoTrajectories((*doc)["v_out"], pred_v_out, now, out_conns.size());
  ParseIntoTrajectories((*doc)["v_out_max"], pred_v_out_max, now, out_conns.size());
  ParseIntoTrajectories((*doc)["s_buffer"], pred_s_buffer, next_step, out_conns.size());
  ParseIntoTrajectories((*doc)["s_circuit"], pred_s_circuit, next_step, num_circuits);
  ParseIntoTrajectories((*doc)["bandwidth_load_target"], pred_bandwidth_load_target, next_step, out_conns.size());
  ParseIntoTrajectories((*doc)["memory_load_target"], pred_memory_load_target, next_step, out_conns.size());
  ParseIntoTrajectories((*doc)["bandwidth_load_source"], pred_bandwidth_load_source, next_step, in_conns.size());
  ParseIntoTrajectories((*doc)["memory_load_source"], pred_memory_load_source, next_step, in_conns.size());

  ParseCvInIntoTrajectories((*doc)["cv_in"], pred_cv_in, now, in_conns.size());
  ParseCvOutIntoTrajectories((*doc)["cv_out"], pred_cv_out, now, out_conns.size());
  

  // Since the batched executor does only return a plain pointer to the parsed
  // JSON doc, we need to free it here.
  delete doc;
}

void
PredController::ParseIntoTrajectories(const rapidjson::Value& array, vector<Trajectory>& target, Time first_time, size_t expected_traj)
{
  NS_ASSERT(array.IsArray());
  NS_ASSERT(array.Size() > 0);

  NS_ASSERT(array[0].IsArray());
  const size_t num_traj = array[0].Size();
  NS_ASSERT(num_traj == expected_traj);

  target.clear();
  for (size_t i=0; i<num_traj; i++) {
    target.push_back(Trajectory{this, first_time});
  }

  for (size_t step=0; step<array.Size(); step++) {
    NS_ASSERT(array[step].IsArray());
    NS_ASSERT(array[step].Size() == num_traj);

    for (size_t traj=0; traj< num_traj; traj++) {

    //   const char* kTypeNames[] = 
    // { "Null", "False", "True", "Object", "Array", "String", "Number" };
    //   cout << kTypeNames[array[step][traj].GetType()] << endl;

      // make sure this is a one-element array...
      NS_ASSERT(array[step][traj].IsArray());
      NS_ASSERT(array[step][traj].Size() == 1);

      NS_ASSERT(array[step][traj][0].IsDouble());

      target[traj].Elements().push_back(array[step][traj][0].GetDouble());
    }
  }
}

void
PredController::ParseCvOutIntoTrajectories(const rapidjson::Value& array, vector<vector<Trajectory>>& target, Time first_time, size_t expected_traj_outer)
{
  NS_ASSERT(array.IsArray());
  NS_ASSERT(array.Size() > 0);

  NS_ASSERT(array[0].IsArray());
  const size_t num_conns = array[0].Size();
  cout << num_conns << " " << expected_traj_outer << endl;
  NS_ASSERT(num_conns == expected_traj_outer);

  target.clear();

  map<size_t,size_t> conn_to_circnum;

  // For each of the connections, add a vector that contains all of the respective circuits...
  for (size_t conn=0; conn<num_conns; conn++) {
    target.push_back(vector<Trajectory>{});
    
    /// ...and initialize a trajectory for each of its circuits
    NS_ASSERT(array[0][conn].IsArray());
    conn_to_circnum[conn] = array[0][conn].Size();

    for (size_t i=0; i<conn_to_circnum[conn]; i++) {
      target[conn].push_back(Trajectory{this,first_time});
    }
  }

  for (size_t step=0; step<array.Size(); step++) {
    NS_ASSERT(array[step].IsArray());
    NS_ASSERT(array[step].Size() == num_conns);

    for (size_t conn=0; conn<num_conns; conn++) {
      NS_ASSERT(array[step][conn].IsArray());

      // Ensure the number of circuits per connection is constant over time
      NS_ASSERT(array[step][conn].Size() == conn_to_circnum[conn]);

      for (size_t circ=0; circ<array[step][conn].Size(); circ++) {
        NS_ASSERT(array[step][conn][circ].IsArray());
        NS_ASSERT(array[step][conn][circ].Size() == 1);
        NS_ASSERT(array[step][conn][circ][0].IsDouble());
        double val = array[step][conn][circ][0].GetDouble();
        
        target[conn][circ].Elements().push_back(val);
      }
    }
  }
}

void
PredController::ParseCvInIntoTrajectories(const rapidjson::Value& array, vector<vector<Trajectory>>& target, Time first_time, size_t connections)
{
  NS_ASSERT(array.IsArray());
  NS_ASSERT(array.Size() > 0);

  NS_ASSERT(array[0].IsArray());
  const size_t num_elements = array[0].Size();

  NS_ASSERT(num_elements > 0);
  NS_ASSERT(num_elements % connections == 0);
  size_t num_steps = num_elements / connections;

  map<size_t,size_t> conn_to_circnum;
  
  target.clear();

  for(size_t conn=0; conn<connections; conn++) {
    NS_ASSERT(array[0][conn].IsArray());

    conn_to_circnum[conn] = array[0][conn].Size();
    target.push_back(vector<Trajectory>{});

    for (size_t i=0; i<conn_to_circnum[conn]; i++) {
      target[conn].push_back(Trajectory{this,first_time});
    }
  }

  for (size_t step=0; step<num_steps; step++) {
    for (size_t conn=0; conn<connections; conn++) {
      NS_ASSERT(array[0][step*connections+conn].IsArray());

      // Ensure the number of circuits per connection is constant over time
      NS_ASSERT(array[0][step*connections+conn].Size() == conn_to_circnum[conn]);

      for (size_t circ=0; circ<array[0][step*connections+conn].Size(); circ++) {
        NS_ASSERT(array[0][step*connections+conn][circ].IsArray());
        NS_ASSERT(array[0][step*connections+conn][circ].Size() == 1);
        NS_ASSERT(array[0][step*connections+conn][circ][0].IsDouble());
        double val = array[0][step*connections+conn][circ][0].GetDouble();
        
        target[conn][circ].Elements().push_back(val);
      }
    }
  }
}

BatchedExecutor PredController::executor;

//
// Container for handling trajectory data
//

Trajectory Trajectory::InterpolateToTime (Time target_time)
{
  // TODO: If target time is after the current start time, do not "cut off"
  //       the beginning of our trajectory, but keep the overall sum equal.
  if (target_time == first_time)
  {
    // If no interpolation necessary, just copy this trajectory
    return Trajectory{*this};
  }

  Trajectory result{time_step, target_time};
  int num_steps = Steps();
  
  Time last_time = target_time;

  for (int i=0; i<num_steps; i++)
  {
    // Add one step
    result.elements.push_back(GetAverageValue(last_time, last_time + time_step));
    last_time = last_time + time_step;
  }

  return result;
}

} //namespace ns3
