#ifndef __TOR_FAIR_H__
#define __TOR_FAIR_H__

#include "tor-base.h"
#include "tor.h"
#include "cell-header.h"

namespace ns3 {

class CircuitRing {
public:
  CircuitRing();
  ~CircuitRing();

  void AddCircuit(Ptr<Circuit>);
  uint32_t Write(uint32_t);
private:
  int active_circuit;
  std::vector<Ptr<Circuit> > circuits;
};



class TorFairApp : public TorApp
{
public:
  static TypeId GetTypeId (void);
  //TorFairApp ();
  //~TorFairApp ();

  //virtual Ptr<Connection> AddConnection (Ipv4Address, int);
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
                           Ptr<PseudoClientSocket> clientSocket = 0);
  virtual void ConnWriteCallback (Ptr<Socket>, uint32_t);
  virtual void ConnReadCallback (Ptr<Socket> socket);
  CircuitRing m_circuitRing;
};






} //namespace ns3

#endif /* __TOR_FAIR_H__ */
