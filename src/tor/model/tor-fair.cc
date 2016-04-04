#include "tor-fair.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorFairApp");
NS_OBJECT_ENSURE_REGISTERED (TorFairApp);


TypeId
TorFairApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorFairApp")
    .SetParent<TorApp> ()
    .AddConstructor<TorFairApp> ();
  return tid;
}


void
TorFairApp::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                    Ptr<PseudoClientSocket> clientSocket)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype);

  // ensure unique id
  NS_ASSERT (circuits[id] == 0);

  // allocate and init new circuit
  Ptr<Connection> p_conn = AddConnection (p_ip, p_conntype);
  Ptr<Connection> n_conn = AddConnection (n_ip, n_conntype);
  p_conn->SetSocket (clientSocket);

  Ptr<Circuit> circ = Create<Circuit> (id, n_conn, p_conn, m_windowStart, m_windowIncrement);

  // add to circuit list maintained by every connection
  AddActiveCircuit (p_conn, circ);
  AddActiveCircuit (n_conn, circ);

  m_circuitRing.AddCircuit(circ);

  // add to the global list of circuits
  circuits[id] = circ;
  baseCircuits[id] = circ;
}



void
TorFairApp::ConnWriteCallback (Ptr<Socket> socket, uint32_t tx)
{
  NS_ASSERT (socket);
  Ptr<Connection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  int written_bytes = 0;
  uint32_t max_write = m_writebucket.GetSize();

  NS_LOG_LOGIC ("Write max " << max_write);

  if (max_write <= 0)
    {
      return;
    }

  written_bytes = m_circuitRing.Write(max_write);
  NS_LOG_LOGIC (written_bytes << " bytes written");

  if (written_bytes > 0)
    {
      GlobalBucketsDecrement (0, written_bytes);

      /* try flushing more */
      conn->ScheduleWrite ();
    }
}


void
TorFairApp::ConnReadCallback (Ptr<Socket> socket)
{
  NS_ASSERT (socket);
  Ptr<Connection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  if (conn->IsBlocked ())
    {
      NS_LOG_LOGIC ("Reading blocked, return");
      return;
    }

  uint32_t base = conn->SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t max_read = socket->GetRxAvailable();

  // find the minimum amount of data to read safely from the socket
  // max_read = min (max_read, socket->GetRxAvailable ());
  NS_LOG_LOGIC ("Read " << max_read << "/" << socket->GetRxAvailable () << " bytes from " << conn->GetRemote ());

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

  for (uint32_t i = 0; i < packet_list.size (); i++)
    {
      if (conn->SpeaksCells ())
        {
          ReceiveRelayCell (conn, packet_list[i]);
        }
      else
        {
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


CircuitRing::CircuitRing() {
  active_circuit = 0;
}


CircuitRing::~CircuitRing() {

}


void
CircuitRing::AddCircuit(Ptr<Circuit> circ) {
  this->circuits.push_back(circ);
}


uint32_t
CircuitRing::Write(uint32_t max_write) {
  bool wrote_something = false;
  int start_index = active_circuit;
  int index;
  uint32_t bytes_written = 0;
  uint32_t temp_written;
  Ptr<Circuit> circ;

  while(bytes_written < max_write) {
    index = active_circuit;
    circ = circuits[index];

    temp_written = 0;
    temp_written += circ->SendCell(INBOUND);
    temp_written += circ->SendCell(OUTBOUND);
    if(temp_written > 0) {
      bytes_written += temp_written;
      wrote_something = true;
    }

    index = (index + 1) % circuits.size();
    active_circuit = index;

    if(index == start_index) {
      if(wrote_something) {
        wrote_something = false;
      } else {
        break;
      }
    }

  }

  return bytes_written;
}


} // namespace ns3