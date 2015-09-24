/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef __TOR_PCTCP_H__
#define __TOR_PCTCP_H__

#include "tor.h"

namespace ns3 {

class TorPctcpApp : public TorApp
{
public:
  static TypeId GetTypeId (void);
  TorPctcpApp ();
  ~TorPctcpApp ();

  virtual Ptr<Connection> AddConnection (Ipv4Address, int);

};


} /* end namespace ns3 */
#endif /* __TOR_PCTCP_H__ */
