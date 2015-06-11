
#ifndef PATHHELPER_H
#define PATHHELPER_H

#include <string>
#include <iostream>
#include <fstream>
#include <vector>

#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-star.h"
#include "ns3/global-route-manager.h"
#include "ns3/tor-module.h"

using namespace std;
using namespace ns3;


namespace ns3 {

class PathHelper
{
public:
    PathHelper();
    ~PathHelper();

    void ParseFile(string);
    void AddCircuit(int,string, string, string);
    void AddCircuit(int, string, string, string, Ptr<RandomVariableStream>, Ptr<RandomVariableStream>);
    void SetRelayAttribute(string, string, const AttributeValue &value);
    void SetRtt(Time);
    void DisableProxies(bool);
    void EnableBulkSockets(bool);
    void EnablePcap(bool);
    void EnableNscStack(bool,string="cubic");
    void SetTorAppType(string);

    void BuildTopology();
    void PrintCircuits();

    ApplicationContainer GetTorAppsContainer();
    ApplicationContainer GetServersContainer();

    Ptr<Node> GetSpokeNode(uint32_t);
    Ptr<Node> GetTorNode(string);
    Ptr<TorBaseApp> GetTorApp(string);
    Ptr<TorBaseApp> GetExitApp(int);
    Ptr<TorBaseApp> GetMiddleApp(int);
    Ptr<TorBaseApp> GetEntryApp(int);
    Ptr<TorBaseApp> GetProxyApp(int);

    string GetProxyName(int);
    string GetServerName(int);

    vector<int> circuitIds;

private:
    class CircuitDescriptor {
    public:
        CircuitDescriptor(){}
        CircuitDescriptor(int id, string _proxy, string _entry, string _middle, string _exit, string _server){
            this->id = id;
            this->path[0] = _proxy;
            this->path[1] = _entry;
            this->path[2] = _middle;
            this->path[3] = _exit;
            this->path[4] = _server;
            this->m_rng_request = 0;
            this->m_rng_think = 0;
        }

        CircuitDescriptor(int id, string _proxy, string _entry, string _middle, string _exit, string _server,
                Ptr<RandomVariableStream> rng_request, Ptr<RandomVariableStream> rng_think){
            this->id = id;
            this->path[0] = _proxy;
            this->path[1] = _entry;
            this->path[2] = _middle;
            this->path[3] = _exit;
            this->path[4] = _server;
            this->m_rng_request = rng_request;
            this->m_rng_think = rng_think;
        }

        string proxy()  { return path[0]; }
        string entry()  { return path[1]; }
        string middle() { return path[2]; }
        string exit()   { return path[3]; }
        string server() { return path[4]; }


        int id;
        string path[5];
        Ptr<RandomVariableStream> m_rng_request;
        Ptr<RandomVariableStream> m_rng_think;
    };

    class RelayDescriptor {
    public:
        RelayDescriptor(){}
        RelayDescriptor(string name, int spokeId, Ptr<TorBaseApp> app) {
            this->spokeId = spokeId;
            tapp = app;
            tapp->SetNodeName(name);
        }

        Ptr<TorBaseApp> tapp;
        int spokeId;
    };


    Ptr<TorBaseApp> CreateTorApp();

    void AddServer(string);
    void AddRelay(string);

    void InstallCircuits();
    Ptr<TorBaseApp> InstallTorApp(string);

    map<int,CircuitDescriptor> m_circuits;
    map<string, RelayDescriptor> m_relays;
    map<string, int> m_server;

    int m_nSpokes;
    bool m_disableProxies;
    bool m_enablePcap;
    bool m_enableBulkSockets;
    std::string m_nscTcpCong;

    std::string m_torAppType;

    ApplicationContainer m_relayApps;
    ApplicationContainer m_serverApps;

    InternetStackHelper m_stackHelper;
    Ipv4AddressHelper m_addressHelper;
    PointToPointHelper m_p2pHelper;
    PointToPointStarHelper *m_starHelper;
    ObjectFactory m_factory;
};


} //end namespace ns3


#endif
