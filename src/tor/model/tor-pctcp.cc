#include "tor-pctcp.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorPctcpApp");
NS_OBJECT_ENSURE_REGISTERED (TorPctcpApp);


TorPctcpApp::TorPctcpApp ()
{
  //
}

TorPctcpApp::~TorPctcpApp ()
{
  //
}

TypeId
TorPctcpApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorPctcpApp")
    .SetParent<TorApp> ()
    .AddConstructor<TorPctcpApp> ();
  return tid;
}

Ptr<Connection>
TorPctcpApp::AddConnection (Ipv4Address ip, int conn_type)
{
  Ptr<Connection> conn = Create<Connection> (this, ip, conn_type);
  connections.push_back (conn);

  return conn;
}


} //namespace ns3