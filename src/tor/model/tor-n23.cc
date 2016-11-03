#include "tor-n23.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorN23App");
NS_OBJECT_ENSURE_REGISTERED (TorN23App);
NS_OBJECT_ENSURE_REGISTERED (N23Circuit);


TorN23App::TorN23App ()
{
  //
}

TorN23App::~TorN23App ()
{
  //
}

TypeId
TorN23App::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorN23App")
    .SetParent<TorApp> ()
    .AddConstructor<TorN23App> ();
  return tid;
}

void
TorN23App::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                       Ptr<PseudoClientSocket> clientSocket)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype);

  // ensure unique id
  NS_ASSERT (circuits[id] == 0);

  // allocate and init new circuit
  Ptr<Connection> p_conn = AddConnection (p_ip, p_conntype);
  Ptr<Connection> n_conn = AddConnection (n_ip, n_conntype);
  p_conn->SetSocket (clientSocket);

  Ptr<N23Circuit> circ = CreateObject<N23Circuit> (id, n_conn, p_conn, m_windowStart, m_windowIncrement);

  // add to circuit list maintained by every connection
  AddActiveCircuit (p_conn, circ);
  AddActiveCircuit (n_conn, circ);

  // add to the global list of circuits
  circuits[id] = circ;
  baseCircuits[id] = circ;
}



N23Circuit::N23Circuit (uint16_t circ_id, Ptr<Connection> n_conn, Ptr<Connection> p_conn,
                        int windowStart, int windowIncrement) : Circuit (circ_id, n_conn, p_conn, windowStart, windowIncrement)
{
  p_creditBalance = N2 + N3;
  n_creditBalance = N2 + N3;
  p_cellsforwarded = 0;
  n_cellsforwarded = 0;
}

Ptr<Packet>
N23Circuit::PopCell (CellDirection direction)
{
  Ptr<Packet> cell;
  Ptr<Connection> conn = GetConnection (direction);
  Ptr<Connection> opp_conn = GetOppositeConnection (direction);
  bool sendCredit = false;

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
      IncrementStats (direction, 0, CELL_PAYLOAD_SIZE);
      if (direction == OUTBOUND)
        {
          if (N2 <= ++n_cellsforwarded)
            {
              n_cellsforwarded -= N2;
              sendCredit = true;
            }
        }
      else
        {
          if (N2 <= ++p_cellsforwarded)
            {
              p_cellsforwarded -= N2;
              sendCredit = true;
            }
        }
    }

  if (sendCredit && opp_conn->SpeaksCells ())
    {
      NS_LOG_LOGIC ("[Circuit " << GetId () << "] Send CREDIT cell ");
      Ptr<Packet> creditCell = CreateCredit ();
      GetQueue (BaseCircuit::GetOppositeDirection (direction))->push (creditCell);
      opp_conn->ScheduleWrite ();
    }

  return cell;
}


void
N23Circuit::PushCell (Ptr<Packet> cell, CellDirection direction)
{
  if (cell)
    {
      Ptr<Connection> conn = GetConnection (direction);
      Ptr<Connection> opp_conn = GetOppositeConnection (direction);

      if (IsCredit (cell))
        {
          if (IncrementCredit (direction) && conn->IsBlocked ())
            {
              conn->SetBlocked (false);
              conn->ScheduleRead ();
            }
          opp_conn->ScheduleWrite ();
          return;
        }

      if (direction == OUTBOUND)
        {
          if (--n_creditBalance <= 0 && !opp_conn->SpeaksCells ())
            {
              opp_conn->SetBlocked (true);
            }
        }
      else
        {
          if (--p_creditBalance <= 0 && !opp_conn->SpeaksCells ())
            {
              opp_conn->SetBlocked (true);
            }
        }

      if (!conn->SpeaksCells ())
        {
          CellHeader h;
          cell->RemoveHeader (h); // for delivery
        }

      IncrementStats (direction, CELL_PAYLOAD_SIZE, 0);
      GetQueue (direction)->push (cell);
    }
}

bool
N23Circuit::IncrementCredit (CellDirection direction)
{
  bool positive = false;
  if (BaseCircuit::GetOppositeDirection (direction) == OUTBOUND)
    {
      n_creditBalance += N2;
      if (n_creditBalance > N2 + N3)
        {
          n_creditBalance = N2 + N3;
        }
      if (n_creditBalance > 0)
        {
          positive = true;
        }
    }
  else
    {
      p_creditBalance += N2;
      if (p_creditBalance > N2 + N3)
        {
          p_creditBalance = N2 + N3;
        }
      if (p_creditBalance > 0)
        {
          positive = true;
        }
    }
  return positive;
}


Ptr<Packet>
N23Circuit::CreateCredit ()
{
  CellHeader h;
  h.SetCircId (GetId ());
  h.SetType (CREDIT);
  h.SetStreamId (42);
  h.SetCmd (CREDIT);
  h.SetLength (0);
  Ptr<Packet> cell = Create<Packet> (CELL_PAYLOAD_SIZE);
  cell->AddHeader (h);

  return cell;
}

bool
N23Circuit::IsCredit (Ptr<Packet> cell)
{
  if (!cell)
    {
      return false;
    }
  CellHeader h;
  cell->PeekHeader (h);
  if (h.GetCmd () == CREDIT)
    {
      return true;
    }
  return false;
}


} //namespace ns3
