#define NS_LOG_APPEND_CONTEXT std::clog << Simulator::Now ().GetSeconds () << " ";

#include "ns3/log.h"
#include "ns3/random-variable-stream.h"

#include "tor.h"

using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorApp");
NS_OBJECT_ENSURE_REGISTERED (TorApp);

TypeId
TorApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorApp")
    .SetParent<TorBaseApp> ()
    .AddConstructor<TorApp> ();
  return tid;
}

TorApp::TorApp (void)
{
  listen_socket = 0;
}

TorApp::~TorApp (void){
  NS_LOG_FUNCTION(this);
}

void
TorApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  listen_socket = 0;

  std::map<uint16_t,Ptr<Circuit> >::iterator i;
  for (i = circuits.begin(); i != circuits.end(); ++i)
  {
    i->second->DoDispose();
  }
  circuits.clear();
  connections.clear();
  Application::DoDispose ();
}

void
TorApp::AddCircuit (int circ_id, Ipv4Address n_ip, int n_conn_type, Ipv4Address p_ip, int p_conn_type)
{
  NS_LOG_FUNCTION(circ_id << p_ip << n_ip);

  // ensure unique circ_i
  NS_ASSERT (circuits[circ_id] == 0);

  // ensure valid connection types
  NS_ASSERT (n_conn_type == OR_CONN || n_conn_type == EDGE_CONN);
  NS_ASSERT (p_conn_type == OR_CONN || p_conn_type == EDGE_CONN);

  // allocate and init new circuit
  Ptr<Connection> p_conn = AddConnection(p_ip, p_conn_type);
  Ptr<Connection> n_conn = AddConnection(n_ip, n_conn_type);
  Ptr<Circuit> circ = Create<Circuit> (circ_id, n_conn, p_conn);

  // add to circuit list maintained by every connection
  AddActiveCircuit (p_conn, circ);
  AddActiveCircuit (n_conn, circ);

  // add to the global list of circuits
  circuits[circ_id] = circ;
}

void
TorApp::AddCircuit (int circ_id, Ipv4Address n_ip, int n_conn_type, Ipv4Address p_ip, int p_conn_type,
      Ptr<RandomVariableStream> rng_request, Ptr<RandomVariableStream> rng_think)
{
  NS_LOG_FUNCTION(circ_id << p_ip << n_ip);

  // ensure unique circ_id
  NS_ASSERT (circuits[circ_id] == 0);

  // ensure valid connection types
  NS_ASSERT (n_conn_type == OR_CONN || n_conn_type == EDGE_CONN);
  NS_ASSERT (p_conn_type == OR_CONN || p_conn_type == EDGE_CONN);

  // allocate and init new circuit
  Ptr<Connection> p_conn = AddConnection(p_ip, p_conn_type);
  Ptr<Connection> n_conn = AddConnection(n_ip, n_conn_type);
  p_conn->SetRandomVariableStreams(rng_request, rng_think);

  Ptr<Circuit> circ = Create<Circuit> (circ_id, n_conn, p_conn);

  // add to circuit list maintained by every connection
  AddActiveCircuit (p_conn, circ);
  AddActiveCircuit (n_conn, circ);

  // add to the global list of circuits
  circuits[circ_id] = circ;
}

Ptr<Connection>
TorApp::AddConnection (Ipv4Address ip, int conn_type)
{
  // ensure valid connecton type
  NS_ASSERT (conn_type == OR_CONN || conn_type == EDGE_CONN);

  // find existing or create new connection
  Ptr<Connection> conn;
  std::vector<Ptr<Connection> >::iterator it;
  for (it = connections.begin(); it != connections.end(); ++it) {
    if ((*it)->get_remote() == ip) {
      conn = *it;
      break;
    }
  }

  if (!conn)
    {
      conn = Create<Connection> (this, ip, conn_type);
      connections.push_back(conn);
    }

  return conn;
}

void
TorApp::AddActiveCircuit (Ptr<Connection> conn, Ptr<Circuit> circ)
{
  NS_ASSERT (conn);
  NS_ASSERT (circ);
  if (conn) {
    if (!conn->get_active_circuits()) {
      conn->set_active_circuits(circ);
      circ->set_next_circ(conn, circ);
    } else {
      Ptr<Circuit> temp = conn->get_active_circuits()->get_next_circ(conn);
      circ->set_next_circ(conn, temp);
      conn->get_active_circuits()->set_next_circ(conn, circ);
    }
  }
}

void
TorApp::StartApplication (void)
{
  TorBaseApp::StartApplication();
  m_readbucket.SetRefilledCallback(MakeCallback (&TorApp::refill_read_callback, this));
  m_writebucket.SetRefilledCallback(MakeCallback (&TorApp::refill_write_callback, this));

  // create listen socket
  if (!listen_socket)
    {
      listen_socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
      listen_socket->Bind (m_local);
      listen_socket->Listen ();
    }

  listen_socket->SetAcceptCallback (MakeNullCallback<bool,Ptr<Socket>,const Address &> (),
    MakeCallback (&TorApp::HandleAccept,this));

  Ipv4Mask ipmask = Ipv4Mask("255.0.0.0");

  // iterate over all neighboring connections
  std::vector<Ptr<Connection> >::iterator it;
  for ( it = connections.begin (); it != connections.end (); it++ )
    {
      Ptr<Connection> conn = *it;
      NS_ASSERT (conn);

      // if m_ip smaller then connect to remote node
      if (m_ip < conn->get_remote()  && conn->get_type() == OR_CONN)
        {
          Ptr<Socket> socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
          socket->Bind ();
          socket->Connect (Address (InetSocketAddress (conn->get_remote(), InetSocketAddress::ConvertFrom (m_local).GetPort ())));
		      // socket->SetSendCallback (MakeCallback(&TorApp::conn_write_callback, this));
          socket->SetDataSentCallback (MakeCallback (&TorApp::conn_write_callback, this));
          socket->SetRecvCallback (MakeCallback (&TorApp::conn_read_callback, this));
          conn->set_socket(socket);
        }

      if (conn->get_type() == EDGE_CONN && ipmask.IsMatch(conn->get_remote(), Ipv4Address("127.0.0.1")) ) {
        if (conn->get_active_circuits()->get_direction(conn) == OUTBOUND) {
          // EDGE_CONN exit2server
          Ptr<Socket> socket = CreateObject<PseudoServerSocket> ();
          socket->SetDataSentCallback(MakeCallback(&TorApp::conn_write_callback, this));
          // socket->SetSendCallback(MakeCallback(&TorApp::conn_write_callback, this));
          socket->SetRecvCallback(MakeCallback(&TorApp::conn_read_callback, this));
          conn->set_socket(socket);
        } else {
          // EDGE_CONN proxy2client
          Ptr<PseudoClientSocket> socket = CreateObject<PseudoClientSocket> ();
          if(conn->GetRequestStream() && conn->GetThinkStream()) {
            socket->SetRequestStream(conn->GetRequestStream());
            socket->SetThinkStream(conn->GetThinkStream());
          }
          socket->SetDataSentCallback(MakeCallback(&TorApp::conn_write_callback, this));
          // socket->SetSendCallback(MakeCallback(&TorApp::conn_write_callback, this));
          socket->SetRecvCallback(MakeCallback(&TorApp::conn_read_callback, this));
          conn->set_socket(socket);
          conn->RegisterCallbacks();
          Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
          conn->schedule_read(Seconds(rng->GetValue(0.1,1.0)));
        }
      }
    }

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
  std::vector<Ptr<Connection> >::iterator it_conn;
  for ( it_conn = connections.begin (); it_conn != connections.end (); ++it_conn )
    {
      Ptr<Connection> conn = *it_conn;
      NS_ASSERT (conn);
      if (conn->get_socket())
        {
          conn->get_socket()->Close ();
          conn->get_socket()->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
          conn->get_socket()->SetDataSentCallback (MakeNullCallback<void, Ptr<Socket>, uint32_t > ());
          // conn->get_socket()->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t > ());
        }
    }
}

Ptr<Circuit>
TorApp::GetCircuit(uint32_t circid) {
  return circuits[circid];
}


void
TorApp::conn_read_callback (Ptr<Socket> socket)
{
  NS_ASSERT (socket);
  Ptr<Connection> conn = lookup_conn (socket);
  NS_ASSERT (conn);

  if (conn->is_blocked())
    {
      NS_LOG_LOGIC("Reading blocked, return");
      return;
    }

  uint32_t base = conn->get_type() == EDGE_CONN ? CELL_PAYLOAD_SIZE : CELL_NETWORK_SIZE;
  uint32_t max_read = round_robin (base, m_readbucket.GetSize());

  // find the minimum amount of data to read safely from the socket
  max_read = std::min (max_read, socket->GetRxAvailable ());
  NS_LOG_LOGIC("Read " << max_read << "/" << socket->GetRxAvailable() << " bytes from " << conn->get_remote());

  if (max_read <= 0)
    {
      return;
    }

  if (conn->get_type() == EDGE_CONN) {
    max_read = std::min(conn->get_active_circuits()->get_package_window()*base,max_read);
  }

  std::vector<Ptr<Packet> > packet_list;
  uint32_t read_bytes = conn->read(&packet_list, max_read);

  for(uint32_t i=0; i<packet_list.size(); i++) {
    if(conn->get_type() == EDGE_CONN) {
      package_relay_cell(conn, packet_list[i]);
    } else {
      receive_relay_cell(conn, packet_list[i]);
    }
  }

  if (read_bytes > 0)
    {
      // decrement buckets
      global_buckets_decrement (read_bytes, 0);

      // try to read more
      if (socket->GetRxAvailable () > 0)
        {
          // add some virtual processing time before reading more
          Time delay = Time::FromInteger (read_bytes * 2, Time::NS);
          conn->schedule_read(delay);
        }
    }
}

void
TorApp::package_relay_cell (Ptr<Connection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<Circuit> circ = conn->get_active_circuits();
  NS_ASSERT (circ);

  package_relay_cell_impl (circ->get_id(), cell);

  CellDirection direction = circ->get_opposite_direction(conn);
  append_cell_to_circuit_queue (circ, cell, direction);
  if (circ->get_package_window() <= 0)
    {
      NS_LOG_LOGIC("[Circuit " << circ->get_id() << "] Package window empty. Block reading from " << conn->get_remote());
      conn->set_blocked(true);
    }
}

void
TorApp::package_relay_cell_impl (int circ_id, Ptr<Packet> cell)
{
  NS_ASSERT(cell);
  CellHeader h;
  h.SetCircId (circ_id);
  h.SetCmd (RELAY_DATA);
  h.SetType (RELAY);
  h.SetLength (cell->GetSize ());
  cell->AddHeader (h);
}

void
TorApp::receive_relay_cell (Ptr<Connection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<Circuit> circ = lookup_circuit_from_cell (cell);
  NS_ASSERT (circ);

  // find target connection for relaying
  CellDirection direction = circ->get_opposite_direction(conn);
  Ptr<Connection> target_conn = circ->get_connection(direction);
  NS_ASSERT (target_conn);

  append_cell_to_circuit_queue(circ, cell, direction);
}


Ptr<Circuit>
TorApp::lookup_circuit_from_cell (Ptr<Packet> cell)
{
  NS_ASSERT(cell);
  CellHeader h;
  cell->PeekHeader (h);
  return circuits[h.GetCircId ()];
}


/* Add cell to the queue of circ writing to orconn transmitting in direction. */
void
TorApp::append_cell_to_circuit_queue (Ptr<Circuit> circ, Ptr<Packet> cell, CellDirection direction)
{
  NS_ASSERT (circ);
  NS_ASSERT (cell);
  std::queue<Ptr<Packet> > *queue = circ->get_queue(direction);
  Ptr<Connection> conn = circ->get_connection(direction);
  NS_ASSERT (queue);
  NS_ASSERT (conn);

  circ->push_cell(cell, direction);

  NS_LOG_LOGIC("[Circuit " << circ->get_id() << "] Appended cell. Queue holds " << queue->size() << " cells.");
  conn->schedule_write();
}


void
TorApp::conn_write_callback (Ptr<Socket> socket, uint32_t tx)
{
  NS_ASSERT (socket);
  Ptr<Connection> conn = lookup_conn (socket);
  NS_ASSERT (conn);

  uint32_t newtx = socket->GetTxAvailable ();

  int written_bytes = 0;
  uint16_t base = conn->get_type() == EDGE_CONN ? CELL_PAYLOAD_SIZE : CELL_NETWORK_SIZE;
  uint32_t max_write = round_robin (base, m_writebucket.GetSize());
  max_write = max_write > newtx ? newtx : max_write;

  NS_LOG_LOGIC("Write max " << max_write << " bytes to " << conn->get_remote());

  if (max_write <= 0)
    {
      return;
    }

  written_bytes = conn->write(max_write);
  NS_LOG_LOGIC(written_bytes << " bytes written to " << conn->get_remote());

  if (written_bytes > 0)
    {
      global_buckets_decrement (0, written_bytes);

      /* try flushing more */
      conn->schedule_write();
    }
}



void
TorApp::HandleAccept (Ptr<Socket> s, const Address& from)
{
  Ptr<Connection> conn;
  Ipv4Address ip = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
  std::vector<Ptr<Connection> >::iterator it;
  for (it = connections.begin(); it != connections.end(); ++it) {
    if ((*it)->get_remote() == ip && !(*it)->get_socket() ) {
      conn = *it;
      break;
    }
  }

  NS_ASSERT (conn);
  conn->set_socket(s);

  s->SetRecvCallback (MakeCallback (&TorApp::conn_read_callback, this));
  // s->SetSendCallback (MakeCallback(&TorApp::conn_write_callback, this));
  s->SetDataSentCallback (MakeCallback (&TorApp::conn_write_callback, this));
}



Ptr<Connection>
TorApp::lookup_conn (Ptr<Socket> socket)
{
  std::vector<Ptr<Connection> >::iterator it;
  for ( it = connections.begin (); it != connections.end (); it++ )
    {
      NS_ASSERT (*it);
      if ((*it)->get_socket() == socket)
        {
          return (*it);
        }
    }
  return NULL;
}


void
TorApp::refill_read_callback (int64_t prev_read_bucket)
{
  NS_LOG_LOGIC ("read bucket was " << prev_read_bucket << ". Now " << m_readbucket.GetSize());
  Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
  if (prev_read_bucket <= 0 && m_readbucket.GetSize() > 0)
    {
      std::vector<Ptr<Connection> >::iterator it;
      for ( it = connections.begin (); it != connections.end (); ++it )
        {
          Ptr<Connection> conn = *it;
          NS_ASSERT (conn);
          conn->schedule_read(Time("10ns"));
        }
    }
}

void
TorApp::refill_write_callback (int64_t prev_write_bucket)
{
  NS_LOG_LOGIC ("write bucket was " << prev_write_bucket << ". Now " << m_writebucket.GetSize());

  if (prev_write_bucket <= 0 && m_writebucket.GetSize() > 0)
    {
      std::vector<Ptr<Connection> >::iterator it;
      for ( it = connections.begin (); it != connections.end (); ++it )
        {
          Ptr<Connection> conn = *it;
          NS_ASSERT (conn);
          conn->schedule_write();
        }
    }
}


/** We just read num_read and wrote num_written bytes
 * onto conn. Decrement buckets appropriately. */
void
TorApp::global_buckets_decrement (uint32_t num_read, uint32_t num_written)
{
  m_readbucket.Decrement(num_read);
  m_writebucket.Decrement(num_written);
}



/** Helper function to decide how many bytes out of global_bucket
 * we're willing to use for this transaction. Yes, this is how Tor
 * implements it; no kidding. */
uint32_t
TorApp::round_robin (int base, int64_t global_bucket)
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



Circuit::Circuit(uint32_t circ_id, Ptr<Connection> n_conn, Ptr<Connection> p_conn) {
  this->circ_id = circ_id;

  this->p_cellQ = new std::queue<Ptr<Packet> >;
  this->n_cellQ = new std::queue<Ptr<Packet> >;

  this->deliver_window = CIRCWINDOW_START;
  this->package_window = CIRCWINDOW_START;

  this->stats_p_bytes_read = 0;
  this->stats_p_bytes_written = 0;
  this->stats_n_bytes_read = 0;
  this->stats_n_bytes_written = 0;

  this->p_conn = p_conn;
  this->n_conn = n_conn;

  this->next_active_on_n_conn = 0;
  this->next_active_on_p_conn = 0;
}


Circuit::~Circuit() {
  NS_LOG_FUNCTION(this);
  delete this->p_cellQ;
  delete this->n_cellQ;
}

void
Circuit::DoDispose() {
    this->next_active_on_p_conn = 0;
    this->next_active_on_n_conn = 0;
    this->p_conn->set_active_circuits(0);
    this->n_conn->set_active_circuits(0);
}


Ptr<Packet>
Circuit::pop_queue(std::queue<Ptr<Packet> > *queue) {
  if (queue->size () > 0)
    {
      Ptr<Packet> cell = queue->front ();
      queue->pop ();

      return cell;
    }
  return 0;
}


Ptr<Packet>
Circuit::pop_cell(CellDirection direction) {
  Ptr<Packet> cell;
  if (direction == OUTBOUND) {
    cell = this->pop_queue(this->n_cellQ);
  } else {
    cell = this->pop_queue(this->p_cellQ);
  }

  if (cell) {
    if(!is_relay_sendme(cell)) {
      inc_stats_bytes(direction, 0, CELL_PAYLOAD_SIZE);
    }

    /* handle sending sendme cells here (instead of in push_cell) because
     * otherwise short circuits could have more than a window-ful of cells
     * in-flight. Regular circuits will not be affected by this. */
    if (get_connection(direction)->get_type() == EDGE_CONN){
      deliver_window--;
      if(deliver_window <= CIRCWINDOW_START - CIRCWINDOW_INCREMENT) {
        inc_deliver_window();
        NS_LOG_LOGIC("[Circuit " << circ_id << "] Send SENDME cell ");
        Ptr<Packet> sendme_cell = create_sendme_cell();
        get_queue(get_opposite_direction(direction))->push(sendme_cell);
        get_opposite_connection(direction)->schedule_write();
      }
    }
  }

  return cell;
}


void
Circuit::push_cell(Ptr<Packet> cell, CellDirection direction) {
  if (cell) {
    Ptr<Connection> conn = get_connection(direction);
    Ptr<Connection> opp_conn = get_opposite_connection(direction);

    if(opp_conn->get_type() == EDGE_CONN) {
    // new packaged cell
      package_window--;
      if(package_window <= 0) {
        //block connection
        opp_conn->set_blocked(true);
      }
    }

    if(conn->get_type() == EDGE_CONN) {
    // delivery
      if (is_relay_sendme(cell)) {
      // update package window
        inc_package_window();
        NS_LOG_LOGIC("[Circuit " << circ_id << "] Received SENDME cell. Package window now " << package_window);
        if(conn->is_blocked()) {
          conn->set_blocked(false);
          conn->schedule_read();
        }

        // no stats and no cell push on sendme cells
        return;
      }

      CellHeader h;
      cell->RemoveHeader(h);
    }

    inc_stats_bytes(direction, CELL_PAYLOAD_SIZE, 0);
    get_queue(direction)->push(cell);
  }
}




uint32_t
Circuit::get_id() {
  return circ_id;
}


Ptr<Connection>
Circuit::get_connection(CellDirection direction) {
  if(direction == OUTBOUND) {
    return this->n_conn;
  } else {
    return this->p_conn;
  }
}

Ptr<Connection>
Circuit::get_opposite_connection(CellDirection direction) {
  if(direction == OUTBOUND) {
    return this->p_conn;
  } else {
    return this->n_conn;
  }
}

Ptr<Connection>
Circuit::get_opposite_connection(Ptr<Connection> conn) {
  if(this->n_conn == conn) {
    return this->p_conn;
  } else if(this->p_conn == conn) {
    return this->n_conn;
  } else {
    return 0;
  }
}



CellDirection
Circuit::get_direction(Ptr<Connection> conn) {
  if(this->n_conn == conn) {
    return OUTBOUND;
  } else {
    return INBOUND;
  }
}

CellDirection
Circuit::get_opposite_direction(Ptr<Connection> conn) {
  if(this->n_conn == conn) {
    return INBOUND;
  } else {
    return OUTBOUND;
  }
}

CellDirection
Circuit::get_opposite_direction(CellDirection direction) {
  if (direction == OUTBOUND) {
    return INBOUND;
  } else {
    return OUTBOUND;
  }
}


Ptr<Circuit>
Circuit::get_next_circ(Ptr<Connection> conn) {
  NS_ASSERT (this->n_conn);
  if(this->n_conn == conn) {
    return next_active_on_n_conn;
  } else {
    return next_active_on_p_conn;
  }
}


void
Circuit::set_next_circ(Ptr<Connection> conn, Ptr<Circuit> circ) {
  if(this->n_conn == conn) {
    next_active_on_n_conn = circ;
  } else {
    next_active_on_p_conn = circ;
  }
}


bool
Circuit::is_relay_sendme(Ptr<Packet> cell) {
  if (!cell) {
      return false;
  }
  CellHeader h;
  cell->PeekHeader(h);
  if (h.GetCmd () == RELAY_SENDME) {
      return true;
  }
  return false;
}

Ptr<Packet>
Circuit::create_sendme_cell() {
  CellHeader h;
  h.SetCircId(get_id());
  h.SetType(RELAY);
  h.SetStreamId(42);
  h.SetCmd(RELAY_SENDME);
  h.SetLength(0);
  Ptr<Packet> cell = Create<Packet> (CELL_PAYLOAD_SIZE);
  cell->AddHeader(h);

  return cell;
}


std::queue<Ptr<Packet> >*
Circuit::get_queue(CellDirection direction) {
  if (direction == OUTBOUND) {
    return this->n_cellQ;
  } else {
    return this->p_cellQ;
  }
}


uint32_t
Circuit::get_queue_size(CellDirection direction) {
  if(direction == OUTBOUND) {
    return this->n_cellQ->size();
  } else {
    return this->p_cellQ->size();
  }
}


uint32_t
Circuit::get_stats_bytes_read(CellDirection direction) {
    if (direction == OUTBOUND) {
      return stats_n_bytes_read;
    } else {
      return stats_p_bytes_read;
    }
}

uint32_t
Circuit::get_stats_bytes_written(CellDirection direction) {
    if (direction == OUTBOUND) {
      return stats_n_bytes_written;
    } else {
      return stats_p_bytes_written;
    }
}

void
Circuit::reset_stats_bytes() {
  stats_p_bytes_read = 0;
  stats_n_bytes_read = 0;
  stats_p_bytes_written = 0;
  stats_n_bytes_written = 0;
}

void
Circuit::inc_stats_bytes(CellDirection direction, uint32_t read, uint32_t write) {
    if (direction == OUTBOUND) {
      stats_n_bytes_read += read;
      stats_n_bytes_written += write;
    } else {
      stats_p_bytes_read += read;
      stats_p_bytes_written += write;
    }
}

uint32_t
Circuit::get_package_window() {
  return package_window;
}

void
Circuit::inc_package_window() {
  package_window += CIRCWINDOW_INCREMENT;
  if (package_window > CIRCWINDOW_START){
    package_window = CIRCWINDOW_START;
  }
}

uint32_t
Circuit::get_deliver_window() {
  return deliver_window;
}

void
Circuit::inc_deliver_window() {
  deliver_window += CIRCWINDOW_INCREMENT;
  if (deliver_window > CIRCWINDOW_START) {
    deliver_window = CIRCWINDOW_START;
  }
}

uint32_t
Circuit::send_cell(CellDirection direction) {
  std::queue<Ptr<Packet> >* cellQ = get_queue(direction);
  if(cellQ->size() <= 0) {
    return 0;
  }

  Ptr<Connection> conn = get_connection(direction);
  if(conn->is_blocked() || conn->get_socket()->GetTxAvailable() < CELL_NETWORK_SIZE) {
    return 0;
  }

  Ptr<Packet> cell = pop_cell(direction);
  return conn->get_socket()->Send(cell);
}








Connection::Connection(TorApp* torapp, Ipv4Address ip, int conn_type) {
  this->torapp = torapp;
  this->remote = ip;
  this->socket = 0;
  this->inbuf.size = 0;
  this->outbuf.size = 0;
  this->conn_type = conn_type;
  this->reading_blocked = 0;
  this->active_circuits = 0;
  this->m_rng_request = 0;
  this->m_rng_think = 0;

  this->m_ttfb_id = -1;
  this->m_ttlb_id = -1;
  this->m_ttfb_callback = 0;
  this->m_ttlb_callback = 0;
}


Connection::~Connection() {
  NS_LOG_FUNCTION(this);
}

Ptr<Circuit>
Connection::get_active_circuits() {
  return this->active_circuits;
}

void
Connection::set_active_circuits(Ptr<Circuit> circ) {
  this->active_circuits = circ;
}


uint8_t
Connection::get_type() {
  return this->conn_type;
}

bool
Connection::is_blocked() {
  return this->reading_blocked;
}

void
Connection::set_blocked(bool b) {
  this->reading_blocked = b;
}


Ptr<Socket>
Connection::get_socket() {
  return this->socket;
}

void
Connection::set_socket(Ptr<Socket> socket) {
  this->socket = socket;
}

Ipv4Address
Connection::get_remote() {
  return this->remote;
}




uint32_t
Connection::read(std::vector<Ptr<Packet> >* packet_list, uint32_t max_read) {
  if(reading_blocked) {
    return 0;
  }

  uint8_t raw_data[max_read + this->inbuf.size];
  memcpy (raw_data, this->inbuf.data, this->inbuf.size);
  int read_bytes = socket->Recv(&raw_data[this->inbuf.size], max_read, 0);

  uint32_t base = conn_type == EDGE_CONN ? CELL_PAYLOAD_SIZE : CELL_NETWORK_SIZE;
  uint32_t datasize = read_bytes + inbuf.size;
  uint32_t leftover = datasize % base;
  int num_packages = datasize / base;

  // slice data into packets
  Ptr<Packet> cell;
  for(int i = 0; i < num_packages; i++) {
    cell = Create<Packet> (&raw_data[i * base], base);
    packet_list->push_back(cell);
  }

  //safe leftover
  memcpy (inbuf.data, &raw_data[datasize - leftover], leftover);
  inbuf.size = leftover;

  return read_bytes;
}


uint32_t
Connection::write(uint32_t max_write) {
  uint32_t base = this->conn_type == EDGE_CONN ? CELL_PAYLOAD_SIZE : CELL_NETWORK_SIZE;
  uint8_t raw_data[outbuf.size + (max_write / base + 1) * base];
  memcpy (raw_data, outbuf.data, outbuf.size);
  uint32_t datasize = outbuf.size;
  int written_bytes = 0;

  // fill raw_data
  bool flushed_some = false;
  Ptr<Circuit> start_circ = get_active_circuits();
  NS_ASSERT (start_circ);
  Ptr<Circuit> circ;
  Ptr<Packet> cell = Ptr<Packet> (NULL);
  CellDirection direction;

  while (datasize < max_write) {
    circ = get_active_circuits();
    NS_ASSERT (circ);

    direction = circ->get_direction(this);
    cell = circ->pop_cell(direction);

    if (cell) {
      datasize += cell->CopyData (&raw_data[datasize], cell->GetSize ());
      flushed_some = true;
    }

    NS_ASSERT (circ->get_next_circ(this));
    this->set_active_circuits(circ->get_next_circ(this));

    if (get_active_circuits() == start_circ) {
      if (!flushed_some) {
        break;
      }
      flushed_some = false;
    }
  }

  // send data
  max_write = std::min(max_write, datasize);
  if (max_write > 0) {
    written_bytes = socket->Send (raw_data, max_write, 0);
  }

  /* save leftover for next time */
  written_bytes = std::max(written_bytes,0);
  uint32_t leftover = datasize - written_bytes;
  memcpy (outbuf.data, &raw_data[datasize - leftover], leftover);
  outbuf.size = leftover;

  return written_bytes;
}


void
Connection::schedule_write(Time delay) {
  if(socket && write_event.IsExpired()) {
    write_event = Simulator::Schedule (delay, &TorApp::conn_write_callback, torapp, socket, socket->GetTxAvailable());
  }
}

void
Connection::schedule_read(Time delay) {
  if(socket && read_event.IsExpired()) {
    read_event = Simulator::Schedule(delay, &TorApp::conn_read_callback, torapp, socket);
  }
}


uint32_t
Connection::get_outbuf_size() {
  return outbuf.size;
}

uint32_t
Connection::get_inbuf_size() {
  return inbuf.size;
}

void
Connection::SetRandomVariableStreams(Ptr<RandomVariableStream> rng_request, Ptr<RandomVariableStream> rng_think) {
  m_rng_request = rng_request;
  m_rng_think = rng_think;
}

Ptr<RandomVariableStream>
Connection::GetRequestStream() {
  return m_rng_request;
}

Ptr<RandomVariableStream>
Connection::GetThinkStream() {
  return m_rng_think;
}

void
Connection::SetTtfbCallback(void (*ttfb)(int, double, string), int id, string desc) {
  m_ttfb_id = id;
  m_ttfb_desc = desc;
  m_ttfb_callback = ttfb;
}

void
Connection::SetTtlbCallback(void (*ttlb)(int, double, string), int id, string desc) {
  m_ttlb_id = id;
  m_ttlb_desc = desc;
  m_ttlb_callback = ttlb;
}

void
Connection::RegisterCallbacks() {
  if(conn_type == EDGE_CONN) {
    Ptr<PseudoClientSocket> csock = get_socket()->GetObject<PseudoClientSocket>();
    if (csock) {
      if(m_ttfb_callback) {
        csock->SetTtfbCallback(m_ttfb_callback, m_ttfb_id, m_ttfb_desc);
        csock->SetTtlbCallback(m_ttlb_callback, m_ttlb_id, m_ttlb_desc);
      }
    }
  }
}


} //namespace ns3
