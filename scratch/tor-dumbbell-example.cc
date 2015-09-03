#include <iostream>
#include <fstream>

#include "ns3/tor-module.h"

using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TorExample");

void StatsCallbackBktap(TorDumbbellHelper*, Time);
void StatsCallbackVanilla(TorDumbbellHelper*, Time);
void RegisterCallbacksVanilla(TorDumbbellHelper*);
void RegisterCallbacksBktap(TorDumbbellHelper*);
void TtfbCallback(int, double, std::string);
void TtlbCallback(int, double, std::string);

int main (int argc, char *argv[]) {
    uint32_t run = 1;
    Time simTime = Time("120s");
    bool vanilla = true;

    CommandLine cmd;
    cmd.AddValue("run", "run number", run);
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

    TorDumbbellHelper ph;
    if (!vanilla){
        ph.SetTorAppType("ns3::TorBktapApp");
    }

    ph.DisableProxies(true); // make circuits shorter (entry = proxy), thus the simulation faster
    ph.EnableNscStack(true,"cubic"); // enable linux protocol stack and set tcp flavor

    Ptr<UniformRandomVariable> m_startTime = CreateObject<UniformRandomVariable> ();
    m_startTime->SetAttribute ("Min", DoubleValue (0.1));
    m_startTime->SetAttribute ("Max", DoubleValue (30.0));
    ph.SetStartTimeStream (m_startTime);

    ph.ParseFile ("circuits.dat",10); // parse scenario from file
    // ph.PrintCircuits();
    ph.BuildTopology(); // finally build topology, setup relays and seed circuits

    ApplicationContainer relays = ph.GetTorAppsContainer();

    relays.Start (Seconds (0.0));
    relays.Stop (simTime);
    Simulator::Stop (simTime);

    if (vanilla){
        Simulator::Schedule(Seconds(0), &StatsCallbackVanilla, &ph, simTime);
        RegisterCallbacksVanilla(&ph);
    } else {
        Simulator::Schedule(Seconds(0), &StatsCallbackBktap, &ph, simTime);
        RegisterCallbacksBktap(&ph);
    }

    NS_LOG_INFO("start simulation");
    Simulator::Run ();

    NS_LOG_INFO("stop simulation");
    Simulator::Destroy ();

    return 0;
}


/* example of (cumulative) i/o stats */
void StatsCallbackBktap(TorDumbbellHelper* ph, Time simTime) {
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

    Time resolution = MilliSeconds(100);
    resolution = Seconds(1);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallbackBktap, ph, simTime);
}


/* example of (cumulative) i/o stats */
void StatsCallbackVanilla(TorDumbbellHelper* ph, Time simTime) {
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

    Time resolution = MilliSeconds(100);
    resolution = Seconds(1);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallbackVanilla, ph, simTime);
}


void RegisterCallbacksVanilla (TorDumbbellHelper* ph){
    Ptr<TorApp> tapp;
    Ptr<Circuit> circ;
    Ptr<Connection> conn;
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
        tapp = ph->GetProxyApp(*id)->GetObject<TorApp>();
        circ = tapp->circuits[*id];
        conn = circ->GetConnection(INBOUND);
        conn->SetTtfbCallback(TtfbCallback, *id);
        conn->SetTtlbCallback(TtlbCallback, *id);
    }
}

void RegisterCallbacksBktap (TorDumbbellHelper* ph){
    Ptr<TorBktapApp> tapp;
    Ptr<BktapCircuit> circ;
    Ptr<UdpChannel> ch;
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
        tapp = ph->GetProxyApp(*id)->GetObject<TorBktapApp>();
        circ = tapp->circuits[*id];
        ch = circ->inbound;
        ch->SetTtfbCallback(TtfbCallback, *id);
        ch->SetTtlbCallback(TtlbCallback, *id);
    }
}

void TtfbCallback(int id, double time, std::string desc) {
    cout << Simulator::Now().GetSeconds() << " ttfb from id " << id << ": " << time << endl;
}

void TtlbCallback(int id, double time, std::string desc) {
    cout << Simulator::Now().GetSeconds() << " ttlb from id " << id << ": " << time << endl;
}