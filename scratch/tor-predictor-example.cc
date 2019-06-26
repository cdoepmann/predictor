#include <iostream>
#include <fstream>

#include "ns3/tor-module.h"

using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TorPredictorExample");

void StatsCallback(TorStarHelper*, Time);
void remember_leadtime(TorStarHelper*, Time);

bool use_predictor = false;
bool use_vanilla = false;

// void TestTrajectory()
// {
//   Trajectory base{Seconds(1), Seconds(0)};
//   base.Elements().push_back(3);
//   base.Elements().push_back(2);
//   base.Elements().push_back(1);
//   base.Elements().push_back(0);

//   Trajectory shifted = base.InterpolateToTime(Seconds(0.5));

//   cout << base.GetTime() << " -> " << shifted.GetTime() << endl;
//   for (size_t i=0; i < base.Steps(); i++)
//   {
//     cout << base.Elements()[i] << " -> " << shifted.Elements()[i] << endl;
//   }
// }

map<int,uint64_t> leadtime_offsets;

int main (int argc, char *argv[]) {
    uint32_t run = 1;
    // Time simTime = Time("60s");
    Time simTime = Time("5s");
    uint32_t rtt = 40;

    CommandLine cmd;
    cmd.AddValue("run", "run number", run);
    cmd.AddValue("rtt", "hop-by-hop rtt in msec", rtt);
    cmd.AddValue("time", "simulation time", simTime);
    cmd.AddValue("predictor", "use PredicTor", use_predictor);
    cmd.AddValue("vanilla", "use vanilla Tor", use_vanilla);
    cmd.Parse(argc, argv);

    // TestTrajectory();
    // return 0;

    NS_ABORT_MSG_UNLESS(use_predictor ^ use_vanilla, "Exactly one out of --predictor or --vanilla must be specified");

    SeedManager::SetSeed (12);
    SeedManager::SetRun (run);

    /* set global defaults */
    // GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

    /* defaults for ns3's native Tcp implementation */
    // Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1458));
    // Config::SetDefault ("ns3::TcpSocket::TcpNoDelay", BooleanValue (true));
    // Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue (100));
      // Config::SetDefault ("ns3::QueueBase::MaxSize", QueueSizeValue(QueueSize("4096B")) );

    /* TorApp defaults. Note, this also affects onion proxies. */
    Config::SetDefault ("ns3::TorBaseApp::BandwidthRate", DataRateValue (DataRate ("12Mbps")));
    Config::SetDefault ("ns3::TorBaseApp::BandwidthBurst", DataRateValue (DataRate ("12Mbps")));
    Config::SetDefault ("ns3::TorApp::WindowStart", IntegerValue (500));
    Config::SetDefault ("ns3::TorApp::WindowIncrement", IntegerValue (50));

    // Config::SetDefault ("ns3::QueueBase::MaxSize", QueueSizeValue(QueueSize("2048B")) );
    // Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (2048) );

    TorStarHelper th;
    // if (flavor == "pctcp")
    //     th.SetTorAppType("ns3::TorPctcpApp");
    // else if (flavor == "bktap")
    //     th.SetTorAppType("ns3::TorBktapApp");
    // else if (flavor == "n23")
    //     th.SetTorAppType("ns3::TorN23App");
    // else if (flavor == "fair")
    //     th.SetTorAppType("ns3::TorFairApp");

    if (use_predictor)
    {
      th.SetTorAppType("ns3::TorPredApp");
    }
    // th.EnablePcap(true);

    th.DisableProxies(true);
    th.SetRtt(MilliSeconds(rtt));
    th.SetUnderlayRate(DataRate("10Mbps"));

    Ptr<ConstantRandomVariable> m_bulkRequest = CreateObject<ConstantRandomVariable>();
    m_bulkRequest->SetAttribute("Constant", DoubleValue(pow(2,30)));
    Ptr<ConstantRandomVariable> m_bulkThink = CreateObject<ConstantRandomVariable>();
    m_bulkThink->SetAttribute("Constant", DoubleValue(0));

    Ptr<UniformRandomVariable> m_startTime = CreateObject<UniformRandomVariable> ();
    m_startTime->SetAttribute ("Min", DoubleValue (0.1));
    m_startTime->SetAttribute ("Max", DoubleValue (1.0));
    // th.SetStartTimeStream (m_startTime); // default start time when no PseudoClientSocket specified

    /* state scenario/ add circuits inline */
    th.AddCircuit(1,"entry1","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
    th.AddCircuit(2,"entry2","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
    th.AddCircuit(3,"entry3","btlnk","exit2", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );

    th.SetRelayAttribute("btlnk", "BandwidthRate", DataRateValue(DataRate("2Mb/s")));
    th.SetRelayAttribute("btlnk", "BandwidthBurst", DataRateValue(DataRate("2Mb/s")));

    // th.PrintCircuits();
    th.BuildTopology(); // finally build topology, setup relays and seed circuits

    // Make link of bottleneck slower
    {
      DataRate rate = DataRate("4Mb/s");
      Ptr<Node> client = th.GetTorNode("btlnk");
      client->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
      client->GetDevice(0)->GetChannel()->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
      client->GetDevice(0)->GetChannel()->GetDevice(1)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
    }

    /* limit the access link */
    // Ptr<Node> client = th.GetTorNode("btlnk");
    // client->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(DataRate("1MB/s"));
    // client->GetDevice(0)->GetChannel()->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(DataRate("1MB/s"));

    ApplicationContainer relays = th.GetTorAppsContainer();
    relays.Start (Seconds (0.0));
    relays.Stop (simTime);
    Simulator::Stop (simTime);

    Simulator::Schedule(Seconds(0.01), &StatsCallback, &th, simTime);
    Simulator::Schedule(Seconds(2.5), &remember_leadtime, &th, simTime);

    NS_LOG_INFO("start simulation");
    Simulator::Run ();
    NS_LOG_INFO("stop simulation");

    cout << "=== Summary ===" << endl;

    cout << "Total bytes completed (all circuits): ";
    uint64_t total_bytes_completed = 0;

    for (auto id = th.circuitIds.begin(); id != th.circuitIds.end(); ++id) {
      uint64_t val = th.GetProxyApp(*id)->baseCircuits[*id]->GetBytesWritten(INBOUND) - leadtime_offsets[*id];
      cout << "(" << val << ") ";
      total_bytes_completed += val;
    }

    cout << total_bytes_completed << endl;

    Simulator::Destroy ();
    return 0;
}

void
remember_leadtime(TorStarHelper* th, Time simTime)
{
  for (auto id = th->circuitIds.begin(); id != th->circuitIds.end(); ++id) {
    leadtime_offsets[*id] = th->GetProxyApp(*id)->baseCircuits[*id]->GetBytesWritten(INBOUND);
  }
}


template<typename T> void
print_relay (TorStarHelper * th, const char * relay) {
  cout << relay;
  auto app = DynamicCast<T> (th->GetTorApp(relay));
  cout << "/" << app->GetNode()->GetId() << ":";

  cout << " [" << app->m_writebucket.GetSize() << "," << app->m_writebucket.GetSize() << "]";
  
  for (auto&& conn : app->connections)
  {
    cout << " (" << conn->GetRemoteName() << ") ";
    if(conn->GetSocket())
    {
      cout << conn->GetSocket()->GetTxAvailable();
      if (auto tcp = DynamicCast<TcpSocketBase>(conn->GetSocket()))
      {
        cout << "[" << tcp->GetTxBuffer()->Size() << "," << tcp->GetTxBuffer()->MaxBufferSize() << "]";
      }
      cout << "/";
      cout << conn->GetSocket()->GetRxAvailable();
      if (auto tcp = DynamicCast<TcpSocketBase>(conn->GetSocket()))
      {
        cout << "[" << tcp->GetRxBuffer()->Size() << "," << tcp->GetRxBuffer()->MaxRxSequence() << "," << tcp->GetRxBuffer()->NextRxSequence() << "]";
      }
    }
    else
    {
      cout << "n/a";
    }
    
  }
  cout << endl;
}

/* example of (cumulative) i/o stats */
void
StatsCallback(TorStarHelper* th, Time simTime)
{
    cout << Simulator::Now().GetSeconds() << " ";
    vector<int>::iterator id;
    for (id = th->circuitIds.begin(); id != th->circuitIds.end(); ++id) {
      // apps
      Ptr<TorBaseApp> proxyApp = th->GetProxyApp(*id);
      Ptr<TorBaseApp> middleApp = th->GetMiddleApp(*id);
      Ptr<TorBaseApp> exitApp = th->GetExitApp(*id);

      // circuits
      Ptr<BaseCircuit> proxyCirc = proxyApp->baseCircuits[*id];
      Ptr<BaseCircuit> middleCirc = middleApp->baseCircuits[*id];
      Ptr<BaseCircuit> exitCirc = exitApp->baseCircuits[*id];

      cout << "(" << proxyCirc->GetId() << ") "
        << proxyCirc->GetBytesWritten(INBOUND) << "," << proxyCirc->GetBytesRead(INBOUND) << " / "
        << middleCirc->GetBytesWritten(INBOUND) << "," << middleCirc->GetBytesRead(INBOUND) << " / "
        << exitCirc->GetBytesWritten(INBOUND) << "," << exitCirc->GetBytesRead(INBOUND) << " ";

      // cout << "(" << proxyCirc->GetId() << ") " << proxyCirc->GetBytesWritten(INBOUND) << "," << proxyCirc->GetBytesRead(INBOUND) << "/" << exitCirc->GetBytesWritten(INBOUND) << "," << exitCirc->GetBytesRead(INBOUND) << " ";

      // cout << exitCirc->GetBytesRead(INBOUND) << " " << proxyCirc->GetBytesWritten(INBOUND) << " ";
      // cout << proxyCirc->GetBytesRead(OUTBOUND) << " " << exitCirc->GetBytesWritten(OUTBOUND) << " ";
      // proxyCirc->ResetStats(); exitCirc->ResetStats();
    }
    cout << endl;
    
    cout << "== " << Simulator::Now().GetSeconds() << " ==" << endl;
    for (const char * relay : {"entry1", "btlnk", "exit1"})
    {
      if (use_predictor)
      {
        print_relay<TorPredApp> (th, relay);
      }
      else
      {
        print_relay<TorApp> (th, relay);
      }
    }
    cout << endl;

    Time resolution = MilliSeconds(10);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallback, th, simTime);
}