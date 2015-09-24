
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

namespace ns3 {

class TorStarHelper
{
public:
  TorStarHelper ();
  ~TorStarHelper ();

  void ParseFile (string, uint32_t m = 0);
  void AddCircuit (int, string, string, string, Ptr<PseudoClientSocket> clientSocket = 0);
  void SetRelayAttribute (string, string, const AttributeValue &value);
  void SetStartTimeStream (Ptr<RandomVariableStream>);
  void SetRtt (Time);
  void DisableProxies (bool);
  void EnablePcap (bool);
  void EnableNscStack (bool,string = "cubic");
  void SetTorAppType (string);
  void BuildTopology ();
  void PrintCircuits ();

  ApplicationContainer GetTorAppsContainer ();

  Ptr<Node> GetSpokeNode (uint32_t);
  Ptr<Node> GetTorNode (string);
  Ptr<TorBaseApp> GetTorApp (string);
  Ptr<TorBaseApp> GetExitApp (int);
  Ptr<TorBaseApp> GetMiddleApp (int);
  Ptr<TorBaseApp> GetEntryApp (int);
  Ptr<TorBaseApp> GetProxyApp (int);

  string GetProxyName (int);

  vector<int> circuitIds;

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
