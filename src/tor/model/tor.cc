
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"

#include "tor.h"

using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorApp");
NS_OBJECT_ENSURE_REGISTERED (TorApp);
NS_OBJECT_ENSURE_REGISTERED (Circuit);

TypeId
TorApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorApp")
    .SetParent<TorBaseApp> ()
    .AddConstructor<TorApp> ()
    .AddAttribute ("WindowStart", "End-to-end sliding window size (in cells).",
                   IntegerValue (1000),
                   MakeIntegerAccessor (&TorApp::m_windowStart),
                   MakeIntegerChecker<int> ())
    .AddAttribute ("WindowIncrement", "End-to-end sliding window increment (in cells).",
                   IntegerValue (100),
                   MakeIntegerAccessor (&TorApp::m_windowIncrement),
                   MakeIntegerChecker<int> ())
    .AddTraceSource ("NewSocket",
                     "Trace indicating that a new socket has been installed.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerNewSocket),
                     "ns3::TorApp::TorNewSocketCallback")
    .AddTraceSource ("NewServerSocket",
                     "Trace indicating that a new pseudo server socket has been installed.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerNewPseudoServerSocket),
                     "ns3::TorApp::TorNewPseudoServerSocketCallback")
    .AddTraceSource ("BytesEnteredNetwork",
                     "Trace indicating that an exit sent a byte of data, identified by its index, into the network.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerBytesEnteredNetwork),
                     "ns3::TorPredApp::TorBytesEnteredNetworkCallback")
    .AddTraceSource ("BytesLeftNetwork",
                     "Trace indicating that an eentry received a byte of data, identified by its index, from the network.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerBytesLeftNetwork),
                     "ns3::TorPredApp::TorBytesLeftNetworkCallback");
  return tid;
}

TorApp::TorApp (void)
{
  listen_socket = 0;
  m_scheduleReadHead = 0;
  m_scheduleWriteHead = 0;
}

TorApp::~TorApp (void)
{
  NS_LOG_FUNCTION (this);
}

void
TorApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  listen_socket = 0;

  map<uint16_t,Ptr<Circuit> >::iterator i;
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
TorApp::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                    Ptr<PseudoClientSocket> clientSocket)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype);

  // ensure unique id
  NS_ASSERT (circuits[id] == 0);

  // allocate and init new circuit
  Ptr<Connection> p_conn = AddConnection (p_ip, p_conntype);
  Ptr<Connection> n_conn = AddConnection (n_ip, n_conntype);

  // set client socket of the predecessor connection, if one was given (default is 0)
  p_conn->SetSocket (clientSocket);
  m_triggerNewSocket(this, INBOUND, clientSocket);

  Ptr<Circuit> circ = CreateObject<Circuit> (id, n_conn, p_conn, m_windowStart, m_windowIncrement);

  // add to circuit list maintained by every connection
  AddActiveCircuit (p_conn, circ);
  AddActiveCircuit (n_conn, circ);

  // add to the global list of circuits
  circuits[id] = circ;
  baseCircuits[id] = circ;
}

Ptr<Connection>
TorApp::AddConnection (Ipv4Address ip, int conntype)
{
  // find existing or create new connection
  Ptr<Connection> conn;
  vector<Ptr<Connection> >::iterator it;
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
      conn = Create<Connection> (this, ip, conntype);
      connections.push_back (conn);
    }

  return conn;
}

void
TorApp::AddActiveCircuit (Ptr<Connection> conn, Ptr<Circuit> circ)
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
          Ptr<Circuit> temp = conn->GetActiveCircuits ()->GetNextCirc (conn);
          circ->SetNextCirc (conn, temp);
          conn->GetActiveCircuits ()->SetNextCirc (conn, circ);
        }
    }
}

void
TorApp::StartApplication (void)
{
  TorBaseApp::StartApplication ();
  m_readbucket.SetRefilledCallback (MakeCallback (&TorApp::RefillReadCallback, this));
  m_writebucket.SetRefilledCallback (MakeCallback (&TorApp::RefillWriteCallback, this));

  // create listen socket
  if (!listen_socket)
    {
      listen_socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
      listen_socket->Bind (m_local);
      listen_socket->Listen ();
      Connection::RememberName(Ipv4Address::ConvertFrom (m_ip), GetNodeName());
      cout << "remember " << Ipv4Address::ConvertFrom (m_ip) << " -> " << GetNodeName() << endl;
    }

  listen_socket->SetAcceptCallback (MakeNullCallback<bool,Ptr<Socket>,const Address &> (),
                                    MakeCallback (&TorApp::HandleAccept,this));

  Ipv4Mask ipmask = Ipv4Mask ("255.0.0.0");

  // iterate over all neighboring connections
  vector<Ptr<Connection> >::iterator it;
  for ( it = connections.begin (); it != connections.end (); it++ )
    {
      Ptr<Connection> conn = *it;
      NS_ASSERT (conn);

      // if m_ip smaller then connect to remote node
      if (m_ip < conn->GetRemote () && conn->SpeaksCells ())
        {
          Ptr<Socket> socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
          socket->Bind ();
          socket->Connect (Address (InetSocketAddress (conn->GetRemote (), InetSocketAddress::ConvertFrom (m_local).GetPort ())));
          socket->SetSendCallback (MakeCallback(&TorApp::ConnWriteCallback, this));
          // socket->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
          socket->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
          conn->SetSocket (socket);
          m_triggerNewSocket(this, OUTBOUND, socket);
        }

      if (ipmask.IsMatch (conn->GetRemote (), Ipv4Address ("127.0.0.1")) )
        {
          if (conn->GetType () == SERVEREDGE)
            {
              Ptr<Socket> socket = CreateObject<PseudoServerSocket> ();
              // socket->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
              socket->SetSendCallback(MakeCallback(&TorApp::ConnWriteCallback, this));
              socket->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
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

              // socket->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
              socket->SetSendCallback(MakeCallback(&TorApp::ConnWriteCallback, this));
              socket->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
              conn->SetSocket (socket);
            }
        }
    }

  m_triggerAppStart (Ptr<TorBaseApp>(this));
  NS_LOG_INFO ("StartApplication " << m_name << " ip=" << m_ip);
}

void
TorApp::StopApplication (void)
{
  // close listen socket
  if (listen_socket)
    {
      listen_socket->Close ();
      listen_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }

  // close all connections
  vector<Ptr<Connection> >::iterator it_conn;
  for ( it_conn = connections.begin (); it_conn != connections.end (); ++it_conn )
    {
      Ptr<Connection> conn = *it_conn;
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

Ptr<Circuit>
TorApp::GetCircuit (uint16_t circid)
{
  return circuits[circid];
}


void
TorApp::ConnReadCallback (Ptr<Socket> socket)
{
  // At one of the connections, data is ready to be read. This does not mean
  // the data has been read already, but it has arrived in the RX buffer. We
  // decide whether we want to read it or not (depending on whether the
  // connection is blocked).

  NS_ASSERT (socket);
  Ptr<Connection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  if (conn->IsBlocked ())
    {
      NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Reading blocked, return");
      return;
    }

  uint32_t base = conn->SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t max_read = RoundRobin (base, m_readbucket.GetSize ());

  // find the minimum amount of data to read safely from the socket
  max_read = min (max_read, socket->GetRxAvailable ());
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Reading " << max_read << "/" << socket->GetRxAvailable () << " bytes");

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
      max_read = min (conn->GetActiveCircuits ()->GetPackageWindow () * base,max_read);
    }

  vector<Ptr<Packet> > packet_list;
  uint32_t read_bytes = conn->Read (&packet_list, max_read);

  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Got " << packet_list.size () << " packets (read " << read_bytes << " bytes)");

  for (uint32_t i = 0; i < packet_list.size (); i++)
    {
      if (conn->SpeaksCells ())
        {
          NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Handling relay cell...");
          ReceiveRelayCell (conn, packet_list[i]);
        }
      else
        {
          NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Handling non-relay cell, packaging it...");
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
TorApp::PackageRelayCell (Ptr<Connection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<Circuit> circ = conn->GetActiveCircuits (); // TODO why?, but ok (only one circ on this side (server)
  NS_ASSERT (circ);

  PackageRelayCellImpl (circ->GetId (), cell);

  CellDirection direction = circ->GetOppositeDirection (conn);
  AppendCellToCircuitQueue (circ, cell, direction);
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Appended newly packaged cell to circ queue.");
  if (circ->GetPackageWindow () <= 0)
    {
      NS_LOG_LOGIC ("[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Package window empty now. Block reading from " << conn->GetRemote());
      conn->SetBlocked (true);
      // TODO blocking the whole connection if SENDME window for one of its
      //      circuits is empty
      //  ==> server only!!
    }
}

void
TorApp::PackageRelayCellImpl (uint16_t circ_id, Ptr<Packet> cell)
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
TorApp::ReceiveRelayCell (Ptr<Connection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<Circuit> circ = LookupCircuitFromCell (cell);
  NS_ASSERT (circ);
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] received relay cell.");
  //NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << Ipv4Address::ConvertFrom (conn->GetRemote()) << "/" << conn->GetRemoteName () << "] received relay cell.");

  // find target connection for relaying
  CellDirection direction = circ->GetOppositeDirection (conn);
  Ptr<Connection> target_conn = circ->GetConnection (direction);
  NS_ASSERT (target_conn);
  target_conn->CountFinalReception(circ->GetId(), cell->GetSize());

  AppendCellToCircuitQueue (circ, cell, direction);
}


Ptr<Circuit>
TorApp::LookupCircuitFromCell (Ptr<Packet> cell)
{
  NS_ASSERT (cell);
  CellHeader h;
  cell->PeekHeader (h);
  return circuits[h.GetCircId ()];
}


/* Add cell to the queue of circ writing to orconn transmitting in direction. */
void
TorApp::AppendCellToCircuitQueue (Ptr<Circuit> circ, Ptr<Packet> cell, CellDirection direction)
{
  NS_ASSERT (circ);
  NS_ASSERT (cell);
  queue<Ptr<Packet> > *queue = circ->GetQueue (direction);
  Ptr<Connection> conn = circ->GetConnection (direction);
  NS_ASSERT (queue);
  NS_ASSERT (conn);

  circ->PushCell (cell, direction);

  NS_LOG_LOGIC ("[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Appended cell. Queue holds " << queue->size () << " cells.");
  conn->ScheduleWrite ();
}


void
TorApp::ConnWriteCallback (Ptr<Socket> socket, uint32_t tx)
{
  NS_ASSERT (socket);
  Ptr<Connection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  uint32_t newtx = socket->GetTxAvailable ();

  int written_bytes = 0;
  uint32_t base = conn->SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t max_write = RoundRobin (base, m_writebucket.GetSize ());
  max_write = max_write > newtx ? newtx : max_write;

  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] writing at most " << max_write << " bytes into Conn");

  if (m_writebucket.GetSize() <= 0 && m_scheduleWriteHead == 0) {
    m_scheduleWriteHead = conn;
  }

  if (max_write <= 0)
    {
      return;
    }

  written_bytes = conn->Write (max_write);
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] " << written_bytes << " bytes written");

  if (written_bytes > 0)
    {
      GlobalBucketsDecrement (0, written_bytes);

      /* try flushing more */
      conn->ScheduleWrite ();
    }
}



void
TorApp::HandleAccept (Ptr<Socket> s, const Address& from)
{
  Ptr<Connection> conn;
  Ipv4Address ip = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
  vector<Ptr<Connection> >::iterator it;
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

  s->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
  s->SetSendCallback (MakeCallback(&TorApp::ConnWriteCallback, this));
  // s->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
  conn->ScheduleWrite();
  conn->ScheduleRead();
}



Ptr<Connection>
TorApp::LookupConn (Ptr<Socket> socket)
{
  vector<Ptr<Connection> >::iterator it;
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
TorApp::RefillReadCallback (int64_t prev_read_bucket)
{
  NS_LOG_LOGIC ("read bucket was " << prev_read_bucket << ". Now " << m_readbucket.GetSize ());
  if (prev_read_bucket <= 0 && m_readbucket.GetSize () > 0)
    {
      vector<Ptr<Connection> >::iterator it;
      vector<Ptr<Connection> >::iterator headit;

      headit = connections.begin();
      if (m_scheduleReadHead != 0) {
        headit = find(connections.begin(),connections.end(),m_scheduleReadHead);
        m_scheduleReadHead = 0;
      }

      it = headit;
      do {
        Ptr<Connection> conn = *it;
        NS_ASSERT(conn);
        conn->ScheduleRead (Time ("10ns"));
        if (++it == connections.end ()) {
          it = connections.begin ();
        }
      } while (it != headit);
    }
}

void
TorApp::RefillWriteCallback (int64_t prev_write_bucket)
{
  NS_LOG_LOGIC ("write bucket was " << prev_write_bucket << ". Now " << m_writebucket.GetSize ());

  if (prev_write_bucket <= 0 && m_writebucket.GetSize () > 0)
    {
      vector<Ptr<Connection> >::iterator it;
      vector<Ptr<Connection> >::iterator headit;

      headit = connections.begin();
      if (m_scheduleWriteHead != 0) {
        headit = find(connections.begin(),connections.end(),m_scheduleWriteHead);
        m_scheduleWriteHead = 0;
      }

      it = headit;
      do {
        Ptr<Connection> conn = *it;
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
TorApp::GlobalBucketsDecrement (uint32_t num_read, uint32_t num_written)
{
  m_readbucket.Decrement (num_read);
  m_writebucket.Decrement (num_written);
}



/** Helper function to decide how many bytes out of global_bucket
 * we're willing to use for this transaction. Yes, this is how Tor
 * implements it; no kidding. */
uint32_t
TorApp::RoundRobin (int base, int64_t global_bucket)
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



Circuit::Circuit (uint16_t circ_id, Ptr<Connection> n_conn, Ptr<Connection> p_conn,
                  int windowStart, int windowIncrement) : BaseCircuit (circ_id)
{
  this->p_cellQ = new queue<Ptr<Packet> >;
  this->n_cellQ = new queue<Ptr<Packet> >;

  m_windowStart = windowStart;
  m_windowIncrement = windowIncrement;
  this->deliver_window = m_windowStart;
  this->package_window = m_windowStart;

  this->p_conn = p_conn;
  this->n_conn = n_conn;

  this->next_active_on_n_conn = 0;
  this->next_active_on_p_conn = 0;
}


Circuit::~Circuit ()
{
  NS_LOG_FUNCTION (this);
  delete this->p_cellQ;
  delete this->n_cellQ;
}

void
Circuit::DoDispose ()
{
  this->next_active_on_p_conn = 0;
  this->next_active_on_n_conn = 0;
  this->p_conn->SetActiveCircuits (0);
  this->n_conn->SetActiveCircuits (0);
}


Ptr<Packet>
Circuit::PopQueue (queue<Ptr<Packet> > *queue)
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
Circuit::PopCell (CellDirection direction)
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
      if (!IsSendme (cell))
        {
          IncrementStats (direction, 0, CELL_PAYLOAD_SIZE);
        }

      /* handle sending sendme cells here (instead of in PushCell) because
       * otherwise short circuits could have more than a window-ful of cells
       * in-flight. Regular circuits will not be affected by this. */
      Ptr<Connection> conn = GetConnection (direction);
      if (!conn->SpeaksCells ())
        {
          deliver_window--;
          if (deliver_window <= m_windowStart - m_windowIncrement)
            {
              IncDeliverWindow ();
              NS_LOG_LOGIC ("[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << "] Send SENDME cell ");
              Ptr<Packet> sendme_cell = CreateSendme ();
              GetQueue (BaseCircuit::GetOppositeDirection (direction))->push (sendme_cell);
              GetOppositeConnection (direction)->ScheduleWrite ();
            }
        }
    }

  return cell;
}


void
Circuit::PushCell (Ptr<Packet> cell, CellDirection direction)
{
  if (cell)
    {
      Ptr<Connection> conn = GetConnection (direction);
      Ptr<Connection> opp_conn = GetOppositeConnection (direction);

      if (!opp_conn->SpeaksCells ())
        {
          // new packaged cell
          package_window--;
          if (package_window <= 0)
            {
              //block connection
              opp_conn->SetBlocked (true);
            }
        }

      if (!conn->SpeaksCells ())
        {
          // delivery
          if (IsSendme (cell))
            {
              // update package window
              IncPackageWindow ();
              NS_LOG_LOGIC ("[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << "] Received SENDME cell. Package window now " << package_window);
              if (conn->IsBlocked ())
                {
                  conn->SetBlocked (false);
                  conn->ScheduleRead ();
                }

              // no stats and no cell push on sendme cells
              return;
            }

          CellHeader h;
          cell->RemoveHeader (h);
        }

      IncrementStats (direction, CELL_PAYLOAD_SIZE, 0);
      GetQueue (direction)->push (cell);
    }
}





Ptr<Connection>
Circuit::GetConnection (CellDirection direction)
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

Ptr<Connection>
Circuit::GetOppositeConnection (CellDirection direction)
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

Ptr<Connection>
Circuit::GetOppositeConnection (Ptr<Connection> conn)
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
Circuit::GetDirection (Ptr<Connection> conn)
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
Circuit::GetOppositeDirection (Ptr<Connection> conn)
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

Ptr<Circuit>
Circuit::GetNextCirc (Ptr<Connection> conn)
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
Circuit::SetNextCirc (Ptr<Connection> conn, Ptr<Circuit> circ)
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


bool
Circuit::IsSendme (Ptr<Packet> cell)
{
  if (!cell)
    {
      return false;
    }
  CellHeader h;
  cell->PeekHeader (h);
  if (h.GetCmd () == RELAY_SENDME)
    {
      return true;
    }
  return false;
}

Ptr<Packet>
Circuit::CreateSendme ()
{
  CellHeader h;
  h.SetCircId (GetId ());
  h.SetType (RELAY);
  h.SetStreamId (42);
  h.SetCmd (RELAY_SENDME);
  h.SetLength (0);
  Ptr<Packet> cell = Create<Packet> (CELL_PAYLOAD_SIZE);
  cell->AddHeader (h);

  return cell;
}


queue<Ptr<Packet> >*
Circuit::GetQueue (CellDirection direction)
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
Circuit::GetQueueSize (CellDirection direction)
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

uint32_t
Circuit::GetQueueSizeBytes (CellDirection direction)
{
  queue<Ptr<Packet>> * source = (direction == OUTBOUND) ? this->n_cellQ : this->p_cellQ;
  
  // copy temporarily
  queue<Ptr<Packet>> q{*source};

  uint32_t result = 0;
  while (q.size() > 0)
  {
    result += q.front()->GetSize();
    q.pop();
  }
  return result;
}

uint32_t
Circuit::GetPackageWindow ()
{
  return package_window;
}

void
Circuit::IncPackageWindow ()
{
  package_window += m_windowIncrement;
  if (package_window > m_windowStart)
    {
      package_window = m_windowStart;
    }
}

uint32_t
Circuit::GetDeliverWindow ()
{
  return deliver_window;
}

void
Circuit::IncDeliverWindow ()
{
  deliver_window += m_windowIncrement;
  if (deliver_window > m_windowStart)
    {
      deliver_window = m_windowStart;
    }
}

uint32_t
Circuit::SendCell (CellDirection direction)
{
  queue<Ptr<Packet> >* cellQ = GetQueue (direction);
  if (cellQ->size () <= 0)
    {
      return 0;
    }

  Ptr<Connection> conn = GetConnection (direction);
  if (conn->IsBlocked () || conn->GetSocket ()->GetTxAvailable () < CELL_NETWORK_SIZE)
    {
      return 0;
    }

  Ptr<Packet> cell = PopCell (direction);
  return conn->GetSocket ()->Send (cell);
}








Connection::Connection (TorApp* torapp, Ipv4Address ip, int conntype)
{
  this->torapp = torapp;
  this->remote = ip;
  this->inbuf.size = 0;
  this->outbuf.size = 0;
  this->reading_blocked = 0;
  this->active_circuits = 0;

  m_socket = 0;
  m_conntype = conntype;
}


Connection::~Connection ()
{
  NS_LOG_FUNCTION (this);
}

Ptr<Circuit>
Connection::GetActiveCircuits ()
{
  return active_circuits;
}

void
Connection::SetActiveCircuits (Ptr<Circuit> circ)
{
  active_circuits = circ;
}


uint8_t
Connection::GetType ()
{
  return m_conntype;
}

bool
Connection::SpeaksCells ()
{
  return m_conntype == RELAYEDGE;
}

bool
Connection::IsBlocked ()
{
  return reading_blocked;
}

void
Connection::SetBlocked (bool b)
{
  reading_blocked = b;
}


Ptr<Socket>
Connection::GetSocket ()
{
  return m_socket;
}

void
Connection::SetSocket (Ptr<Socket> socket)
{
  m_socket = socket;
}

Ipv4Address
Connection::GetRemote ()
{
  return remote;
}

map<Ipv4Address,string> Connection::remote_names;

void
Connection::RememberName (Ipv4Address address, string name)
{
  Connection::remote_names[address] = name;
}

string
Connection::GetRemoteName ()
{
  if (Ipv4Mask ("255.0.0.0").IsMatch (GetRemote (), Ipv4Address ("127.0.0.1")) )
    return "pseudo";

  map<Ipv4Address,string>::const_iterator it = Connection::remote_names.find(GetRemote ());
  NS_ASSERT(it != Connection::remote_names.end() );
  return it->second;
}



uint32_t
Connection::Read (vector<Ptr<Packet> >* packet_list, uint32_t max_read)
{
  if (reading_blocked)
    {
      NS_LOG_LOGIC ("[" << torapp->GetNodeName() << ": Connection " << GetRemoteName () << "] Reading nothing: blocked");
      return 0;
    }

  uint8_t raw_data[max_read + this->inbuf.size];
  memcpy (raw_data, this->inbuf.data, this->inbuf.size);
  uint32_t tmp_available = m_socket->GetRxAvailable ();
  int read_bytes = m_socket->Recv (&raw_data[this->inbuf.size], max_read, 0);

  uint32_t base = SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t datasize = read_bytes + inbuf.size;
  uint32_t leftover = datasize % base;
  int num_packages = datasize / base;

  NS_LOG_LOGIC ("[" << torapp->GetNodeName() << ": Connection " << GetRemoteName () << "] " <<
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

void
Connection::CountFinalReception(int circid, uint32_t length)
{
  if (!SpeaksCells() && DynamicCast<PseudoClientSocket>(GetSocket()))
  {
    // notify about bytes leaving the network
    torapp->m_triggerBytesLeftNetwork(torapp, circid, data_index_last_delivered[circid] + 1, data_index_last_delivered[circid] + (uint64_t)length);
    data_index_last_delivered[circid] += (uint64_t) length;
  }
}


uint32_t
Connection::Write (uint32_t max_write)
{
  uint32_t base = SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint8_t raw_data[outbuf.size + (max_write / base + 1) * base];
  memcpy (raw_data, outbuf.data, outbuf.size);
  uint32_t datasize = outbuf.size;
  int written_bytes = 0;

  // fill raw_data
  bool flushed_some = false;
  Ptr<Circuit> start_circ = GetActiveCircuits ();
  NS_ASSERT (start_circ);
  Ptr<Circuit> circ;
  Ptr<Packet> cell = Ptr<Packet> (NULL);
  CellDirection direction;

  NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": Connection " << GetRemoteName () << "] Trying to write cells from circuit queues");

  while (datasize < max_write)
    {
      circ = GetActiveCircuits ();
      NS_ASSERT (circ);

      direction = circ->GetDirection (this);
      cell = circ->PopCell (direction);
      int circid = circ->GetId();

      if (cell)
        {
          uint32_t cell_size = cell->CopyData (&raw_data[datasize], cell->GetSize ());

          // check if just packaged
          auto opp_con = circ->GetOppositeConnection(direction);
          if (!opp_con->SpeaksCells() && DynamicCast<PseudoServerSocket>(opp_con->GetSocket()))
          {
            // notify about bytes entering the network
            torapp->m_triggerBytesEnteredNetwork(torapp, circid, data_index_last_seen[circid] + 1, data_index_last_seen[circid] + cell_size);
          }

          datasize += cell_size;

          flushed_some = true;

          data_index_last_seen[circid] += cell_size;
          NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": Connection " << GetRemoteName () << "] Actually sending one cell from circuit " << circ->GetId ());
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
    NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": Connection " << GetRemoteName () << "] " << leftover << " bytes left over for next conn write");

  return written_bytes;
}


void
Connection::ScheduleWrite (Time delay)
{
  if (m_socket && write_event.IsExpired ())
    {
      write_event = Simulator::Schedule (delay, &TorApp::ConnWriteCallback, torapp, m_socket, m_socket->GetTxAvailable ());
    }
}

void
Connection::ScheduleRead (Time delay)
{
  if (m_socket && read_event.IsExpired ())
    {
      read_event = Simulator::Schedule (delay, &TorApp::ConnReadCallback, torapp, m_socket);
    }
}


uint32_t
Connection::GetOutbufSize ()
{
  return outbuf.size;
}

uint32_t
Connection::GetInbufSize ()
{
  return inbuf.size;
}

} //namespace ns3
