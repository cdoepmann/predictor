#ifndef __TOR_BASE_H__
#define __TOR_BASE_H__

#include "ns3/application.h"
#include "ns3/internet-module.h"
#include "ns3/data-rate.h"

#include "pseudo-socket.h"
#include "tokenbucket.h"

#define RELAYEDGE 0 // aka speaks cells
#define PROXYEDGE 2
#define SERVEREDGE 3

#define CELL_PAYLOAD_SIZE 498

using namespace std;
using namespace ns3;

namespace ns3 {

/** Used to indicate which way a cell is going on a circuit.
  * Inbound = cell is moving torwards the client
  * Outbound = cell is moving away from the client */
enum CellDirection
{
  INBOUND, OUTBOUND
};

// TODO private
class TorBaseApp : public Application
{
public:
  static TypeId GetTypeId (void);
  TorBaseApp ();
  virtual ~TorBaseApp ();

  virtual void StartApplication (void);
  virtual void StopApplication (void);
  virtual void DoDispose (void);

  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
    Ptr<PseudoClientSocket> clientSocket=0);

  virtual void SetNodeName (std::string);
  virtual std::string GetNodeName (void);

  uint32_t m_id;
  string m_name;
  Ipv4Address m_ip;
  Address m_local;
  DataRate m_rate;
  DataRate m_burst;
  Time m_refilltime;
  TokenBucket m_writebucket;
  TokenBucket m_readbucket;
};


} /* end namespace ns3 */
#endif /* __TOR_BASE_H__ */