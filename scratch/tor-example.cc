#include <iostream>
#include <fstream>

#include "ns3/tor-module.h"

using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TorExample");

void StatsCallbackBktap(TorStarHelper*, Time);
void StatsCallbackVanilla(TorStarHelper*, Time);

int main (int argc, char *argv[]) {
    uint32_t run = 1;
    Time simTime = Time("60s");
    uint32_t rtt = 40;
    bool vanilla = true;

    CommandLine cmd;
    cmd.AddValue("run", "run number", run);
    cmd.AddValue("rtt", "hop-by-hop rtt in msec", rtt);
    cmd.AddValue("time", "simulation time", simTime);
    cmd.AddValue("vanilla", "use vanilla tor", vanilla);
    cmd.Parse(argc, argv);

    SeedManager::SetSeed (42);
    SeedManager::SetRun (run);

    /* set global defaults */
    // GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

    /* defaults for ns3's native Tcp implementation */
    // Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1458));
    // Config::SetDefault ("ns3::TcpSocket::TcpNoDelay", BooleanValue (true));
    // Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue (100));

    /* TorApp defaults. Note, this also affects onion proxies. */
    // Config::SetDefault ("ns3::TorBaseApp::BandwidthRate", DataRateValue (DataRate ("100Mbps")));
    // Config::SetDefault ("ns3::TorBaseApp::BandwidthBurst", DataRateValue (DataRate ("100Mbps")));

    NS_LOG_INFO("setup topology");

    TorStarHelper ph;
    if (!vanilla) {
        ph.SetTorAppType("ns3::TorBktapApp");
    }

    ph.DisableProxies(true); // make circuits shorter (entry = proxy), thus the simulation faster
    ph.EnableNscStack(true,"cubic"); // enable linux protocol stack and set tcp flavor
    ph.SetRtt(MilliSeconds(rtt)); // set rtt
    // ph.EnablePcap(true); // enable pcap logging
    // ph.ParseFile ("circuits.dat"); // parse scenario from file

    Ptr<ConstantRandomVariable> m_bulkRequest = CreateObject<ConstantRandomVariable>();
    m_bulkRequest->SetAttribute("Constant", DoubleValue(pow(2,30)));
    Ptr<ConstantRandomVariable> m_bulkThink = CreateObject<ConstantRandomVariable>();
    m_bulkThink->SetAttribute("Constant", DoubleValue(0));

    /* state scenario/ add circuits inline */
    ph.AddCircuit(1,"entry1","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(0.1)) );
    ph.AddCircuit(2,"entry2","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(0.1)) );
    ph.AddCircuit(3,"entry3","btlnk","exit2", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(0.1)) );

    ph.SetRelayAttribute("btlnk", "BandwidthRate", DataRateValue(DataRate("2MB/s")));
    ph.SetRelayAttribute("btlnk", "BandwidthBurst", DataRateValue(DataRate("2MB/s")));

    // ph.PrintCircuits();
    ph.BuildTopology(); // finally build topology, setup relays and seed circuits

    /* limit the access link */
    // Ptr<Node> client = ph.GetTorNode("btlnk");
    // client->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(DataRate("1MB/s"));
    // client->GetDevice(0)->GetChannel()->GetDevice(0)->GetObject<PointToPointNetDevice>()->SetDataRate(DataRate("1MB/s"));

    ApplicationContainer relays = ph.GetTorAppsContainer();
    relays.Start (Seconds (0.0));
    relays.Stop (simTime);
    Simulator::Stop (simTime);

    if (vanilla) {
        Simulator::Schedule(Seconds(0), &StatsCallbackVanilla, &ph, simTime);
    } else {
        Simulator::Schedule(Seconds(0), &StatsCallbackBktap, &ph, simTime);
    }

    NS_LOG_INFO("start simulation");
    Simulator::Run ();

    NS_LOG_INFO("stop simulation");
    Simulator::Destroy ();

    return 0;
}


/* example of (cumulative) i/o stats */
void StatsCallbackBktap(TorStarHelper* ph, Time simTime) {
    cout << Simulator::Now().GetSeconds() << " ";
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
      Ptr<TorBktapApp> proxyApp = ph->GetProxyApp(*id)->GetObject<TorBktapApp> ();
      Ptr<TorBktapApp> exitApp = ph->GetExitApp(*id)->GetObject<TorBktapApp> ();
      Ptr<BktapCircuit> proxyCirc = proxyApp->circuits[*id];
      Ptr<BktapCircuit> exitCirc = exitApp->circuits[*id];
      cout << exitCirc->GetBytesRead(INBOUND) << " " << proxyCirc->GetBytesWritten(INBOUND) << " ";
      // cout << proxyCirc->GetBytesRead(OUTBOUND) << " " << exitCirc->GetBytesWritten(OUTBOUND) << " ";
      // proxyCirc->ResetStats(); exitCirc->ResetStats();
    }
    cout << endl;

    Time resolution = MilliSeconds(10);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallbackBktap, ph, simTime);
}


/* example of (cumulative) i/o stats */
void StatsCallbackVanilla(TorStarHelper* ph, Time simTime) {
    cout << Simulator::Now().GetSeconds() << " ";
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
      Ptr<TorApp> proxyApp = ph->GetProxyApp(*id)->GetObject<TorApp> ();
      Ptr<TorApp> exitApp = ph->GetExitApp(*id)->GetObject<TorApp> ();
      Ptr<Circuit> proxyCirc = proxyApp->circuits[*id];
      Ptr<Circuit> exitCirc = exitApp->circuits[*id];
      cout << exitCirc->GetStatsBytesRead(INBOUND) << " " << proxyCirc->GetStatsBytesWritten(INBOUND) << " ";
      // cout << proxyCirc->GetStatsBytesRead(OUTBOUND) << " " << exitCirc->GetStatsBytesWritten(OUTBOUND) << " ";
      // proxyCirc->ResetStatsBytes(); exitCirc->ResetStatsBytes();
    }
    cout << endl;

    Time resolution = MilliSeconds(10);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallbackVanilla, ph, simTime);
}