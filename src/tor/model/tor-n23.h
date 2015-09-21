/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef __TOR_N23_H__
#define __TOR_N23_H__

#include "tor.h"

#define N2 20
#define N3 70
#define CREDIT 23

namespace ns3 {

class N23Circuit : public Circuit
{
public:
  N23Circuit (uint16_t, Ptr<Connection>, Ptr<Connection>, int, int);
  // ~N23Circuit ();
  virtual Ptr<Packet> PopCell (CellDirection);
  virtual void PushCell (Ptr<Packet>, CellDirection);
  Ptr<Packet> CreateCredit ();
  bool IsCredit (Ptr<Packet>);
  bool IncrementCredit (CellDirection);

  int p_creditBalance;
  int n_creditBalance;
  uint32_t p_cellsforwarded;
  uint32_t n_cellsforwarded;

};

class TorN23App : public TorApp
{
public:
  static TypeId GetTypeId (void);
  TorN23App();
  ~TorN23App();

  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
                           Ptr<PseudoClientSocket> clientSocket=0);
};


} /* end namespace ns3 */
#endif /* __TOR_N23_H__ */
