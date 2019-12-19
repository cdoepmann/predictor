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
    Time simTime = Time("7.5s");
    uint32_t rtt = 80;
    string predictor_multiplexing_mode{"aggressive"};
    double predictor_feedback_loss = 0.0;
    bool predictor_oob_feedback = true;

    CommandLine cmd;
    cmd.AddValue("run", "run number", run);
    cmd.AddValue("rtt", "hop-by-hop rtt in msec", rtt);
    cmd.AddValue("time", "simulation time", simTime);
    cmd.AddValue("predictor", "use PredicTor", use_predictor);
    cmd.AddValue("vanilla", "use vanilla Tor", use_vanilla);
    cmd.AddValue("predictor-multiplex", "multiplexing mode for PredicTor", predictor_multiplexing_mode);
    cmd.AddValue("predictor-feedback-loss", "ratio of feedback messages randomly lost", predictor_feedback_loss);
    cmd.AddValue("predictor-oob-feedback", "Do not really send feedback messages over the network, but schedule their reception out of band", predictor_oob_feedback);
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

    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(5));
    // Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
    Config::SetDefault ("ns3::TcpSocket::TcpNoDelay", BooleanValue (true));

    // disable TCP congestion control
    Config::SetDefault ("ns3::TcpL4Protocol::RecoveryType", TypeIdValue (TcpDummyRecovery::GetTypeId ()));
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpDummyCong::GetTypeId ()));


    /* TorApp defaults. Note, this also affects onion proxies. */
    Config::SetDefault ("ns3::TorBaseApp::BandwidthRate", DataRateValue (DataRate ("100Mbps")));
    Config::SetDefault ("ns3::TorBaseApp::BandwidthBurst", DataRateValue (DataRate ("100Mbps")));
    Config::SetDefault ("ns3::TorApp::WindowStart", IntegerValue (500));
    Config::SetDefault ("ns3::TorApp::WindowIncrement", IntegerValue (50));

    // Config::SetDefault ("ns3::QueueBase::MaxSize", QueueSizeValue(QueueSize("2048B")) );
    // Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (2048) );
    
    Config::SetDefault ("ns3::TorPredApp::MultiplexingMode", StringValue (predictor_multiplexing_mode));
    Config::SetDefault ("ns3::TorPredApp::FeedbackLoss", DoubleValue (predictor_feedback_loss));
    Config::SetDefault ("ns3::TorPredApp::OutOfBandFeedback", BooleanValue (predictor_oob_feedback));

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
    th.AddCircuit(1,"entry1","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(0.4)) );
    th.AddCircuit(2,"entry2","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(5.6)) );
    th.AddCircuit(3,"entry3","btlnk","exit2", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(7.3)) );

    // th.SetRelayAttribute("btlnk", "BandwidthRate", DataRateValue(DataRate("2Mb/s")));
    // th.SetRelayAttribute("btlnk", "BandwidthBurst", DataRateValue(DataRate("2Mb/s")));

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
    Simulator::Schedule(Seconds(4), &remember_leadtime, &th, simTime);

    // Dump a few static facts
    dumper.dump("cell-size", "bytes", CELL_NETWORK_SIZE);

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

    dumper.dump("result", "time", simTime.GetSeconds(), "total", total_bytes_completed);

    if (use_predictor)
    {
      // Use any predictor app to get the average batch size of the shared executor
      for (auto id = th.circuitIds.begin(); id != th.circuitIds.end(); ++id) {
        double val = DynamicCast<TorPredApp>(th.GetProxyApp(*id))->controller->GetAverageExecutorBatchSize();
        dumper.dump("average-executor-batch-size", "tasks-per-call", val);
        break;
      }
    }

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

  cout << " [" << app->m_writebucket.GetSize() << "," << app->m_readbucket.GetSize() << "]";
  
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

      int bytes_tcptxbuf = 0;
      if (auto tcp = DynamicCast<TcpSocketBase>(conn->GetSocket()))
      {
        bytes_tcptxbuf = tcp->GetTxBuffer()->Size();
      }

      int bytes_outbuf = conn->GetOutbufSize();
      
      int bytes_circqueues = 0;
      auto first_circuit = conn->GetActiveCircuits ();
      auto this_circuit = first_circuit;
      do {
        NS_ASSERT(this_circuit);
        bytes_circqueues += (int) this_circuit->GetQueueSizeBytes(this_circuit->GetDirection(conn));
        this_circuit = this_circuit->GetNextCirc(conn);
      }
      while (this_circuit != first_circuit);

      dumper.dump("buffer-size-sampled",
                  "time", Simulator::Now().GetSeconds(),
                  "node", relay,
                  "conn", conn->GetRemoteName(),
                  "bytes_tcptxbuf", bytes_tcptxbuf,
                  "bytes_outbuf", bytes_outbuf,
                  "bytes_circqueues", bytes_circqueues,
                  "bytes_total", bytes_tcptxbuf + bytes_outbuf + bytes_circqueues
      );
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

      // cout << "(" << proxyCirc->GetId() << ") "
      //   << proxyCirc->GetBytesWritten(INBOUND) << "," << proxyCirc->GetBytesRead(INBOUND) << " / "
      //   << middleCirc->GetBytesWritten(INBOUND) << "," << middleCirc->GetBytesRead(INBOUND) << " / "
      //   << exitCirc->GetBytesWritten(INBOUND) << "," << exitCirc->GetBytesRead(INBOUND) << " ";

      // cout << "(" << proxyCirc->GetId() << ") " << proxyCirc->GetBytesWritten(INBOUND) << "," << proxyCirc->GetBytesRead(INBOUND) << "/" << exitCirc->GetBytesWritten(INBOUND) << "," << exitCirc->GetBytesRead(INBOUND) << " ";

      // cout << exitCirc->GetBytesRead(INBOUND) << " " << proxyCirc->GetBytesWritten(INBOUND) << " ";
      // cout << proxyCirc->GetBytesRead(OUTBOUND) << " " << exitCirc->GetBytesWritten(OUTBOUND) << " ";
      // proxyCirc->ResetStats(); exitCirc->ResetStats();

      dumper.dump("received-applevel-cumulative",
                  "time", Simulator::Now().GetSeconds(),
                  "circuit-id", *id,
                  "position", "entry",
                  "node", proxyApp->GetNodeName(),
                  "bytes", (int) proxyCirc->GetBytesRead(INBOUND)
      );
      dumper.dump("received-applevel-cumulative",
                  "time", Simulator::Now().GetSeconds(),
                  "circuit-id", *id,
                  "position", "middle",
                  "node", middleApp->GetNodeName(),
                  "bytes", (int) middleCirc->GetBytesRead(INBOUND)
      );
      dumper.dump("received-applevel-cumulative",
                  "time", Simulator::Now().GetSeconds(),
                  "circuit-id", *id,
                  "position", "exit",
                  "node", exitApp->GetNodeName(),
                  "bytes", (int) exitCirc->GetBytesRead(INBOUND)
      );

      dumper.dump("forwarded-applevel-cumulative",
                  "time", Simulator::Now().GetSeconds(),
                  "circuit-id", *id,
                  "position", "entry",
                  "node", proxyApp->GetNodeName(),
                  "bytes", (int) proxyCirc->GetBytesWritten(INBOUND)
      );
      dumper.dump("forwarded-applevel-cumulative",
                  "time", Simulator::Now().GetSeconds(),
                  "circuit-id", *id,
                  "position", "middle",
                  "node", middleApp->GetNodeName(),
                  "bytes", (int) middleCirc->GetBytesWritten(INBOUND)
      );
      dumper.dump("forwarded-applevel-cumulative",
                  "time", Simulator::Now().GetSeconds(),
                  "circuit-id", *id,
                  "position", "exit",
                  "node", exitApp->GetNodeName(),
                  "bytes", (int) exitCirc->GetBytesWritten(INBOUND)
      );
      dumper.dump("sampled-backlog",
                  "time", Simulator::Now().GetSeconds(),
                  "circuit-id", *id,
                  "bytes", (int) exitCirc->GetBytesWritten(INBOUND) - (int) proxyCirc->GetBytesWritten(INBOUND)
      );
    }
    cout << endl;
    
    cout << "== " << Simulator::Now().GetSeconds() << " ==" << endl;
    th->GetTorAppsContainer();
    for (string relay : th->GetAllRelayNames())
    {
      if (use_predictor)
      {
        print_relay<TorPredApp> (th, relay.c_str());
      }
      else
      {
        print_relay<TorApp> (th, relay.c_str());
      }
    }
    cout << endl;

    Time resolution = MilliSeconds(10);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallback, th, simTime);
}