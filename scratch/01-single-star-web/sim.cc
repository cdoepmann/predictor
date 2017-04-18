#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>

#include "ns3/tor-module.h"
#include "ns3/pointer.h"
#include "ns3/log.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include "utils.h"

using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TorExperiment");

void StatsCallback(TorStarHelper*, Time);
void TtlbCallback (int, double, string);
void TtfbCallback (int, double, string);
void ClientRecvCallback (int, uint32_t, string);
void CwndChangeCallback (string context, uint32_t, uint32_t);
void SeqQueueLengthPacketsChangeCallback (string context, uint32_t, uint32_t);
void SeqQueueLengthBytesChangeCallback (string context, uint32_t, uint32_t);
void SsthreshChangeCallback (string context, uint32_t, uint32_t);
void VanillaPackageWindowChangeCallback (string context, int32_t, int32_t);
void RouterQueueLengthChangeCallback (string context, uint32_t, uint32_t);
void RouterQueueDropCallback (string context, Ptr<Packet const>);
void TcpStateChangeCallback (string context, TcpSocket::TcpStates_t oldValue, TcpSocket::TcpStates_t newValue);
void TcpCongStateChangeCallback (string context, TcpSocketState::TcpCongState_t oldValue, TcpSocketState::TcpCongState_t newValue);
void AppStartedCallback (string context, Ptr<TorBaseApp>);
void NewSocketCallback (string context, Ptr<TorBaseApp> app, CellDirection direction, Ptr<Socket> sock);
void NewServerSocketCallback (Ptr<TorBktapApp> app, Ptr<PseudoServerSocket> sock);

template <typename T,
         typename D1_1, typename D1_2,
         typename D2_1, typename D2_2,
         typename D3_1, typename D3_2,
         typename D4_1, typename D4_2
         >
void output(T type,
            D1_1 d1_1, D1_2 d1_2,
            D2_1 d2_1, D2_2 d2_2,
            D3_1 d3_1, D3_2 d3_2,
            D4_1 d4_1, D4_2 d4_2
           )
{
    std::cout << "OUTPUT[" << type << "] simtime=" << Simulator::Now().GetSeconds() << " "
      << d1_1 << "=" << d1_2 << " "
      << d2_1 << "=" << d2_2 << " "
      << d3_1 << "=" << d3_2 << " "
      << d4_1 << "=" << d4_2 << " "
    << std::endl;
}

template <typename T,
         typename D1_1, typename D1_2,
         typename D2_1, typename D2_2,
         typename D3_1, typename D3_2
         >
void output(T type,
            D1_1 d1_1, D1_2 d1_2,
            D2_1 d2_1, D2_2 d2_2,
            D3_1 d3_1, D3_2 d3_2
           )
{
    std::cout << "OUTPUT[" << type << "] simtime=" << Simulator::Now().GetSeconds() << " "
      << d1_1 << "=" << d1_2 << " "
      << d2_1 << "=" << d2_2 << " "
      << d3_1 << "=" << d3_2 << " "
    << std::endl;
}

template <typename T, typename U, typename V, typename W, typename X>
void output(T type, U data, V data2, W data3, X data4) {
    std::cout << "OUTPUT[" << type << "] simtime=" << Simulator::Now().GetSeconds() << " " << data << "=" << data2 << " " << data3 << "=" << data4 << std::endl;
}

template <typename T, typename U, typename V>
void output(T type, U data, V data2) {
    std::cout << "OUTPUT[" << type << "] simtime=" << Simulator::Now().GetSeconds() << " " << data << "=" << data2 << std::endl;
}

template <typename T, typename U>
void output(T type, U data) {
    std::cout << "OUTPUT[" << type << "] simtime=" << Simulator::Now().GetSeconds() << " " << data << std::endl;
}

struct NodeSpecCommand {
  string name;
  enum class aspect {
    RTT,
    RATE
  } aspect;
  string data;
};

bool
ParseCmdlineNodeSpec (vector<NodeSpecCommand> &commands, const string value)
{
  string::size_type n = value.find(":");
  if (n == string::npos)
    return false;
  string node = value.substr(0, n);

  string::size_type m = value.find(":", n+1);
  if (m == string::npos)
    return false;

  string mode = value.substr(n+1, m-(n+1));
  string data = value.substr(m+1, string::npos);

  NodeSpecCommand command;

  if(mode == "rtt") {
    command = {
      node,
      NodeSpecCommand::aspect::RTT,
      data
    };
  }
  else if(mode == "rate") {
    command = {
      node,
      NodeSpecCommand::aspect::RATE,
      data
    };
  }
  else
    return false;

  commands.push_back(command);

  return true;
}

bool
ParseNodeRttOption (map<string,uint32_t> * rtts, const string value)
{
  auto entries = split2vector(value, ",");
  
  for(auto it = entries.cbegin(); it != entries.cend(); ++it) {
    auto fields = split2vector(*it, ":");

    if(fields.size() != 2) {
      return false;
    }

    try {
      uint32_t rtt = (uint32_t) std::stoul(fields[1]);
      (*rtts)[fields[0]] = rtt;
    }
    catch (const std::invalid_argument& e) {
      return false;
    }
    catch (const std::out_of_range& e) {
      return false;
    }
  }

  return true;
}

bool
ParseNodeRateOption (map<string,string> * rates, const string value)
{
  auto entries = split2vector(value, ",");
  
  for(auto it = entries.cbegin(); it != entries.cend(); ++it) {
    auto fields = split2vector(*it, ":");

    if(fields.size() != 2) {
      return false;
    }

    (*rates)[fields[0]] = fields[1];
  }

  return true;
}

string flavor = "vanilla";
bool use_nsc = false;

int main (int argc, char *argv[]) {
    Time simTime = Time("60s");
    uint32_t rtt = 20;
    uint32_t object_size = 320;
    bool disable_proxy = true;
    bool enable_pcap = false;
    string bottleneck = "btlnk";
    string bottleneck_rate = "2Mb/s";
    string bottleneck_link_rate = "0";
    string underlay_rate = "10Gb/s";
    string refill_interval = "100ms";
    string startup_scheme = "slow-start";
    uint32_t queue_max_bytes = 0;
    uint32_t queue_max_packets = 0;
    bool disable_dack = false;
    bool nagle = true;
    bool min_filtering = false;
    map<string,uint32_t> node_rtts;
    map<string,string> node_rates;

    CommandLine cmd;
    cmd.AddValue("rtt", "hop-by-hop rtt in msec", rtt);
    cmd.AddValue("time", "simulation time", simTime);
    cmd.AddValue("size", "object size in kilo bytes", object_size);
    cmd.AddValue("flavor", "Tor flavor", flavor);
    cmd.AddValue("bottleneck", "The node to be used as a bottleneck", bottleneck);
    cmd.AddValue("bottleneck-rate", "The rate of the bottleneck (application layer)", bottleneck_rate);
    cmd.AddValue("bottleneck-link-rate", "The rate of the bottleneck (underlay link)", bottleneck_link_rate);
    cmd.AddValue("underlay-rate", "The rate of the underlay links", underlay_rate);
    cmd.AddValue("disable-proxy", "Disable proxy (default true)", disable_proxy);
    cmd.AddValue("enable-pcap", "Enable .pcap logging (default false)", enable_pcap);
    cmd.AddValue("queue-max-bytes", "Maximum length of the netdevies' tx queues", queue_max_bytes);
    cmd.AddValue("queue-max-packets", "Maximum #packets the netdevies' tx queues", queue_max_packets);
    cmd.AddValue("use-nsc", "Enable the NSC network stack (default false)", use_nsc);
    cmd.AddValue("refill-interval", "Interval in which the app-layer token buckets are refilled", refill_interval);
    cmd.AddValue("disable-dack", "Disable Delayed ACKs (ns-3 TCP only!)", disable_dack);
    cmd.AddValue("startup-scheme", "The startup scheme to use with BackTap", startup_scheme);
    cmd.AddValue("node-rtts", "Set per-node RTTs overriding the default. Comma-seperated node:rtt list", MakeBoundCallback(ParseNodeRttOption, &node_rtts));
    cmd.AddValue("node-rates", "Set per-node link data rates overriding the default. Comma-seperated node:rate list", MakeBoundCallback(ParseNodeRateOption, &node_rates));
    cmd.AddValue("nagle", "Enable BackTap's Nagle algorithm (default true)", nagle);
    cmd.AddValue("min-filter", "Min-filter RTT samples instead of 90th percentile (default false)", min_filtering);
    cmd.Parse(argc, argv);

    /* set global defaults */
    // GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

    /* defaults for ns3's native Tcp implementation */
    // Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1458));
    // Config::SetDefault ("ns3::TcpSocket::TcpNoDelay", BooleanValue (true));
    
    /* avoid initial delayed ACK delay */
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(2));
    
    if(disable_dack)
    {
      Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
    }
    
    NS_ABORT_MSG_UNLESS( queue_max_bytes*queue_max_packets == 0, 
        "Queues can only be limited by either bytes or packets, not both.");
    if(queue_max_bytes > 0)
    {
      Config::SetDefault ("ns3::Queue::Mode", EnumValue (Queue::QUEUE_MODE_BYTES));
      Config::SetDefault ("ns3::Queue::MaxBytes", UintegerValue (queue_max_bytes));
    }
    else if(queue_max_packets > 0)
    {
      Config::SetDefault ("ns3::Queue::Mode", EnumValue (Queue::QUEUE_MODE_PACKETS));
      Config::SetDefault ("ns3::Queue::MaxPackets", UintegerValue (queue_max_packets));
    }

  /* TorApp defaults. Note, this also affects onion proxies. */
  // Config::SetDefault ("ns3::TorBaseApp::BandwidthRate", DataRateValue (DataRate ("12Mbps")));
    // Config::SetDefault ("ns3::TorBaseApp::BandwidthBurst", DataRateValue (DataRate ("12Mbps")));
    Config::SetDefault ("ns3::TorApp::WindowStart", IntegerValue (500));
    Config::SetDefault ("ns3::TorApp::WindowIncrement", IntegerValue (50));

    NS_LOG_INFO("setup topology");

    // apply min-filter setting
    Config::SetDefault ("ns3::TorBktapApp::MinFiltering", BooleanValue (min_filtering));

    TorStarHelper th;
    if (flavor == "pctcp")
        th.SetTorAppType("ns3::TorPctcpApp");
    else if (flavor == "bktap")
        th.SetTorAppType("ns3::TorBktapApp");
    else if (flavor == "n23")
        th.SetTorAppType("ns3::TorN23App");
    else if (flavor == "fair")
        th.SetTorAppType("ns3::TorFairApp");

    if(disable_proxy)
      th.DisableProxies(true); // make circuits shorter (entry = proxy), thus the simulation faster
    if(use_nsc)
      th.EnableNscStack(true,"reno"); // enable linux protocol stack and set tcp flavor
      //th.EnableNscStack(true,"cubic"); // enable linux protocol stack and set tcp flavor
    th.SetRtt(MilliSeconds(rtt)); // set rtt
    th.EnablePcap(enable_pcap); // enable pcap logging
    // th.ParseFile ("circuits.dat"); // parse scenario from file

    th.SetUnderlayRate(underlay_rate);

    // properties for web a web circuit
    Ptr<ConstantRandomVariable> m_clientRequest = CreateObject<ConstantRandomVariable> ();
    m_clientRequest->SetAttribute ("Constant", DoubleValue (object_size * 1024));
    Ptr<ConstantRandomVariable> m_clientThink = CreateObject<ConstantRandomVariable> ();
    m_clientThink->SetAttribute ("Constant", DoubleValue (90.0));
    //Ptr<UniformRandomVariable> m_clientThink = CreateObject<UniformRandomVariable> ();
    //m_clientThink->SetAttribute ("Min", DoubleValue (1.0));
    //m_clientThink->SetAttribute ("Max", DoubleValue (20.0));

    // properties for bulk transfer
    //Ptr<ConstantRandomVariable> m_bulkRequest = CreateObject<ConstantRandomVariable>();
    //m_bulkRequest->SetAttribute("Constant", DoubleValue(pow(2,30)));
    //Ptr<ConstantRandomVariable> m_bulkThink = CreateObject<ConstantRandomVariable>();
    //m_bulkThink->SetAttribute("Constant", DoubleValue(0));

    Ptr<UniformRandomVariable> m_startTime = CreateObject<UniformRandomVariable> ();
    m_startTime->SetAttribute ("Min", DoubleValue (0.1));
    m_startTime->SetAttribute ("Max", DoubleValue (1.0));
    th.SetStartTimeStream (m_startTime); // default start time when no PseudoClientSocket specified
    Time generatedStartTime = Seconds(m_startTime->GetValue ());
    output("app_start", "start_at", generatedStartTime.GetSeconds());

    /* state scenario/ add circuits inline */
    th.AddCircuit(1,"entry1","btlnk","exit1", CreateObject<PseudoClientSocket> (m_clientRequest, m_clientThink, generatedStartTime) );
    // th.AddCircuit(2,"entry2","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
    // th.AddCircuit(3,"entry3","btlnk","exit2", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );

    if(bottleneck.size() > 0 && DataRate(bottleneck_rate).GetBitRate() > 0)
    {
      th.SetRelayAttribute(bottleneck, "BandwidthRate", DataRateValue(DataRate(bottleneck_rate)));
      th.SetRelayAttribute(bottleneck, "BandwidthBurst", DataRateValue(DataRate(bottleneck_rate)));
    }

    if(refill_interval.size () > 0)
    {
      th.SetRelayAttribute("entry1", "Refilltime", TimeValue(Time(refill_interval)));
      th.SetRelayAttribute("btlnk", "Refilltime", TimeValue(Time(refill_interval)));
      th.SetRelayAttribute("exit1", "Refilltime", TimeValue(Time(refill_interval)));
      if (!disable_proxy)
        th.SetRelayAttribute("proxy1", "Refilltime", TimeValue(Time(refill_interval)));
    }

    ostringstream hint_formatter;
    // hint_formatter << flavor << " with " << object_size << " KB (run " << RngSeedManager::GetRun() << ")";

    th.RegisterTtfbCallback (&TtfbCallback, hint_formatter.str());
    th.RegisterTtlbCallback (&TtlbCallback, hint_formatter.str());
    th.RegisterRecvCallback (&ClientRecvCallback, hint_formatter.str());

    // apply Tor app-specific settings
    {
      ApplicationContainer apps = th.GetTorAppsContainer ();
      for (uint32_t i=0; i<apps.GetN (); i++)
        {
          Ptr<Application> app = apps.Get (i);
          Ptr<TorBktapApp> bktap_app = DynamicCast<TorBktapApp> (app);
          if (bktap_app)
          {
            bktap_app->SetStartupScheme (startup_scheme);
            bktap_app->SetNagle (nagle);
          }
        }
    }

    /* set node-specific link delays */
    for(auto it = node_rtts.cbegin(); it != node_rtts.cend(); ++it) {
      th.SetNodeRtt(it->first, MilliSeconds(it->second));
    }

    // th.PrintCircuits();
    th.BuildTopology(); // finally build topology, setup relays and seed circuits

    /* limit the access link */
    if(bottleneck.size() > 0 && DataRate(bottleneck_link_rate).GetBitRate() > 0)
    {
      DataRate rate = DataRate(bottleneck_link_rate);
      Ptr<Node> client = th.GetTorNode(bottleneck);
      client->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
      client->GetDevice(0)->GetChannel()->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
      client->GetDevice(0)->GetChannel()->GetDevice(1)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
    }

    /* limit the node-wise access links */
    for(auto it = node_rates.cbegin(); it != node_rates.cend(); ++it) {
      DataRate rate = DataRate(it->second);
      Ptr<Node> client = th.GetTorNode(it->first);
      client->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
      client->GetDevice(0)->GetChannel()->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
      client->GetDevice(0)->GetChannel()->GetDevice(1)->GetObject<PointToPointNetDevice>()->SetDataRate(rate);
    }

    output("bdp", "circuit", 1, "bytes", th.GetBdp(1));
    output("circuit-rtt", "circuit", 1, "seconds", th.GetCircuitRtt(1).GetSeconds());
    output("circuit-data-rate", "circuit", 1, "bytes-per-second", th.GetCircuitDataRate(1).GetBitRate()/8.0);
    output("optimal-bktap-circuit-cwnd", "circuit", 1, "bytes", th.GetOptimalBktapCircuitCwnd(1));
    output("optimal-bktap-circuit-backlog", "circuit", 1, "bytes", th.GetOptimalBktapBacklog(1));

    /* log the router queues */
    map<string, Ptr<PointToPointNetDevice> > router_devices = th.GetRouterDevices();
    {
      map<string, Ptr<PointToPointNetDevice> >::const_iterator it;

      for(it = router_devices.begin(); it != router_devices.end(); ++it)
      {
        PointerValue queue_val;
        it->second->GetAttribute("TxQueue", queue_val);
        Ptr<DropTailQueue> queue = queue_val.Get<DropTailQueue> ();
        NS_ASSERT(queue);

        queue->TraceConnect("BytesInQueue", it->first, MakeCallback(&RouterQueueLengthChangeCallback));
        queue->TraceConnect("Drop", it->first+"-qdrop", MakeCallback(&RouterQueueDropCallback));
        it->second->TraceConnect("MacTxDrop", it->first+"-macdrop", MakeCallback(&RouterQueueDropCallback));
        it->second->TraceConnect("PhyTxDrop", it->first+"-phy", MakeCallback(&RouterQueueDropCallback));
        it->second->TraceConnect("MacTx", it->first+"-mactx", MakeCallback(&RouterQueueDropCallback));
        it->second->TraceConnect("PhyTxBegin", it->first+"-phybegin", MakeCallback(&RouterQueueDropCallback));
        it->second->TraceConnect("PhyTxEnd", it->first+"-phyend", MakeCallback(&RouterQueueDropCallback));
        // queue->TraceConnect("Drop", it->first, MakeCallback(&RouterQueueDropCallback));
      }
    }
    {
      Ptr<Node> client = th.GetTorNode("exit1");

      PointerValue queue_val;
      client->GetDevice(0)->GetAttribute("TxQueue", queue_val);
      Ptr<DropTailQueue> queue = queue_val.Get<DropTailQueue> ();
      NS_ASSERT(queue);

      queue->TraceConnect("BytesInQueue", "node-exit1", MakeCallback(&RouterQueueLengthChangeCallback));
      queue->TraceConnect("Drop", "node-exit1-qdrop", MakeCallback(&RouterQueueDropCallback));
      client->GetDevice(0)->TraceConnect("MacTxDrop", "node-exit1-macdrop", MakeCallback(&RouterQueueDropCallback));
      client->GetDevice(0)->TraceConnect("PhyTxDrop", "node-exit1-phy", MakeCallback(&RouterQueueDropCallback));
      client->GetDevice(0)->TraceConnect("MacTx", "node-exit1-mactx", MakeCallback(&RouterQueueDropCallback));
      client->GetDevice(0)->TraceConnect("PhyTxBegin", "node-exit1-phybegin", MakeCallback(&RouterQueueDropCallback));
      client->GetDevice(0)->TraceConnect("PhyTxEnd", "node-exit1-phyend", MakeCallback(&RouterQueueDropCallback));
    }

    ApplicationContainer relays = th.GetTorAppsContainer();
    relays.Start (Seconds (0.0));
    relays.Stop (simTime);
    Simulator::Stop (simTime);
    
    if(flavor == "vanilla") {
      //DynamicCast<TorApp>(th.GetProxyApp(1))->circuits[1]->GetConnection(INBOUND)->;
      //Config::MatchContainer mc = Config::LookupMatches ("/NodeList/2/ApplicationList
      //cout << "huhublabla: " << 
      DynamicCast<TorApp>(th.GetProxyApp(1))->TraceConnect("NewSocket", "proxy", MakeCallback(&NewSocketCallback));
      DynamicCast<TorApp>(th.GetEntryApp(1))->TraceConnect("NewSocket", "entry", MakeCallback(&NewSocketCallback));
      DynamicCast<TorApp>(th.GetMiddleApp(1))->TraceConnect("NewSocket", "middle", MakeCallback(&NewSocketCallback));
      DynamicCast<TorApp>(th.GetExitApp(1))->TraceConnect("NewSocket", "exit", MakeCallback(&NewSocketCallback));

      DynamicCast<TorApp>(th.GetExitApp(1))->circuits[1]->TraceConnect("PackageWindow", "exit", MakeCallback(&VanillaPackageWindowChangeCallback));
    }
    else if(flavor == "bktap") {
      DynamicCast<TorBktapApp>(th.GetProxyApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("CongestionWindow", "proxy-inbound",  MakeCallback(&CwndChangeCallback));
      DynamicCast<TorBktapApp>(th.GetProxyApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("CongestionWindow", "proxy-outbound",  MakeCallback(&CwndChangeCallback));

      DynamicCast<TorBktapApp>(th.GetEntryApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("CongestionWindow", "entry-inbound",  MakeCallback(&CwndChangeCallback));
      DynamicCast<TorBktapApp>(th.GetEntryApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("CongestionWindow", "entry-outbound",  MakeCallback(&CwndChangeCallback));

      DynamicCast<TorBktapApp>(th.GetMiddleApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("CongestionWindow", "middle-inbound",  MakeCallback(&CwndChangeCallback));
      DynamicCast<TorBktapApp>(th.GetMiddleApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("CongestionWindow", "middle-outbound",  MakeCallback(&CwndChangeCallback));

      DynamicCast<TorBktapApp>(th.GetExitApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("CongestionWindow", "exit-inbound",  MakeCallback(&CwndChangeCallback));
      DynamicCast<TorBktapApp>(th.GetExitApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("CongestionWindow", "exit-outbound",  MakeCallback(&CwndChangeCallback));


      DynamicCast<TorBktapApp>(th.GetProxyApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("QueueLengthBytes", "proxy-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
      DynamicCast<TorBktapApp>(th.GetProxyApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("QueueLengthBytes", "proxy-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));

      DynamicCast<TorBktapApp>(th.GetEntryApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("QueueLengthBytes", "entry-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
      DynamicCast<TorBktapApp>(th.GetEntryApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("QueueLengthBytes", "entry-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));

      DynamicCast<TorBktapApp>(th.GetMiddleApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("QueueLengthBytes", "middle-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
      DynamicCast<TorBktapApp>(th.GetMiddleApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("QueueLengthBytes", "middle-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));

      DynamicCast<TorBktapApp>(th.GetExitApp(1))->circuits[1]->inboundQueue
        ->TraceConnect("QueueLengthBytes", "exit-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
      DynamicCast<TorBktapApp>(th.GetExitApp(1))->circuits[1]->outboundQueue
        ->TraceConnect("QueueLengthBytes", "exit-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));


      DynamicCast<TorBktapApp>(th.GetExitApp(1))->TraceConnectWithoutContext("NewServerSocket", MakeCallback(&NewServerSocketCallback));
    }

    Simulator::Schedule(Seconds(0), &StatsCallback, &th, simTime);

    output("PACKET_PAYLOAD_SIZE", "value", PACKET_PAYLOAD_SIZE);

    NS_LOG_INFO("start simulation");
    Simulator::Run ();

    NS_LOG_INFO("stop simulation");
    Simulator::Destroy ();

    return 0;
}

void StartResponseCallback() {
  output("start-response", "");
}

void
NewSocketCallback (string context, Ptr<TorBaseApp> app, CellDirection direction, Ptr<Socket> sock)
{
  Ptr<TorApp> tor_app = DynamicCast<TorApp>(app);

  string sdir;
  if(direction == INBOUND)
    sdir = "-inbound";
  else
    sdir = "-outbound";

  cout << "new sock: " << context << sdir << " at " << DynamicCast<TcpSocket>(sock) << endl;

  Ptr<TcpSocket> tcp = DynamicCast<TcpSocket>(sock);
  Ptr<PseudoServerSocket> server = DynamicCast<PseudoServerSocket>(sock);

  if(tcp) {
    tcp->TraceConnect("CongestionWindow", context + sdir, MakeCallback(&CwndChangeCallback));
    tcp->TraceConnect("SlowStartThreshold", context + sdir, MakeCallback(&SsthreshChangeCallback));
    tcp->TraceConnect("State", context + sdir, MakeCallback(&TcpStateChangeCallback));
    tcp->TraceConnect("CongState", context + sdir, MakeCallback(&TcpCongStateChangeCallback));

    UintegerValue data;
    tcp->GetAttribute("InitialCwnd", data);
    cout << context << " InitialCwnd: " << data.Get() << endl;

//    if(app->GetNode()->GetObject<TcpL4Protocol>()) {
//      cout << "got tcpl4: ";
//      TypeIdValue socket_type;
//      app->GetNode()->GetObject<TcpL4Protocol>()->GetAttribute("SocketType", socket_type);
//      cout << socket_type.Get();
//      cout << endl;
//    }
//    else
//      cout << "did not get tcpl4" << endl;

    tcp->GetAttribute("InitialSlowStartThreshold", data);
    cout << context << " InitialSlowStartThreshold: " << data.Get() << endl;
  }
  else if(server) {
    server->TraceConnectWithoutContext("StartResponse", MakeCallback(&StartResponseCallback));
  }
}

void
NewServerSocketCallback (Ptr<TorBktapApp> app, Ptr<PseudoServerSocket> server)
{
  cout << "new pseudo server socket" << endl;
  server->TraceConnectWithoutContext("StartResponse", MakeCallback(&StartResponseCallback));
}

void
TcpStateChangeCallback (string context, TcpSocket::TcpStates_t oldValue, TcpSocket::TcpStates_t newValue)
{
  output("tcp-state", "which", context, "from", TcpSocket::TcpStateName[oldValue], "to", TcpSocket::TcpStateName[newValue]);
}

void
TcpCongStateChangeCallback (string context, TcpSocketState::TcpCongState_t oldValue, TcpSocketState::TcpCongState_t newValue)
{
  output("cong-state", "which", context, "from", TcpSocketState::TcpCongStateName[oldValue], "to", TcpSocketState::TcpCongStateName[newValue]);
}

void
CwndChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
{
  output("cwnd", "which", context, "from", oldValue, "to", newValue);
}

void
SeqQueueLengthPacketsChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
{
  output("seq-queue-len-cells", "which", context, "from", oldValue, "to", newValue);
}

void
SeqQueueLengthBytesChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
{
  output("seq-queue-len-bytes", "which", context, "from", oldValue, "to", newValue);
}

void
SsthreshChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
{
  output("ssthresh", "which", context, "from", oldValue, "to", newValue);
}

void
RouterQueueLengthChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
{
  output("router-queue-len", "which", context, "from", oldValue, "to", newValue);
}

void
RouterQueueDropCallback (string context, Ptr<Packet const> packet)
{
  if(flavor == "vanilla")
  {
    Ptr<Packet> p = packet->Copy();

    PppHeader ppp_header; p->RemoveHeader (ppp_header);
    Ipv4Header ip_header; p->RemoveHeader (ip_header);

    TcpHeader tcp;
    p->RemoveHeader(tcp);

    output("router-queue-drop", "which", context,
        "tcp-seq", tcp.GetSequenceNumber (),
        "tcp-ack", tcp.GetAckNumber (),
        "tcp-len", p->GetSize ()
    );
  }
  else
  {
    output("router-queue-drop", "which", context);
  }
}


void
VanillaPackageWindowChangeCallback (string context, int32_t oldValue, int32_t newValue)
{
  output("vanilla-package-window", "which", context, "from", oldValue, "to", newValue);
}

void
AppStartedCallback (string context, Ptr<TorBaseApp> app)
{
  cout << "app started (" << context << ")" << endl;
  Ptr<TcpSocket> sock;

  sock = DynamicCast<TcpSocket>(DynamicCast<TorApp>(app)->circuits[1]->GetConnection(INBOUND)->GetSocket());
  if(sock) {
    sock->TraceConnect("CongestionWindow", context + "-inbound", MakeCallback(&CwndChangeCallback));
    cout << "watching " << context + "-inbound" << endl;
  }
  else {
    if (DynamicCast<TorApp>(app)->circuits[1]->GetConnection(INBOUND)->GetSocket()) {
      cout << context + "-inbound: raw ok" << endl;
    }
  }

  sock = DynamicCast<TcpSocket>(DynamicCast<TorApp>(app)->circuits[1]->GetConnection(OUTBOUND)->GetSocket());
  if(sock) {
    sock->TraceConnect("CongestionWindow", context + "-outbound", MakeCallback(&CwndChangeCallback));
    cout << "watching " << context + "-outbound" << endl;
  }
  else {
    if (DynamicCast<TorApp>(app)->circuits[1]->GetConnection(OUTBOUND)->GetSocket()) {
      cout << context + "-outbound: raw ok" << endl;
    }
  }


//  //DynamicCast<TorApp>(app)->circuits[1]->GetConnection(INBOUND)->GetSocket()->TraceConnect("CongestionWindow", context + "-inbound", MakeCallback(&CwndChangeCallback));
//  IntegerValue cwnd;
//  //Ptr<Object> sock = DynamicCast<TorApp>(app)->circuits[1]->GetConnection(INBOUND)->GetSocket();
//  //Ptr<Socket> raw_socket = DynamicCast<TorApp>(app)->circuits[1]->GetConnection(INBOUND)->GetSocket();
//  PointerValue raw_socket_val;
//  Ptr<Socket> raw_socket;
//  DynamicCast<TorApp>(app)->circuits[1]->GetConnection(OUTBOUND)->GetAttribute("Socket", raw_socket_val);
//  raw_socket = raw_socket_val.Get< Socket >();
//  if(raw_socket)
//    cout << " got raw socket " << raw_socket->GetTypeId() << endl;
//  else
//    cout << " got no raw socket" << endl;
//
//  cout << "Type: " << raw_socket->GetSocketType() << endl;
//  cout << "raw_socket: " << raw_socket << endl;
//
//  Ptr<TcpSocket> sock = DynamicCast<TcpSocket>(raw_socket);
//  if(sock)
//    cout << " got socket" << endl;
//  else {
//    cout << " got no socket" << endl;
//    return;
//  }
//  cout << sock->GetTypeId() << endl;
//
//  for(uint32_t i=0; i<sock->GetTypeId().GetAttributeN(); i++) {
//    cout << "- " << sock->GetTypeId().GetAttributeFullName(i) << " = " << endl;
//  }
//
//  sock->TraceConnect("CongestionWindow", context + "-outbound", MakeCallback(&CwndChangeCallback));
}

void
TtlbCallback (int id, double time, string hint)
{
    cout << Simulator::Now().GetSeconds() << " " << hint << " TTLB from id " << id << ": " << time << endl;
    output("last-byte", "");
}

void
TtfbCallback (int id, double time, string hint)
{
    cout << Simulator::Now().GetSeconds() << " " << hint << " TTFB from id " << id << ": " << time << endl;
    output("first-byte", "");
}

void
ClientRecvCallback (int id, uint32_t new_bytes, string hint)
{
  output("client-got-data", "circuit", id, "bytes", new_bytes);
}

/* example of (cumulative) i/o stats */
void
StatsCallback(TorStarHelper* th, Time simTime)
{
    cout << setw(5) << Simulator::Now().GetSeconds() << " ";
    vector<int>::iterator id;
    for (id = th->circuitIds.begin(); id != th->circuitIds.end(); ++id) {

      Ptr<TorBaseApp> proxyApp = th->GetProxyApp(*id);
      Ptr<TorBaseApp> entryApp = th->GetEntryApp(*id);
      Ptr<TorBaseApp> middleApp = th->GetMiddleApp(*id);
      Ptr<TorBaseApp> exitApp = th->GetExitApp(*id);
      Ptr<BaseCircuit> proxyCirc = proxyApp->baseCircuits[*id];
      Ptr<BaseCircuit> entryCirc = entryApp->baseCircuits[*id];
      Ptr<BaseCircuit> middleCirc = middleApp->baseCircuits[*id];
      Ptr<BaseCircuit> exitCirc = exitApp->baseCircuits[*id];
      cout
          << setw(8) << proxyCirc->GetBytesRead(OUTBOUND) << " "
          << setw(8) << proxyCirc->GetBytesWritten(OUTBOUND) << " "
          << setw(8) << proxyCirc->GetBytesRead(INBOUND) << " "
          << setw(8) << proxyCirc->GetBytesWritten(INBOUND) << " |"

          << setw(8) << entryCirc->GetBytesRead(OUTBOUND) << " "
          << setw(8) << entryCirc->GetBytesWritten(OUTBOUND) << " "
          << setw(8) << entryCirc->GetBytesRead(INBOUND) << " "
          << setw(8) << entryCirc->GetBytesWritten(INBOUND) << " |"

          << setw(8) << middleCirc->GetBytesRead(OUTBOUND) << " "
          << setw(8) << middleCirc->GetBytesWritten(OUTBOUND) << " "
          << setw(8) << middleCirc->GetBytesRead(INBOUND) << " "
          << setw(8) << middleCirc->GetBytesWritten(INBOUND) << " |"

          << setw(8) << exitCirc->GetBytesRead(OUTBOUND) << " "
          << setw(8) << exitCirc->GetBytesWritten(OUTBOUND) << " "
          << setw(8) << exitCirc->GetBytesRead(INBOUND) << " "
          << setw(8) << exitCirc->GetBytesWritten(INBOUND)
          ;
      cout << endl;

      output("node-written-so-far-sampled", "which", "proxy-inbound", "bytes", proxyCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "which", "proxy-inbound", "bytes", proxyCirc->GetBytesRead(INBOUND));

      output("node-written-so-far-sampled", "which", "entry-inbound", "bytes", entryCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "which", "entry-inbound", "bytes", entryCirc->GetBytesRead(INBOUND));

      output("node-written-so-far-sampled", "which", "middle-inbound", "bytes", middleCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "which", "middle-inbound", "bytes", middleCirc->GetBytesRead(INBOUND));

      output("node-written-so-far-sampled", "which", "exit-inbound", "bytes", exitCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "which", "exit-inbound", "bytes", exitCirc->GetBytesRead(INBOUND));

      if(flavor == "bktap")
      {
        output("bktap-window", "which", "exit-inbound", "bytes",
          DynamicCast<TorBktapApp>(th->GetExitApp(1))->circuits[1]->inboundQueue->Window()
        );
        output("bktap-inflight", "which", "exit-inbound", "bytes",
          DynamicCast<TorBktapApp>(th->GetExitApp(1))->circuits[1]->inboundQueue->Inflight()
        );
        output("bktap-virtrtt-current", "which", "exit-inbound", "millis",
          DynamicCast<TorBktapApp>(th->GetExitApp(1))->circuits[1]->inboundQueue->virtRtt.currentRtt.GetMilliSeconds()
        );
      }

      output("server-written-so-far-sampled", "bytes", exitCirc->GetBytesWritten(INBOUND));
      output("client-read-so-far-sampled", "bytes", proxyCirc->GetBytesRead(INBOUND));
      // cout << proxyCirc->GetBytesRead(OUTBOUND) << " " << exitCirc->GetBytesWritten(OUTBOUND) << " ";
      // proxyCirc->ResetStats(); exitCirc->ResetStats();
    }

    Time resolution = MilliSeconds(10);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallback, th, simTime);
}
