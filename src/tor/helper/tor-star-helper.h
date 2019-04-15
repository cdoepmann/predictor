
#ifndef TORSTARHELPER_H
#define TORSTARHELPER_H

#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-star.h"
#include "ns3/global-route-manager.h"
#include "ns3/tor-module.h"

#include <map>
#include <vector>

namespace ns3 {

class TorStarHelper
{
public:
  TorStarHelper ();
  ~TorStarHelper ();

  void ParseFile (string, uint32_t m = 0);
  void AddCircuit (int, string, string, string, Ptr<PseudoClientSocket> clientSocket = 0);
  void SetRelayAttribute (string, string, const AttributeValue &value);
  void SetAllRelaysAttribute (string, const AttributeValue &value);
  void SetStartTimeStream (Ptr<RandomVariableStream>);
  void RegisterTtfbCallback (void (*)(int, double, string), string);
  void RegisterTtlbCallback (void (*)(int, double, string), string);
  void RegisterRecvCallback (void (*)(int, uint32_t, string), string);
  void SetRtt (Time);
  void DisableProxies (bool);
  void EnablePcap (bool);
  void EnableNscStack (bool,string = "cubic");
  void SetTorAppType (string);
  void BuildTopology ();
  void PrintCircuits ();

  // Set the delay between a single nod and the router, overriding the default
  // RTT. Note that this is only for this specific node, so the given time is
  // twice the underlay link delay, not four times, as is the case for the
  // default RTT which does not count from node to router, but from node to the
  // next node.
  void SetNodeRtt (string, Time);

  ApplicationContainer GetTorAppsContainer ();

  Ptr<Node> GetSpokeNode (uint32_t);
  Ptr<Node> GetTorNode (string);
  Ptr<TorBaseApp> GetTorApp (string);
  Ptr<TorBaseApp> GetExitApp (int);
  Ptr<TorBaseApp> GetMiddleApp (int);
  Ptr<TorBaseApp> GetEntryApp (int);
  Ptr<TorBaseApp> GetProxyApp (int);

  string GetProxyName (int);

  // Return the BDP of the current setup for one circuit in bytes.
  uint32_t GetBdp(int);

  // Get the hub net devices. Returns a map that assigns each tor node name the
  // respective net device used to connect to it from the router (hub)
  map<string, Ptr<PointToPointNetDevice> > GetRouterDevices ();

  // Set the default underlay link data rate
  void SetUnderlayRate (DataRate);

  // Get the relay names that form a given circuit
  vector<string> GetCircuitRelays (int circuit);

  // Get theoretical maximum data rate for a circuit
  DataRate GetCircuitDataRate (int circuit, vector<string> relays = vector<string>());

  // Get theoretical end-to-end RTT for a circuit
  Time GetCircuitRtt (int circuit, vector<string> relays = vector<string>(), bool one_way = false);

  // Get theoretical RTT between two neighboring relays
  Time GetHopRtt (string from_relay, string to_relay);

  // Get the underlay link delay with which a relay is connected to the router
  Time GetRelayDelay (string relay);

  // Get theoretically optimal cwnd for a BackTap circuit
  uint32_t GetOptimalBktapCircuitCwnd (int circuit);

  // Get theoretically optimal cwnd for a BackTap circuit
  uint32_t GetOptimalBktapBacklog (int circuit);

  // Get the resulting circuits
  std::map <int, std::vector<std::string> > GetCircuitRelays ();

  // Set maximum factor around each link delay
  void SetLinkFuzziness (double);

  vector<int> circuitIds;

protected:
  DataRate m_underlayRate;
  Time m_underlayLinkDelay;
  map<string, Time> m_specificLinkDelays;

  // Set a link property after the topology was built
  void SetLinkProperty (string node, string property, const AttributeValue& value);

private:
  class CircuitDescriptor
  {
public:
    CircuitDescriptor ()
    {
    }
    CircuitDescriptor (int id, string _proxy, string _entry, string _middle, string _exit)
    {
      this->id = id;
      this->path[0] = _proxy;
      this->path[1] = _entry;
      this->path[2] = _middle;
      this->path[3] = _exit;
      this->m_clientSocket = 0;
    }

    CircuitDescriptor (int id, string _proxy, string _entry, string _middle, string _exit,
                       Ptr<PseudoClientSocket> clientSocket)
    {
      this->id = id;
      this->path[0] = _proxy;
      this->path[1] = _entry;
      this->path[2] = _middle;
      this->path[3] = _exit;
      this->m_clientSocket = clientSocket;
    }


    string proxy ()
    {
      return path[0];
    }
    string entry ()
    {
      return path[1];
    }
    string middle ()
    {
      return path[2];
    }
    string exit ()
    {
      return path[3];
    }

    int id;
    string path[4];
    Ptr<PseudoClientSocket> m_clientSocket;
  };

  class RelayDescriptor
  {
public:
    RelayDescriptor ()
    {
    }
    RelayDescriptor (string name, int spokeId, Ptr<TorBaseApp> app)
    {
      this->spokeId = spokeId;
      tapp = app;
      tapp->SetNodeName (name);
    }

    Ptr<TorBaseApp> tapp;
    int spokeId;
  };


  Ptr<TorBaseApp> CreateTorApp ();
  void AddRelay (string);
  void InstallCircuits ();
  Ptr<TorBaseApp> InstallTorApp (string);

  map<int,CircuitDescriptor> m_circuits;
  map<string, RelayDescriptor> m_relays;

  void MakeLinkDelaysFuzzy ();
  double m_fuzziness;

  int m_nSpokes;
  bool m_disableProxies;
  bool m_enablePcap;
  std::string m_nscTcpCong;
  std::string m_torAppType;
  Ptr<RandomVariableStream> m_startTimeStream;
  Ptr<UniformRandomVariable> m_rng;

  ApplicationContainer m_relayApps;
  InternetStackHelper m_stackHelper;
  Ipv4AddressHelper m_addressHelper;
  PointToPointHelper m_p2pHelper;
  PointToPointStarHelper *m_starHelper;
  ObjectFactory m_factory;
};


} //end namespace ns3


#endif
