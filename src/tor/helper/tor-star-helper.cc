#include "tor-star-helper.h"
#include "ns3/traffic-control-helper.h"

TorStarHelper::TorStarHelper ()
{
  m_underlayLinkDelay = Time("20ms");
  m_underlayRate = DataRate("10Gb/s");
  m_p2pHelper.SetDeviceAttribute ("DataRate", DataRateValue(m_underlayRate));
  m_p2pHelper.SetChannelAttribute ("Delay", TimeValue(m_underlayLinkDelay));
  m_rng = CreateObject<UniformRandomVariable> ();
  m_startTimeStream = 0;
  m_starHelper = 0;
  m_nSpokes = 0;
  m_nSpokes = 1;   // hack: spare one for concurrent traffic simulation
  m_disableProxies = false;
  m_enablePcap = false;
  m_factory.SetTypeId ("ns3::TorApp");
}

TorStarHelper::~TorStarHelper ()
{
  if (m_starHelper)
    {
      delete m_starHelper;
    }
}

void
TorStarHelper::AddCircuit (int id, string entryName, string middleName, string exitName,
                           Ptr<PseudoClientSocket> clientSocket)
{

  NS_ASSERT (m_circuits.find (id) == m_circuits.end ());

  if (!clientSocket)
    {
      clientSocket = CreateObject<PseudoClientSocket> ();
      if (m_startTimeStream)
        {
          clientSocket->Start (Seconds (m_startTimeStream->GetValue ()));
        }
    }

  CircuitDescriptor desc (id, GetProxyName (id), entryName, middleName, exitName, clientSocket);
  m_circuits[id] = desc;
  circuitIds.push_back (id);

  if (!m_disableProxies)
    {
      AddRelay (GetProxyName (id));
    }

  AddRelay (entryName);
  AddRelay (middleName);
  AddRelay (exitName);
}

void
TorStarHelper::AddRelay (string name)
{
  if (m_relays.find (name) == m_relays.end ())
    {
      RelayDescriptor desc (name, m_nSpokes++, CreateTorApp ());
      m_relays[name] = desc;
      m_relayApps.Add (desc.tapp);
    }
}

void
TorStarHelper::SetRelayAttribute (string relayName, string attrName, const AttributeValue &value)
{
  NS_ASSERT (m_relays.find (relayName) != m_relays.end ());
  GetTorApp (relayName)->SetAttribute (attrName, value);
}

void
TorStarHelper::SetRtt (Time rtt)
{
  m_underlayLinkDelay = rtt/4.0;
  m_p2pHelper.SetChannelAttribute ("Delay", TimeValue (m_underlayLinkDelay));
}

void
TorStarHelper::SetUnderlayRate (DataRate rate)
{
  m_underlayRate = rate;
  m_p2pHelper.SetDeviceAttribute ("DataRate", DataRateValue(m_underlayRate));
}

void
TorStarHelper::SetStartTimeStream (Ptr<RandomVariableStream> startTimeStream)
{
  m_startTimeStream = startTimeStream;
}

void
TorStarHelper::EnableNscStack (bool enableNscStack, string nscTcpCong)
{
  if (enableNscStack)
    {
      m_nscTcpCong = nscTcpCong;
    }
  else
    {
      m_nscTcpCong = "";
    }
}

void
TorStarHelper::EnablePcap (bool enablePcap)
{
  m_enablePcap = enablePcap;
}

void
TorStarHelper::SetTorAppType (string type)
{
  m_factory.SetTypeId (type);
}

void
TorStarHelper::DisableProxies (bool disableProxies)
{
  m_disableProxies = disableProxies;
}

void
TorStarHelper::RegisterTtfbCallback (void (*ttfb)(int, double, string), string hint)
{
  NS_ASSERT (m_circuits.size () > 0 );
  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor desc = i->second;
      desc.m_clientSocket->SetTtfbCallback (ttfb, desc.id, hint);
    }
}

void
TorStarHelper::RegisterTtlbCallback (void (*ttlb)(int, double, string), string hint)
{
  NS_ASSERT (m_circuits.size () > 0);
  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor desc = i->second;
      desc.m_clientSocket->SetTtlbCallback (ttlb, desc.id, hint);
    }
}

void
TorStarHelper::RegisterRecvCallback (void (*cb)(int, uint32_t, string), string hint)
{
  NS_ASSERT (m_circuits.size () > 0);
  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor desc = i->second;
      desc.m_clientSocket->SetClientRecvCallback (cb, desc.id, hint);
    }
}


void
TorStarHelper::ParseFile (string filename, uint32_t m)
{
  ifstream f;
  f.open (filename.c_str ());
  NS_ASSERT (f.is_open ());

  set<uint32_t> chosenCircuits;
  if (m > 0)
    {
      string line;
      uint32_t n;
      for (n = 0; getline (f, line); ++n)
        {
        }
      NS_ASSERT (m <= n);
      while (chosenCircuits.size () < m)
        {
          chosenCircuits.insert (m_rng->GetInteger (1,n));
        }
      f.close ();
      f.open (filename.c_str ());
    }

  int id;
  string path[3], bw[3], dummy;
  uint32_t lineno = 0;
  while (f >> id >> path[0] >> dummy >> bw[0] >> path[1] >> dummy >> bw[1] >> path[2] >> dummy >> bw[2])
    {
      ++lineno;
      if (m > 0 && chosenCircuits.find (lineno) == chosenCircuits.end ())
        {
          continue;
        }

      AddCircuit (id, path[0], path[1], path[2]);
      for (int i = 0; i < 3; ++i)
        {
          SetRelayAttribute (path[i], "BandwidthRate", DataRateValue (DataRate (bw[i] + "B/s")));
          SetRelayAttribute (path[i], "BandwidthBurst", DataRateValue (DataRate (bw[i] + "B/s")));
        }
    }
  f.close ();
}


void
TorStarHelper::BuildTopology ()
{
  m_starHelper = new PointToPointStarHelper (m_nSpokes,m_p2pHelper);

  // Disable high-level traffic control
  TrafficControlHelper tch;

  //install stack
  m_stackHelper.Install (m_starHelper->GetHub ());

  //use the linux protocol stack for the spokes
  if (m_nscTcpCong.size () > 0)
    {
      string nscStack = "liblinux2.6.26.so";
      m_stackHelper.SetTcp ("ns3::NscTcpL4Protocol","Library",StringValue (nscStack));
    }

  for (int i = 0; i < m_nSpokes; ++i)
    {
      m_stackHelper.Install (m_starHelper->GetSpokeNode (i));
    }

  if (m_nscTcpCong.size () > 0)
    {
      if (m_nscTcpCong != "cubic")
        {
          Config::Set ("/NodeList/*/$ns3::Ns3NscStack<linux2.6.26>/net.ipv4.tcp_congestion_control", StringValue (m_nscTcpCong));
        }
    }

  //assign ipv4
  m_addressHelper.SetBase ("10.1.0.0", "255.255.255.0");
  m_starHelper->AssignIpv4Addresses (m_addressHelper);

  if (m_enablePcap)
    {
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

  for (int i = 0; i < m_nSpokes; ++i)
    {
      tch.Uninstall (m_starHelper->GetHub()->GetDevice(i));
      tch.Uninstall (m_starHelper->GetSpokeNode (i)->GetDevice(0));
    }

  InstallCircuits ();
}


void
TorStarHelper::InstallCircuits ()
{
  Ptr<TorBaseApp> clientApp;
  Ptr<TorBaseApp> entryApp;
  Ptr<TorBaseApp> middleApp;
  Ptr<TorBaseApp> exitApp;

  Ipv4AddressHelper ipHelper = Ipv4AddressHelper ("127.0.0.0", "255.0.0.0");

  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor e = i->second;

      if (!m_disableProxies)
        {
          clientApp = InstallTorApp (e.proxy ());
        }
      entryApp = InstallTorApp (e.entry ());
      middleApp = InstallTorApp (e.middle ());
      exitApp = InstallTorApp (e.exit ());

      Ipv4Address clientAddress;
      if (!m_disableProxies)
        {
          clientAddress = m_starHelper->GetSpokeIpv4Address (m_relays[e.proxy ()].spokeId);
        }

      Ipv4Address entryAddress  = m_starHelper->GetSpokeIpv4Address (m_relays[e.entry ()].spokeId);
      Ipv4Address middleAddress = m_starHelper->GetSpokeIpv4Address (m_relays[e.middle ()].spokeId);
      Ipv4Address exitAddress   = m_starHelper->GetSpokeIpv4Address (m_relays[e.exit ()].spokeId);
      Ipv4Address serverAddress = ipHelper.NewAddress ();

      exitApp->AddCircuit (e.id, serverAddress, SERVEREDGE, middleAddress, RELAYEDGE);
      middleApp->AddCircuit (e.id, exitAddress, RELAYEDGE, entryAddress, RELAYEDGE);
      if (!m_disableProxies)
        {
          entryApp->AddCircuit (e.id, middleAddress, RELAYEDGE, clientAddress, RELAYEDGE);
          clientApp->AddCircuit (e.id, entryAddress, RELAYEDGE, ipHelper.NewAddress (), PROXYEDGE, e.m_clientSocket);
        }
      else
        {
          entryApp->AddCircuit (e.id, middleAddress, RELAYEDGE, ipHelper.NewAddress (), PROXYEDGE, e.m_clientSocket);
        }
    }

}


Ptr<TorBaseApp>
TorStarHelper::InstallTorApp (string name)
{
  NS_ASSERT (m_relays.find (name) != m_relays.end ());
  RelayDescriptor desc = m_relays[name];
  if (m_starHelper->GetSpokeNode (desc.spokeId)->GetNApplications () == 0 )
    {
      m_starHelper->GetSpokeNode (desc.spokeId)->AddApplication (desc.tapp);
    }

  return desc.tapp;
}


Ptr<Node>
TorStarHelper::GetSpokeNode (uint32_t id)
{
  return m_starHelper->GetSpokeNode (id);
}


ApplicationContainer
TorStarHelper::GetTorAppsContainer ()
{
  return m_relayApps;
}

Ptr<TorBaseApp>
TorStarHelper::GetTorApp (string name)
{
  return m_relays[name].tapp;
}

Ptr<Node>
TorStarHelper::GetTorNode (string name)
{
  return m_starHelper->GetSpokeNode (m_relays[name].spokeId);
}

Ptr<TorBaseApp>
TorStarHelper::GetExitApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  return GetTorApp (desc.exit ());
}

Ptr<TorBaseApp>
TorStarHelper::GetMiddleApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  return GetTorApp (desc.middle ());
}

Ptr<TorBaseApp>
TorStarHelper::GetEntryApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  return GetTorApp (desc.entry ());
}

Ptr<TorBaseApp>
TorStarHelper::GetProxyApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  if (m_disableProxies)
    {
      return GetTorApp (desc.entry ());
    }
  else
    {
      return GetTorApp (desc.proxy ());
    }
}


Ptr<TorBaseApp>
TorStarHelper::CreateTorApp ()
{
  Ptr<TorBaseApp> tapp = m_factory.Create<TorBaseApp> ();
  NS_ASSERT (tapp);
  return tapp;
}


string
TorStarHelper::GetProxyName (int id)
{
  stringstream ss;
  ss << "proxy" << id;
  return ss.str ();
}

uint32_t TorStarHelper::GetBdp()
{
  // get bottleneck data rate
  DataRate bottleneck = m_underlayRate;

  map<string,RelayDescriptor>::iterator i;
  for (i = m_relays.begin (); i != m_relays.end (); ++i)
  {
    DataRateValue rate_value;
    i->second.tapp->GetAttribute("BandwidthRate", rate_value);
    DataRate rate = rate_value.Get();

    DataRateValue burst_value;
    i->second.tapp->GetAttribute("BandwidthBurst", burst_value);
    DataRate burst = burst_value.Get();

    NS_ASSERT(rate == burst);

    if(rate < bottleneck)
      bottleneck = rate;
  }

  // get delay
  Time delay = m_disableProxies ? m_underlayLinkDelay*8 : m_underlayLinkDelay*12;

  return static_cast<uint32_t>(delay.GetSeconds()/8.0 * bottleneck.GetBitRate());
}

map<string, Ptr<PointToPointNetDevice> >
TorStarHelper::GetRouterDevices ()
{
  map<string, Ptr<PointToPointNetDevice> > result;

  map<string, RelayDescriptor>::const_iterator it;
  for(it = m_relays.begin(); it != m_relays.end(); ++it)
  {
    string torname = it->first;
    RelayDescriptor relay = it->second;

    result[torname] = DynamicCast<PointToPointNetDevice> (
        m_starHelper->GetHub()->GetDevice(relay.spokeId)
    );
  }

  return result;
}

void
TorStarHelper::PrintCircuits ()
{
  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor e = i->second;
      cout << e.id << ":";
      if (!m_disableProxies)
        {
          cout << "\t" << e.proxy () << "[" << m_relays[e.proxy ()].spokeId + 1 << "]";
        }
      cout << "\t" << e.entry () << "[" << m_relays[e.entry ()].spokeId + 1 << "]";
      cout << "\t" << e.middle () << "[" << m_relays[e.middle ()].spokeId + 1 << "]";
      cout << "\t" << e.exit () << "[" << m_relays[e.exit ()].spokeId + 1 << "]";
      cout << endl;
    }
}
