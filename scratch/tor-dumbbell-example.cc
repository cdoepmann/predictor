#include <iostream>
#include <fstream>

#include "ns3/tor-module.h"

using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TorExample");

void stats_callback_bktap(TorDumbbellHelper*, Time);
void stats_callback_vanilla(TorDumbbellHelper*, Time);
void register_callbacks_vanilla(TorDumbbellHelper*);
void register_callbacks_bktap(TorDumbbellHelper*);
void ttfb_callback(int, double, std::string);
void ttlb_callback(int, double, std::string);

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
    ph.ParseFile ("circuits.dat",10); // parse scenario from file
    // ph.PrintCircuits();
    ph.BuildTopology(); // finally build topology, setup relays and seed circuits

    ApplicationContainer relays = ph.GetTorAppsContainer();

    relays.Start (Seconds (0.0));
    relays.Stop (simTime);
    Simulator::Stop (simTime);

    if (vanilla){
        Simulator::Schedule(Seconds(0), &stats_callback_vanilla, &ph, simTime);
        register_callbacks_vanilla(&ph);
    } else {
        Simulator::Schedule(Seconds(0), &stats_callback_bktap, &ph, simTime);
        register_callbacks_bktap(&ph);
    }

    NS_LOG_INFO("start simulation");
    Simulator::Run ();

    NS_LOG_INFO("stop simulation");
    Simulator::Destroy ();

    return 0;
}


/* example of (cumulative) i/o stats */
void stats_callback_bktap(TorDumbbellHelper* ph, Time simTime) {
    cout << Simulator::Now().GetSeconds() << " ";
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
      Ptr<TorBktapApp> proxyApp = ph->GetProxyApp(*id)->GetObject<TorBktapApp> ();
      Ptr<TorBktapApp> exitApp = ph->GetExitApp(*id)->GetObject<TorBktapApp> ();
      Ptr<BktapCircuit> proxyCirc = proxyApp->circuits[*id];
      Ptr<BktapCircuit> exitCirc = exitApp->circuits[*id];
      cout << exitCirc->GetBytesRead(INBOUND) << " " << proxyCirc->GetBytesWritten(INBOUND) << " ";
      // cout << proxyCirc->get_stats_bytes_read(OUTBOUND) << " " << exitCirc->get_stats_bytes_written(OUTBOUND) << " ";
      // proxyCirc->ResetStats(); exitCirc->ResetStats();
    }
    cout << endl;

    Time resolution = MilliSeconds(100);
    resolution = Seconds(1);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &stats_callback_bktap, ph, simTime);
}


/* example of (cumulative) i/o stats */
void stats_callback_vanilla(TorDumbbellHelper* ph, Time simTime) {
    cout << Simulator::Now().GetSeconds() << " ";
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
      Ptr<TorApp> proxyApp = ph->GetProxyApp(*id)->GetObject<TorApp> ();
      Ptr<TorApp> exitApp = ph->GetExitApp(*id)->GetObject<TorApp> ();
      Ptr<Circuit> proxyCirc = proxyApp->circuits[*id];
      Ptr<Circuit> exitCirc = exitApp->circuits[*id];
      cout << exitCirc->get_stats_bytes_read(INBOUND) << " " << proxyCirc->get_stats_bytes_written(INBOUND) << " ";
      // cout << proxyCirc->get_stats_bytes_read(OUTBOUND) << " " << exitCirc->get_stats_bytes_written(OUTBOUND) << " ";
      // proxyCirc->reset_stats_bytes(); exitCirc->reset_stats_bytes();
    }
    cout << endl;

    Time resolution = MilliSeconds(100);
    resolution = Seconds(1);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &stats_callback_vanilla, ph, simTime);
}


void register_callbacks_vanilla (TorDumbbellHelper* ph){
    Ptr<TorApp> tapp;
    Ptr<Circuit> circ;
    Ptr<Connection> conn;
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
        tapp = ph->GetProxyApp(*id)->GetObject<TorApp>();
        circ = tapp->circuits[*id];
        conn = circ->get_connection(INBOUND);
        conn->SetTtfbCallback(ttfb_callback, *id);
        conn->SetTtlbCallback(ttlb_callback, *id);
    }
}

void register_callbacks_bktap (TorDumbbellHelper* ph){
    Ptr<TorBktapApp> tapp;
    Ptr<BktapCircuit> circ;
    Ptr<UdpChannel> ch;
    vector<int>::iterator id;
    for (id = ph->circuitIds.begin(); id != ph->circuitIds.end(); ++id) {
        tapp = ph->GetProxyApp(*id)->GetObject<TorBktapApp>();
        circ = tapp->circuits[*id];
        ch = circ->inbound;
        ch->SetTtfbCallback(ttfb_callback, *id);
        ch->SetTtlbCallback(ttlb_callback, *id);
    }
}

void ttfb_callback(int id, double time, std::string desc) {
    cout << Simulator::Now().GetSeconds() << " ttfb from id " << id << ": " << time << endl;
}

void ttlb_callback(int id, double time, std::string desc) {
    cout << Simulator::Now().GetSeconds() << " ttlb from id " << id << ": " << time << endl;
}