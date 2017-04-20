#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <string>

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
void CwndChangeCallback (int id, string context, uint32_t, uint32_t);
//void SeqQueueLengthPacketsChangeCallback (string context, uint32_t, uint32_t);
//void SeqQueueLengthBytesChangeCallback (string context, uint32_t, uint32_t);
void SsthreshChangeCallback (int id, string context, uint32_t, uint32_t);
void VanillaPackageWindowChangeCallback (int id, string context, int32_t, int32_t);
//void RouterQueueLengthChangeCallback (string context, uint32_t, uint32_t);
//void RouterQueueDropCallback (string context, Ptr<Packet const>);
void TcpStateChangeCallback (int id, string context, TcpSocket::TcpStates_t oldValue, TcpSocket::TcpStates_t newValue);
void TcpCongStateChangeCallback (int id, string context, TcpSocketState::TcpCongState_t oldValue, TcpSocketState::TcpCongState_t newValue);
void NewSocketCallback (int id, string context, Ptr<TorBaseApp> app, CellDirection direction, Ptr<Socket> sock);
void NewServerSocketCallback (Ptr<TorBktapApp> app, int id, Ptr<PseudoServerSocket> sock);
void BktapStateChangeCallback (Ptr<TorBktapApp> app, int id, BktapState from, BktapState to);

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
ParseNodeIntOption (map<string,uint32_t> * target, const string value)
{
  auto entries = split2vector(value, ",");
  
  for(auto it = entries.cbegin(); it != entries.cend(); ++it) {
    auto fields = split2vector(*it, ":");

    if(fields.size() != 2) {
      return false;
    }

    try {
      uint32_t value = (uint32_t) std::stoul(fields[1]);
      (*target)[fields[0]] = value;
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
ParseNodeIntDoubleOption (map<int,double> * target, const string value)
{
  auto entries = split2vector(value, ",");
  
  for(auto it = entries.cbegin(); it != entries.cend(); ++it) {
    auto fields = split2vector(*it, ":");

    if(fields.size() != 2) {
      return false;
    }

    int key;
    try {
         key = (int) std::stoul(fields[0]);
    }
    catch (const std::invalid_argument& e) {
      return false;
    }

    try {
      double value = std::stod(fields[1]);
      (*target)[key] = value;
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
ParseNodeStringOption (map<string,string> * target, const string value)
{
  auto entries = split2vector(value, ",");
  
  for(auto it = entries.cbegin(); it != entries.cend(); ++it) {
    auto fields = split2vector(*it, ":");

    if(fields.size() != 2) {
      return false;
    }

    (*target)[fields[0]] = fields[1];
  }

  return true;
}

bool
ParseIntSet (set<int> * target, const string value)
{
  target->clear();

  auto entries = split2vector(value, ",");
  bool range = false;
  int last = 1;
  
  for(auto it = entries.cbegin(); it != entries.cend(); ++it) {
    try {
      if(!range && *it == "...")
      {
        range = true;
      }
      else if(range)
      {
        int val = (int) std::stoul(*it);
        if(val <= last)
          return false;

        for(int i=last; i <= val; i++)
          target->insert(i);

        last = val;
      }
      else
      {
        int val = (int) std::stoul(*it);
        target->insert(val);
        last = val;
      }
    }
    catch (const std::invalid_argument& e) {
      return false;
    }
  }

  return true;
}

vector<int>
RandomSubset(int available, int size)
{
  NS_ABORT_MSG_UNLESS( size <= available, 
      "Cannot choose " << size << " different numbers from " << available << " available.");

  vector<int> choices;

  Ptr<UniformRandomVariable> stream = CreateObject<UniformRandomVariable> ();
  stream->SetAttribute ("Min", DoubleValue (0.0));
  stream->SetAttribute ("Max", DoubleValue (available));

  while ((int) choices.size() < size)
  {
    int chosen = (int) stream->GetValue();

    if (chosen < 0 || chosen >= available)
      continue;

    bool used_before = false;

    for (auto it = choices.begin(); it != choices.end(); ++it)
    {
      if (*it == chosen) {
        used_before = true;
        break;
      }
    }

    if (!used_before)
      choices.push_back(chosen);
  }

  return choices;
}

string flavor = "vanilla";
bool use_nsc = false;

int main (int argc, char *argv[]) {
    Time simTime = Time("60s");
    uint32_t n_circuits = 1;
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
    double beginning_interval = 0.9;
    string circuit_topology = "single_bottleneck";
    int available_relays = -1;
    set<int> beginning_circids = {1};
    map<int,double> begin_at_circuits;

    CommandLine cmd;
    cmd.AddValue("circuits", "The number of circuits", n_circuits);
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
    cmd.AddValue("node-rtts", "Set per-node RTTs overriding the default. Comma-seperated node:rtt list", MakeBoundCallback(ParseNodeIntOption, &node_rtts));
    cmd.AddValue("node-rates", "Set per-node link data rates overriding the default. Comma-seperated node:rate list", MakeBoundCallback(ParseNodeStringOption, &node_rates));
    cmd.AddValue("nagle", "Enable BackTap's Nagle algorithm (default true)", nagle);
    cmd.AddValue("min-filter", "Min-filter RTT samples instead of 90th percentile (default false)", min_filtering);
    cmd.AddValue("beginning-interval", "Interval in seconds during which the initial circuits start (default 0.9)", beginning_interval);
    cmd.AddValue("circuit-topology", "The strategy that is used by circuits for choosing the circuit relays", circuit_topology);
    cmd.AddValue("available-relays", "The number of relays that are in the pool for random circuits", available_relays);
    cmd.AddValue("beginning-circuits", "Comma-seperated list of circuits that start in the beginning", MakeBoundCallback(ParseIntSet, &beginning_circids));
    cmd.AddValue("begin-at", "Comma-seperated list of circuit:start-time mappings.", MakeBoundCallback(ParseNodeIntDoubleOption, &begin_at_circuits));
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
    if (flavor == "bktap")
        th.SetTorAppType("ns3::TorBktapApp");

    if(disable_proxy)
      th.DisableProxies(true); // make circuits shorter (entry = proxy), thus the simulation faster
    if(use_nsc)
      th.EnableNscStack(true,"reno"); // enable linux protocol stack and set tcp flavor
      //th.EnableNscStack(true,"cubic"); // enable linux protocol stack and set tcp flavor
    th.SetRtt(MilliSeconds(rtt)); // set rtt
    th.EnablePcap(enable_pcap); // enable pcap logging

    th.SetUnderlayRate(underlay_rate);

    //
    // Build the topology
    //

    Ptr<UniformRandomVariable> begin_time_stream = CreateObject<UniformRandomVariable> ();
    begin_time_stream->SetAttribute ("Min", DoubleValue (0.1));
    begin_time_stream->SetAttribute ("Max", DoubleValue (0.1 + beginning_interval));

    
    for (int circid=1; (uint32_t)circid <= n_circuits; circid++)
    {
      // Decide on relays
      string entry_name, middle_name, exit_name;

      if(circuit_topology == "single_bottleneck")
      {
        entry_name = "entry" + std::to_string(circid);
        middle_name = "btlnk";
        exit_name = "exit" + std::to_string(circid);
      }
      else if(circuit_topology == "random")
      {
        vector<int> relay_ids = RandomSubset (available_relays, 3);

        entry_name = "relay" + std::to_string(relay_ids[0]);
        middle_name = "relay" + std::to_string(relay_ids[1]);
        exit_name = "relay" + std::to_string(relay_ids[2]);
      }
      else
      {
        NS_ABORT_MSG("unknown circuit topology: " << circuit_topology);
      }

      // Decide on start time
      Ptr<RandomVariableStream> start_stream;
      if(beginning_circids.find(circid) != beginning_circids.end())
      {
        start_stream = begin_time_stream;
      }
      else if(begin_at_circuits.find(circid) != begin_at_circuits.end())
      {
        double this_start_time = begin_at_circuits[circid];
        Ptr<ConstantRandomVariable> this_start_stream = CreateObject<ConstantRandomVariable> ();
        this_start_stream->SetAttribute ("Constant", DoubleValue (this_start_time));

        start_stream = this_start_stream;
      }
      else
      {
        NS_ABORT_MSG("circuit " << circid << " has no role");
      }
      Time start_time = Seconds(start_stream->GetValue());
      output("app_start", "circuit", circid, "time", start_time.GetDouble());
      
      // Decide on object size
      Ptr<ConstantRandomVariable> size_stream = CreateObject<ConstantRandomVariable> ();
      size_stream->SetAttribute ("Constant", DoubleValue (object_size * 1024));

      // Decide on think time
      Ptr<ConstantRandomVariable> think_stream = CreateObject<ConstantRandomVariable> ();
      think_stream->SetAttribute ("Constant", DoubleValue (90.0));

      // Add the circuit
      th.AddCircuit(circid, entry_name, middle_name, exit_name,
          CreateObject<PseudoClientSocket> (
            size_stream,
            think_stream,
            start_time
          )
      );
    
    }

    if(circuit_topology == "single_bottleneck" /* || TODO */)
    {
      if(bottleneck.size() > 0 && DataRate(bottleneck_rate).GetBitRate() > 0)
      {
        th.SetRelayAttribute(bottleneck, "BandwidthRate", DataRateValue(DataRate(bottleneck_rate)));
        th.SetRelayAttribute(bottleneck, "BandwidthBurst", DataRateValue(DataRate(bottleneck_rate)));
      }
    }

    if(refill_interval.size () > 0)
    {
      th.SetAllRelaysAttribute("Refilltime", TimeValue(Time(refill_interval)));
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

    for (int circid=1; (uint32_t)circid <= n_circuits; circid++)
    {
      output("bdp", "circuit", circid, "bytes", th.GetBdp(circid));
      output("circuit-rtt", "circuit", circid, "seconds", th.GetCircuitRtt(circid).GetSeconds());
      output("circuit-data-rate", "circuit", circid, "bytes-per-second", th.GetCircuitDataRate(circid).GetBitRate()/8.0);
      output("optimal-bktap-circuit-cwnd", "circuit", circid, "bytes", th.GetOptimalBktapCircuitCwnd(circid));
      output("optimal-bktap-circuit-backlog", "circuit", circid, "bytes", th.GetOptimalBktapBacklog(circid));
    }

    /* log the router queues */
//    map<STRING, ptr<PointToPointNetDevice> > router_devices = th.GetRouterDevices();
//    {
//      map<string, Ptr<PointToPointNetDevice> >::const_iterator it;
//
//      for(it = router_devices.begin(); it != router_devices.end(); ++it)
//      {
//        PointerValue queue_val;
//        it->second->GetAttribute("TxQueue", queue_val);
//        Ptr<DropTailQueue> queue = queue_val.Get<DropTailQueue> ();
//        NS_ASSERT(queue);
//
//        queue->TraceConnect("BytesInQueue", it->first, MakeCallback(&RouterQueueLengthChangeCallback));
//        queue->TraceConnect("Drop", it->first+"-qdrop", MakeCallback(&RouterQueueDropCallback));
//        it->second->TraceConnect("MacTxDrop", it->first+"-macdrop", MakeCallback(&RouterQueueDropCallback));
//        it->second->TraceConnect("PhyTxDrop", it->first+"-phy", MakeCallback(&RouterQueueDropCallback));
//        it->second->TraceConnect("MacTx", it->first+"-mactx", MakeCallback(&RouterQueueDropCallback));
//        it->second->TraceConnect("PhyTxBegin", it->first+"-phybegin", MakeCallback(&RouterQueueDropCallback));
//        it->second->TraceConnect("PhyTxEnd", it->first+"-phyend", MakeCallback(&RouterQueueDropCallback));
//        // queue->TraceConnect("Drop", it->first, MakeCallback(&RouterQueueDropCallback));
//      }
//    }
//    {
//      Ptr<Node> client = th.GetTorNode("exit1");
//
//      PointerValue queue_val;
//      client->GetDevice(0)->GetAttribute("TxQueue", queue_val);
//      Ptr<DropTailQueue> queue = queue_val.Get<DropTailQueue> ();
//      NS_ASSERT(queue);
//
//      queue->TraceConnect("BytesInQueue", "node-exit1", MakeCallback(&RouterQueueLengthChangeCallback));
//      queue->TraceConnect("Drop", "node-exit1-qdrop", MakeCallback(&RouterQueueDropCallback));
//      client->GetDevice(0)->TraceConnect("MacTxDrop", "node-exit1-macdrop", MakeCallback(&RouterQueueDropCallback));
//      client->GetDevice(0)->TraceConnect("PhyTxDrop", "node-exit1-phy", MakeCallback(&RouterQueueDropCallback));
//      client->GetDevice(0)->TraceConnect("MacTx", "node-exit1-mactx", MakeCallback(&RouterQueueDropCallback));
//      client->GetDevice(0)->TraceConnect("PhyTxBegin", "node-exit1-phybegin", MakeCallback(&RouterQueueDropCallback));
//      client->GetDevice(0)->TraceConnect("PhyTxEnd", "node-exit1-phyend", MakeCallback(&RouterQueueDropCallback));
//    }

    ApplicationContainer relays = th.GetTorAppsContainer();
    relays.Start (Seconds (0.0));
    relays.Stop (simTime);
    Simulator::Stop (simTime);
    
    // per-circuit stuff
    for (int i=1; (uint32_t)i <= n_circuits; i++)
    {
      if(flavor == "vanilla") {
        DynamicCast<TorApp>(th.GetProxyApp(i))->TraceConnect("NewSocket", "proxy", MakeBoundCallback(&NewSocketCallback, i));
        DynamicCast<TorApp>(th.GetEntryApp(i))->TraceConnect("NewSocket", "entry", MakeBoundCallback(&NewSocketCallback, i));
        DynamicCast<TorApp>(th.GetMiddleApp(i))->TraceConnect("NewSocket", "middle", MakeBoundCallback(&NewSocketCallback, i));
        DynamicCast<TorApp>(th.GetExitApp(i))->TraceConnect("NewSocket", "exit", MakeBoundCallback(&NewSocketCallback, i));

        DynamicCast<TorApp>(th.GetExitApp(i))->circuits[i]->TraceConnect("PackageWindow", "exit", MakeBoundCallback(&VanillaPackageWindowChangeCallback, i));
      }
      else if(flavor == "bktap") {
        DynamicCast<TorBktapApp>(th.GetProxyApp(i))->circuits[i]->inboundQueue
          ->TraceConnect("CongestionWindow", "proxy-inbound",  MakeBoundCallback(&CwndChangeCallback, i));
        DynamicCast<TorBktapApp>(th.GetProxyApp(i))->circuits[i]->outboundQueue
          ->TraceConnect("CongestionWindow", "proxy-outbound",  MakeBoundCallback(&CwndChangeCallback, i));

        DynamicCast<TorBktapApp>(th.GetEntryApp(i))->circuits[i]->inboundQueue
          ->TraceConnect("CongestionWindow", "entry-inbound",  MakeBoundCallback(&CwndChangeCallback, i));
        DynamicCast<TorBktapApp>(th.GetEntryApp(i))->circuits[i]->outboundQueue
          ->TraceConnect("CongestionWindow", "entry-outbound",  MakeBoundCallback(&CwndChangeCallback, i));

        DynamicCast<TorBktapApp>(th.GetMiddleApp(i))->circuits[i]->inboundQueue
          ->TraceConnect("CongestionWindow", "middle-inbound",  MakeBoundCallback(&CwndChangeCallback, i));
        DynamicCast<TorBktapApp>(th.GetMiddleApp(i))->circuits[i]->outboundQueue
          ->TraceConnect("CongestionWindow", "middle-outbound",  MakeBoundCallback(&CwndChangeCallback, i));

        DynamicCast<TorBktapApp>(th.GetExitApp(i))->circuits[i]->inboundQueue
          ->TraceConnect("CongestionWindow", "exit-inbound",  MakeBoundCallback(&CwndChangeCallback, i));
        DynamicCast<TorBktapApp>(th.GetExitApp(i))->circuits[i]->outboundQueue
          ->TraceConnect("CongestionWindow", "exit-outbound",  MakeBoundCallback(&CwndChangeCallback, i));


//        DynamicCast<TorBktapApp>(th.GetProxyApp(i))->circuits[i]->inboundQueue
//          ->TraceConnect("QueueLengthBytes", "proxy-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
//        DynamicCast<TorBktapApp>(th.GetProxyApp(i))->circuits[i]->outboundQueue
//          ->TraceConnect("QueueLengthBytes", "proxy-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
//
//        DynamicCast<TorBktapApp>(th.GetEntryApp(i))->circuits[i]->inboundQueue
//          ->TraceConnect("QueueLengthBytes", "entry-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
//        DynamicCast<TorBktapApp>(th.GetEntryApp(i))->circuits[i]->outboundQueue
//          ->TraceConnect("QueueLengthBytes", "entry-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
//
//        DynamicCast<TorBktapApp>(th.GetMiddleApp(i))->circuits[i]->inboundQueue
//          ->TraceConnect("QueueLengthBytes", "middle-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
//        DynamicCast<TorBktapApp>(th.GetMiddleApp(i))->circuits[i]->outboundQueue
//          ->TraceConnect("QueueLengthBytes", "middle-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
//
//        DynamicCast<TorBktapApp>(th.GetExitApp(i))->circuits[i]->inboundQueue
//          ->TraceConnect("QueueLengthBytes", "exit-inbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
//        DynamicCast<TorBktapApp>(th.GetExitApp(i))->circuits[i]->outboundQueue
//          ->TraceConnect("QueueLengthBytes", "exit-outbound",  MakeCallback(&SeqQueueLengthBytesChangeCallback));
      }
    }

    // more per-app stuff
    {
      ApplicationContainer apps = th.GetTorAppsContainer ();
      for (uint32_t i=0; i<apps.GetN (); i++)
        {
          Ptr<Application> app = apps.Get (i);
          Ptr<TorBktapApp> bktap_app = DynamicCast<TorBktapApp> (app);
          if (bktap_app)
          {
            bktap_app->TraceConnectWithoutContext("NewServerSocket", MakeCallback(&NewServerSocketCallback));
            bktap_app->TraceConnectWithoutContext("State", MakeCallback(&BktapStateChangeCallback));
          }
        }
    }
    
    // remember the circuit relay names
    auto circuit_relays = th.GetCircuitRelays ();
    for (auto it = circuit_relays.begin(); it != circuit_relays.end (); ++it)
    {
      int circ = it->first;

      ostringstream relays;
      bool first = true;
      for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2)
      {
        if (!first)
          relays << ",";
        else
          first = false;
        relays << *it2;
      }

      output("circuit-relays", "circuit", circ, "relays", relays.str());
    }

    Simulator::Schedule(Seconds(0), &StatsCallback, &th, simTime);

    output("PACKET_PAYLOAD_SIZE", "value", PACKET_PAYLOAD_SIZE);

    NS_LOG_INFO("start simulation");
    Simulator::Run ();

    NS_LOG_INFO("stop simulation");
    Simulator::Destroy ();

    return 0;
}

void StartResponseCallback(int id) {
  output("start-response", "circuit", id);
}

void
NewSocketCallback (int id, string context, Ptr<TorBaseApp> app, CellDirection direction, Ptr<Socket> sock)
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
    tcp->TraceConnect("CongestionWindow", context + sdir, MakeBoundCallback(&CwndChangeCallback, id));
    tcp->TraceConnect("SlowStartThreshold", context + sdir, MakeBoundCallback(&SsthreshChangeCallback, id));
    tcp->TraceConnect("State", context + sdir, MakeBoundCallback(&TcpStateChangeCallback, id));
    tcp->TraceConnect("CongState", context + sdir, MakeBoundCallback(&TcpCongStateChangeCallback, id));

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
    server->TraceConnectWithoutContext("StartResponse", MakeBoundCallback(&StartResponseCallback, id));
  }
}

void
NewServerSocketCallback (Ptr<TorBktapApp> app, int id, Ptr<PseudoServerSocket> server)
{
  cout << "new pseudo server socket" << endl;
  server->TraceConnectWithoutContext("StartResponse", MakeBoundCallback(&StartResponseCallback, id));
}

void
BktapStateChangeCallback (Ptr<TorBktapApp> app, int id, BktapState from, BktapState to)
{
  output("bktap-state", "node", app->GetNodeName(),
                        "circuit", id,
                        "from", FormatBktapState(from),
                        "to", FormatBktapState(to)
  );
}

void
TcpStateChangeCallback (int id, string context, TcpSocket::TcpStates_t oldValue, TcpSocket::TcpStates_t newValue)
{
  output("tcp-state", "circuit", id, "which", context, "from", TcpSocket::TcpStateName[oldValue], "to", TcpSocket::TcpStateName[newValue]);
}

void
TcpCongStateChangeCallback (int id, string context, TcpSocketState::TcpCongState_t oldValue, TcpSocketState::TcpCongState_t newValue)
{
  output("cong-state", "circuit", id, "which", context, "from", TcpSocketState::TcpCongStateName[oldValue], "to", TcpSocketState::TcpCongStateName[newValue]);
}

void
CwndChangeCallback (int id, string context, uint32_t oldValue, uint32_t newValue)
{
  output("cwnd", "circuit", id, "which", context, "from", oldValue, "to", newValue);
}

//void
//SeqQueueLengthPacketsChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
//{
//  output("seq-queue-len-cells", "which", context, "from", oldValue, "to", newValue);
//}
//
//void
//SeqQueueLengthBytesChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
//{
//  output("seq-queue-len-bytes", "which", context, "from", oldValue, "to", newValue);
//}

void
SsthreshChangeCallback (int id, string context, uint32_t oldValue, uint32_t newValue)
{
  output("ssthresh", "circuit", id, "which", context, "from", oldValue, "to", newValue);
}

//void
//RouterQueueLengthChangeCallback (string context, uint32_t oldValue, uint32_t newValue)
//{
//  output("router-queue-len", "which", context, "from", oldValue, "to", newValue);
//}
//
//void
//RouterQueueDropCallback (string context, Ptr<Packet const> packet)
//{
//  if(flavor == "vanilla")
//  {
//    Ptr<Packet> p = packet->Copy();
//
//    PppHeader ppp_header; p->RemoveHeader (ppp_header);
//    Ipv4Header ip_header; p->RemoveHeader (ip_header);
//
//    TcpHeader tcp;
//    p->RemoveHeader(tcp);
//
//    output("router-queue-drop", "which", context,
//        "tcp-seq", tcp.GetSequenceNumber (),
//        "tcp-ack", tcp.GetAckNumber (),
//        "tcp-len", p->GetSize ()
//    );
//  }
//  else
//  {
//    output("router-queue-drop", "which", context);
//  }
//}


void
VanillaPackageWindowChangeCallback (int id, string context, int32_t oldValue, int32_t newValue)
{
  output("vanilla-package-window", "circuit", id, "which", context, "from", oldValue, "to", newValue);
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
//}

void
TtlbCallback (int id, double time, string hint)
{
    cout << Simulator::Now().GetSeconds() << " " << hint << " TTLB from id " << id << ": " << time << endl;
    output("last-byte", "circuit", id);
}

void
TtfbCallback (int id, double time, string hint)
{
    cout << Simulator::Now().GetSeconds() << " " << hint << " TTFB from id " << id << ": " << time << endl;
    output("first-byte", "circuit", id);
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

      output("node-written-so-far-sampled", "circuit", *id, "which", "proxy-inbound", "bytes", proxyCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "circuit", *id, "which", "proxy-inbound", "bytes", proxyCirc->GetBytesRead(INBOUND));

      output("node-written-so-far-sampled", "circuit", *id, "which", "entry-inbound", "bytes", entryCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "circuit", *id, "which", "entry-inbound", "bytes", entryCirc->GetBytesRead(INBOUND));

      output("node-written-so-far-sampled", "circuit", *id, "which", "middle-inbound", "bytes", middleCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "circuit", *id, "which", "middle-inbound", "bytes", middleCirc->GetBytesRead(INBOUND));

      output("node-written-so-far-sampled", "circuit", *id, "which", "exit-inbound", "bytes", exitCirc->GetBytesWritten(INBOUND));
      output("node-read-so-far-sampled", "circuit", *id, "which", "exit-inbound", "bytes", exitCirc->GetBytesRead(INBOUND));

      if(flavor == "bktap")
      {
        output("bktap-window", "circuit", *id, "which", "exit-inbound", "bytes",
          DynamicCast<TorBktapApp>(th->GetExitApp(1))->circuits[1]->inboundQueue->Window()
        );
        output("bktap-inflight", "circuit", *id, "which", "exit-inbound", "bytes",
          DynamicCast<TorBktapApp>(th->GetExitApp(1))->circuits[1]->inboundQueue->Inflight()
        );
        output("bktap-virtrtt-current", "circuit", *id, "which", "exit-inbound", "millis",
          DynamicCast<TorBktapApp>(th->GetExitApp(1))->circuits[1]->inboundQueue->virtRtt.currentRtt.GetMilliSeconds()
        );
      }

      output("server-written-so-far-sampled", "circuit", *id, "bytes", exitCirc->GetBytesWritten(INBOUND));
      output("client-read-so-far-sampled", "circuit", *id, "bytes", proxyCirc->GetBytesRead(INBOUND));
      // cout << proxyCirc->GetBytesRead(OUTBOUND) << " " << exitCirc->GetBytesWritten(OUTBOUND) << " ";
      // proxyCirc->ResetStats(); exitCirc->ResetStats();
    }

    Time resolution = MilliSeconds(10);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallback, th, simTime);
}
