
#ifndef TORDUMBBELLHELPER_H
#define TORDUMBBELLHELPER_H

#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-dumbbell.h"
#include "ns3/global-route-manager.h"
#include "ns3/tor-module.h"

namespace ns3 {

class TorDumbbellHelper
{
public:
  TorDumbbellHelper ();
  ~TorDumbbellHelper ();

  void DisableProxies (bool);
  void EnableNscStack (bool,string = "cubic");
  void SetTorAppType (string);
  void ParseFile (string,uint32_t = 0,double = 0.05);
  void SetStartTimeStream (Ptr<RandomVariableStream>);
  void RegisterTtfbCallback (void (*)(int, double, string));
  void RegisterTtlbCallback (void (*)(int, double, string));

  void BuildTopology ();
  void PrintCircuits ();

  ApplicationContainer GetTorAppsContainer ();

  Ptr<Node> GetNode (string,uint32_t);
  Ptr<Node> GetNode (string);
  Ipv4Address GetIp (string,uint32_t);
  Ipv4Address GetIp (string);
  Ptr<TorBaseApp> GetTorApp (string);
  Ptr<TorBaseApp> GetExitApp (int);
  Ptr<TorBaseApp> GetMiddleApp (int);
  Ptr<TorBaseApp> GetEntryApp (int);
  Ptr<TorBaseApp> GetProxyApp (int);

  string GetProxyName (int);
  string GetCircuitTypehint (int);

  vector<int> circuitIds;

private:
  class CircuitDescriptor
  {
public:
    CircuitDescriptor () {}
    CircuitDescriptor (int id, string _proxy, string _entry, string _middle, string _exit, string typehint,
                       Ptr<PseudoClientSocket> clientSocket)
    {
      this->id = id;
      this->path[0] = _proxy;
      this->path[1] = _entry;
      this->path[2] = _middle;
      this->path[3] = _exit;
      this->m_clientSocket = clientSocket;
      this->m_typehint = typehint;
    }

    string proxy () { return path[0]; }
    string entry () { return path[1]; }
    string middle () { return path[2]; }
    string exit () { return path[3]; }

    int id;
    string path[5];
    Ptr<PseudoClientSocket> m_clientSocket;
    string m_typehint;
  };

  class RelayDescriptor
  {
public:
    RelayDescriptor () {}
    RelayDescriptor (string name, string continent, int spokeId, Ptr<TorBaseApp> app)
    {
      this->spokeId = spokeId;
      this->continent = continent;
      tapp = app;
      tapp->SetNodeName (name);
    }

    Ptr<TorBaseApp> tapp;
    string continent;
    int spokeId;
  };

  void AddCircuit (int,string, string, string, string);
  void SetRelayAttribute (string, string, const AttributeValue &value);

  Ptr<TorBaseApp> CreateTorApp ();

  void AddRelay (string,string = "");

  void InstallCircuits ();
  Ptr<TorBaseApp> InstallTorApp (string);

  map<int,CircuitDescriptor> m_circuits;
  map<string, RelayDescriptor> m_relays;

  bool m_disableProxies;

  ApplicationContainer m_relayApps;

  std::string m_nscTcpCong;
  InternetStackHelper m_stackHelper;

  std::string m_torAppType;
  ObjectFactory m_factory;

  Ptr<UniformRandomVariable> m_rng;

  Ptr<ConstantRandomVariable> m_bulkRequest;
  Ptr<ConstantRandomVariable> m_bulkThink;
  Ptr<ConstantRandomVariable> m_clientRequest;
  Ptr<UniformRandomVariable> m_clientThink;
  Ptr<RandomVariableStream> m_startTimeStream;

  PointToPointDumbbellHelper *m_dumbbellHelper;

  int m_nLeftLeaf;
  int m_nRightLeaf;

  Ipv4AddressHelper m_leftIp;
  Ipv4AddressHelper m_rightIp;
  Ipv4AddressHelper m_routerIp;

  PointToPointHelper m_p2pLeftHelper;
  PointToPointHelper m_p2pRightHelper;
  PointToPointHelper m_p2pRouterHelper;

  Ptr<EmpiricalRandomVariable> m_owdLeft;
  Ptr<EmpiricalRandomVariable> m_owdRight;
  Ptr<EmpiricalRandomVariable> m_owdRouter;

};


} //end namespace ns3


#endif
