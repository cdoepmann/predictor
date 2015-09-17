
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"

#include "tor-base.h"

using namespace std;
namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorBaseApp");
NS_OBJECT_ENSURE_REGISTERED (TorBaseApp);

TypeId
TorBaseApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorBaseApp")
    .SetParent<Application> ()
    .AddConstructor<TorBaseApp> ()
    .AddAttribute ("Local", "The Address on which to Bind the rx socket.",
                   AddressValue (InetSocketAddress (Ipv4Address::GetAny (), 9001)),
                   MakeAddressAccessor (&TorBaseApp::m_local), MakeAddressChecker ())
    .AddAttribute ("BandwidthRate", "The data rate in on state.",
                   DataRateValue (DataRate ("1GB/s")),
                   MakeDataRateAccessor (&TorBaseApp::m_rate), MakeDataRateChecker ())
    .AddAttribute ("BandwidthBurst", "The data burst in on state.",
                   DataRateValue (DataRate ("1GB/s")),
                   MakeDataRateAccessor (&TorBaseApp::m_burst), MakeDataRateChecker ())
    .AddAttribute ("Refilltime", "Refill interval of the token bucket.",
                   TimeValue (Time ("100ms")),
                   MakeTimeAccessor (&TorBaseApp::m_refilltime), MakeTimeChecker ());
  return tid;
}


TorBaseApp::TorBaseApp ()
{
  NS_LOG_FUNCTION (this);
}

TorBaseApp::~TorBaseApp ()
{
  NS_LOG_FUNCTION (this);
}

void
TorBaseApp::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  m_id = GetNode ()->GetId ();
  m_ip = GetNode ()->GetObject<Ipv4> ()->GetAddress (1,0).GetLocal ();

  m_readbucket = TokenBucket (m_rate,m_burst,m_refilltime);
  m_writebucket = TokenBucket (m_rate,m_burst,m_refilltime);

  Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();
  rng->SetAttribute ("Min", DoubleValue (0.0));
  rng->SetAttribute ("Max", DoubleValue (m_refilltime.GetSeconds ()));
  Time offset = Seconds (rng->GetValue ());

  m_readbucket.StartBucket (offset);
  m_writebucket.StartBucket (offset);

  NS_LOG_INFO ("StartApplication " << GetNodeName () << " ip=" << m_ip << " refillOffset=" << offset.GetSeconds ());
}

void
TorBaseApp::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
}

void
TorBaseApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
}

void
TorBaseApp::AddCircuit (int circ_id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                        Ptr<PseudoClientSocket> clientSocket)
{
  NS_LOG_FUNCTION (this);

  // ensure valid connection types
  NS_ASSERT (n_conntype == RELAYEDGE || n_conntype == PROXYEDGE || n_conntype == SERVEREDGE);
  NS_ASSERT (p_conntype == RELAYEDGE || p_conntype == PROXYEDGE || p_conntype == SERVEREDGE);
}

void
TorBaseApp::SetNodeName (string name)
{
  m_name = name;
}

string
TorBaseApp::GetNodeName (void)
{
  return m_name;
}





BaseCircuit::BaseCircuit ()
{
  NS_LOG_FUNCTION (this);
}

BaseCircuit::BaseCircuit (uint16_t id)
{
  NS_LOG_FUNCTION (this);
  m_id = id;
  ResetStats ();
}

BaseCircuit::~BaseCircuit ()
{
  NS_LOG_FUNCTION (this);
}

uint16_t
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


} //namespace ns3