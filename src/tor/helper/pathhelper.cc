#include "pathhelper.h"

PathHelper::PathHelper() {
    m_p2pHelper.SetDeviceAttribute("DataRate", StringValue("10Gb/s"));
    m_p2pHelper.SetChannelAttribute ("Delay", StringValue ("20ms"));

    m_starHelper = 0;
    m_nSpokes = 0;
    m_nSpokes = 1; // hack: spare one for concurrent traffic
    m_disableProxies = false;
    m_enablePcap = false;
    m_enableBulkSockets = true;
    m_factory.SetTypeId ("ns3::TorApp");
}

PathHelper::~PathHelper() {
    if (m_starHelper) delete m_starHelper;
}

void
PathHelper::AddCircuit(int id, string entryName, string middleName, string exitName) {
    NS_ASSERT (m_circuits.find(id) == m_circuits.end());
    CircuitDescriptor desc (id, GetProxyName(id), entryName, middleName, exitName, GetServerName(id));
    m_circuits[id] = desc;
    circuitIds.push_back(id);

    if(!m_disableProxies) {
        AddRelay(GetProxyName(id));
    }

    AddRelay(entryName);
    AddRelay(middleName);
    AddRelay(exitName);

    if (!m_enableBulkSockets) {
        AddServer(GetServerName(id));
    }
}

void
PathHelper::AddCircuit(int id, string entryName, string middleName, string exitName,
        Ptr<RandomVariableStream> rng_request, Ptr<RandomVariableStream> rng_think) {

    NS_ASSERT (m_circuits.find(id) == m_circuits.end());
    CircuitDescriptor desc (id, GetProxyName(id), entryName, middleName, exitName, GetServerName(id), rng_request, rng_think);
    m_circuits[id] = desc;
    circuitIds.push_back(id);

    if(!m_disableProxies) {
        AddRelay(GetProxyName(id));
    }

    AddRelay(entryName);
    AddRelay(middleName);
    AddRelay(exitName);

    if (!m_enableBulkSockets) {
        AddServer(GetServerName(id));
    }
}


void
PathHelper::AddRelay(string name) {
    if (m_relays.find(name) == m_relays.end()) {
        RelayDescriptor desc (name, m_nSpokes++, CreateTorApp());
        m_relays[name] = desc;
        m_relayApps.Add(desc.tapp);
    }
}

void
PathHelper::AddServer(string name) {
    NS_ASSERT (m_server.find(name) == m_server.end());
    m_server[name] = m_nSpokes++;
}

void
PathHelper::SetRelayAttribute(string relayName, string attrName, const AttributeValue &value){
    NS_ASSERT (m_relays.find(relayName) != m_relays.end());
    GetTorApp(relayName)->SetAttribute(attrName, value);
}

void
PathHelper::SetRtt(Time rtt){
    m_p2pHelper.SetChannelAttribute ("Delay", TimeValue(rtt/4.0));
}

void
PathHelper::EnableNscStack(bool enableNscStack, string nscTcpCong){
    if (enableNscStack) {
        m_nscTcpCong = nscTcpCong;
    } else {
        m_nscTcpCong = "";
    }
}

void
PathHelper::EnablePcap(bool enablePcap){
    m_enablePcap = enablePcap;
}

void
PathHelper::EnableBulkSockets(bool enableBulkSockets){
    m_enableBulkSockets = enableBulkSockets;
}

void
PathHelper::SetTorAppType(string type) {
    m_factory.SetTypeId(type);
}

void
PathHelper::DisableProxies(bool disableProxies){
    m_disableProxies = disableProxies;
}

void
PathHelper::ParseFile(string filename) {
    ifstream f;
    f.open(filename.c_str());
    NS_ASSERT (f.is_open());

    int id;
    string path[3], bw[3], dummy;
    // while(f >> id >> dummy >> path[0] >> bw[0] >> path[1] >> bw[1] >> path[2] >> bw[2] >> dummy) {
    while(f >> id >> path[0] >> dummy >> bw[0] >> path[1] >> dummy >> bw[1] >> path[2] >> dummy >> bw[2]) {
        AddCircuit(id, path[0], path[1], path[2]);
        for (int i = 0; i < 3; ++i) {
            SetRelayAttribute(path[i], "BandwidthRate", DataRateValue(DataRate(bw[i]+"B/s")));
            SetRelayAttribute(path[i], "BandwidthBurst", DataRateValue(DataRate(bw[i]+"B/s")));
        }
    }
    f.close();
}


void
PathHelper::BuildTopology() {
    m_starHelper = new PointToPointStarHelper(m_nSpokes,m_p2pHelper);

    //install stack
    m_stackHelper.Install(m_starHelper->GetHub());

    //use the linux protocol stack for the spokes
    if (m_nscTcpCong.size() > 0) {
        std::string nscStack = "liblinux2.6.26.so";
        m_stackHelper.SetTcp ("ns3::NscTcpL4Protocol","Library",StringValue (nscStack));
        if (m_nscTcpCong != "cubic"){
            Config::Set ("/NodeList/*/$ns3::Ns3NscStack<linux2.6.26>/net.ipv4.tcp_congestion_control", StringValue (m_nscTcpCong));
        }
    }

    for (int i=0;i<m_nSpokes; ++i) {
        m_stackHelper.Install(m_starHelper->GetSpokeNode(i));
    }

    //assign ipv4
    m_addressHelper.SetBase("10.1.0.0", "255.255.255.0");
    m_starHelper->AssignIpv4Addresses(m_addressHelper);

    if(m_enablePcap) {
        m_p2pHelper.EnablePcapAll ("node");
        AsciiTraceHelper ascii;
        m_p2pHelper.EnableAsciiAll (ascii.CreateFileStream ("tor.tr"));
    }

    // Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    // em->SetAttribute ("ErrorRate", DoubleValue (0.0001));
    // Ptr<Node> hub = m_starHelper->GetHub();
    // //Exclude Loopback Device
    // for (uint32_t i = 0; i < hub->GetNDevices()-1; ++i) {
    //     hub->GetDevice(i)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    // }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    InstallCircuits();
}


void
PathHelper::InstallCircuits() {
    Ptr<TorBaseApp> clientApp;
    Ptr<TorBaseApp> entryApp;
    Ptr<TorBaseApp> middleApp;
    Ptr<TorBaseApp> exitApp;

    BulkSendHelper serversHelper ("ns3::TcpSocketFactory", Address ());
    Ipv4AddressHelper ipHelper = Ipv4AddressHelper ("127.0.0.0", "255.0.0.0");

    std::map<int,CircuitDescriptor>::iterator i;
    for(i=m_circuits.begin(); i != m_circuits.end(); ++i) {
        CircuitDescriptor e = i->second;

        if(!m_disableProxies) {
            clientApp = InstallTorApp(e.proxy());
        }
        entryApp = InstallTorApp(e.entry());
        middleApp = InstallTorApp(e.middle());
        exitApp = InstallTorApp(e.exit());

        Ipv4Address clientAddress;
        if(!m_disableProxies) {
            clientAddress = m_starHelper->GetSpokeIpv4Address(m_relays[e.proxy()].spokeId);
        }

        Ipv4Address entryAddress  = m_starHelper->GetSpokeIpv4Address(m_relays[e.entry()].spokeId);
        Ipv4Address middleAddress = m_starHelper->GetSpokeIpv4Address(m_relays[e.middle()].spokeId);
        Ipv4Address exitAddress   = m_starHelper->GetSpokeIpv4Address(m_relays[e.exit()].spokeId);

        Ipv4Address serverAddress;
        if (!m_enableBulkSockets) {
            serverAddress = m_starHelper->GetSpokeIpv4Address(m_server[e.server()]);
            AddressValue remoteAddress (InetSocketAddress(exitAddress, 9001));
            serversHelper.SetAttribute("Remote", remoteAddress);
            Ptr<Node> serverNode = m_starHelper->GetSpokeNode(m_server[e.server()]);
            m_serverApps.Add(serversHelper.Install(serverNode));
        } else {
            serverAddress = ipHelper.NewAddress();
        }

        exitApp->AddCircuit(e.id, serverAddress, EDGE_CONN, middleAddress, OR_CONN);
        middleApp->AddCircuit(e.id, exitAddress, OR_CONN, entryAddress, OR_CONN);
        if(!m_disableProxies) {
            entryApp->AddCircuit(e.id, middleAddress, OR_CONN, clientAddress, OR_CONN);
            if(e.m_rng_request && e.m_rng_think) {
                clientApp->AddCircuit(e.id, entryAddress, OR_CONN, ipHelper.NewAddress(), EDGE_CONN, e.m_rng_request, e.m_rng_think);
            } else {
                clientApp->AddCircuit(e.id, entryAddress, OR_CONN, ipHelper.NewAddress(), EDGE_CONN);
            }
        } else {
            if(e.m_rng_request && e.m_rng_think) {
                entryApp->AddCircuit(e.id, middleAddress, OR_CONN, ipHelper.NewAddress(), EDGE_CONN, e.m_rng_request, e.m_rng_think);
            } else {
                entryApp->AddCircuit(e.id, middleAddress, OR_CONN, ipHelper.NewAddress(), EDGE_CONN);
            }
        }
    }

}


Ptr<TorBaseApp>
PathHelper::InstallTorApp(string name) {
    NS_ASSERT (m_relays.find(name) != m_relays.end());
    RelayDescriptor desc = m_relays[name];
    if (m_starHelper->GetSpokeNode(desc.spokeId)->GetNApplications() == 0 ) {
        m_starHelper->GetSpokeNode(desc.spokeId)->AddApplication(desc.tapp);
    }

    return desc.tapp;
}


Ptr<Node>
PathHelper::GetSpokeNode(uint32_t id) {
    return m_starHelper->GetSpokeNode(id);
}


ApplicationContainer
PathHelper::GetTorAppsContainer() {
    return m_relayApps;
}

ApplicationContainer
PathHelper::GetServersContainer() {
    return m_serverApps;
}


Ptr<TorBaseApp>
PathHelper::GetTorApp(string name) {
    return m_relays[name].tapp;
}

Ptr<Node>
PathHelper::GetTorNode(string name) {
    return m_starHelper->GetSpokeNode(m_relays[name].spokeId);
}

Ptr<TorBaseApp>
PathHelper::GetExitApp(int id) {
    if(m_circuits.find(id) == m_circuits.end()) {
        return 0;
    }
    CircuitDescriptor desc = m_circuits[id];
    return GetTorApp(desc.exit());
}

Ptr<TorBaseApp>
PathHelper::GetMiddleApp(int id) {
    if(m_circuits.find(id) == m_circuits.end()) {
        return 0;
    }
    CircuitDescriptor desc = m_circuits[id];
    return GetTorApp(desc.middle());
}

Ptr<TorBaseApp>
PathHelper::GetEntryApp(int id) {
    if(m_circuits.find(id) == m_circuits.end()) {
        return 0;
    }
    CircuitDescriptor desc = m_circuits[id];
    return GetTorApp(desc.entry());
}

Ptr<TorBaseApp>
PathHelper::GetProxyApp(int id) {
    if(m_circuits.find(id) == m_circuits.end()) {
        return 0;
    }
    CircuitDescriptor desc = m_circuits[id];
    if(m_disableProxies) {
        return GetTorApp(desc.entry());
    } else {
        return GetTorApp(desc.proxy());
    }
}


Ptr<TorBaseApp>
PathHelper::CreateTorApp() {
    Ptr<TorBaseApp> tapp = m_factory.Create<TorBaseApp> ();
    NS_ASSERT(tapp);
    return tapp;
}


string
PathHelper::GetProxyName(int id) {
    stringstream ss;
    ss << "proxy" << id;
    return ss.str();
}


string
PathHelper::GetServerName(int id) {
    stringstream ss;
    ss << "server" << id;
    return ss.str();
}


void
PathHelper::PrintCircuits() {
    std::map<int,CircuitDescriptor>::iterator i;
    for(i=m_circuits.begin(); i != m_circuits.end(); ++i) {
        CircuitDescriptor e = i->second;
        cout << e.id << ":";
        if (!m_disableProxies) {
            cout << "\t" << e.proxy() << "[" << m_relays[e.proxy()].spokeId+1 << "]";
        }
        cout << "\t" << e.entry() << "[" << m_relays[e.entry()].spokeId+1 << "]";
        cout << "\t" << e.middle() << "[" << m_relays[e.middle()].spokeId+1 << "]";
        cout << "\t" << e.exit() << "[" << m_relays[e.exit()].spokeId+1 << "]";
        if (!m_enableBulkSockets) {
            cout << "\t" << e.server() << "[" << m_server[e.server()]+1 << "]";
        }
        cout << endl;
    }
}
