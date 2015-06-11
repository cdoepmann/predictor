
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
                   DataRateValue (DataRate ("5Mbps")),
                   MakeDataRateAccessor (&TorBaseApp::m_rate), MakeDataRateChecker ())
    .AddAttribute ("BandwidthBurst", "The data burst in on state.",
                   DataRateValue (DataRate ("10Mbps")),
                   MakeDataRateAccessor (&TorBaseApp::m_burst), MakeDataRateChecker ())
    .AddAttribute ("Refilltime", "Refill interval of the token bucket.",
                   TimeValue (Time ("100ms")),
                   MakeTimeAccessor (&TorBaseApp::m_refilltime), MakeTimeChecker ());
  return tid;
}


TorBaseApp::TorBaseApp()
{
  NS_LOG_FUNCTION (this);
}

TorBaseApp::~TorBaseApp()
{
  NS_LOG_FUNCTION (this);
}

void TorBaseApp::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  m_id = GetNode ()->GetId ();
  m_ip = GetNode ()->GetObject<Ipv4> ()->GetAddress (1,0).GetLocal ();

  m_readbucket = TokenBucket(m_rate,m_burst,m_refilltime);
  m_writebucket = TokenBucket(m_rate,m_burst,m_refilltime);

  Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();
  rng->SetAttribute ("Min", DoubleValue (0.0));
  rng->SetAttribute ("Max", DoubleValue (m_refilltime.GetSeconds()));
  Time offset = Seconds (rng->GetValue());

  m_readbucket.StartBucket(offset);
  m_writebucket.StartBucket(offset);

  NS_LOG_INFO ("StartApplication " << GetNodeName() << " ip=" << m_ip << " refillOffset=" << offset.GetSeconds ());
}

void TorBaseApp::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
}

void
TorBaseApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
}

void
TorBaseApp::AddCircuit (int id, Ipv4Address n_ip, int n_conn_type, Ipv4Address p_ip, int p_conn_type)
{
  NS_LOG_FUNCTION(this);
}

void
TorBaseApp::AddCircuit (int circ_id, Ipv4Address n_ip, int n_conn_type, Ipv4Address p_ip, int p_conn_type,
      Ptr<RandomVariableStream> rng_request, Ptr<RandomVariableStream> rng_think)
{
  NS_LOG_FUNCTION(this);
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

} //namespace ns3