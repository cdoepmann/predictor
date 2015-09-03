#ifndef PSEUDO_SOCKET_H
#define PSEUDO_SOCKET_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"

// TODO remove hard coded value
#define PACKET_PAYLOAD_SIZE 498

using namespace std;
using namespace ns3;

namespace ns3 {

class RequestHeader : public Header
{
public:
  RequestHeader ();
  // ~RequestHeader ();

  void SetRequestSize (uint32_t);
  uint32_t GetRequestSize (void) const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

private:
  uint32_t m_requestSize;
};


class PseudoSocket : public Socket
{
public:
  PseudoSocket ();
  // ~PseudoSocket();
  // static TypeId GetTypeId (void);

  enum SocketErrno GetErrno (void) const;
  enum SocketType GetSocketType (void) const;
  Ptr<Node> GetNode (void) const;
  int Bind (void);
  int Bind6 (void);
  int Bind (const Address &address);
  int Close (void);
  int ShutdownSend (void);
  int ShutdownRecv (void);
  int Connect (const Address &address);
  int Listen (void);
  uint32_t GetTxAvailable (void) const;
  int Send (Ptr<Packet> p, uint32_t flags);
  int SendTo (Ptr<Packet> p, uint32_t flags, const Address &address);
  uint32_t GetRxAvailable (void) const;
  Ptr<Packet> Recv (uint32_t maxSize, uint32_t flags);
  Ptr<Packet> RecvFrom (uint32_t maxSize, uint32_t flags, Address &fromAddress);
  int GetSockName (Address &address) const;
  bool SetAllowBroadcast (bool allowBroadcast);
  bool GetAllowBroadcast () const;

};


class PseudoSinkSocket : public PseudoSocket
{
public:
  PseudoSinkSocket ();
  // ~PseudoSinkSocket();
  // static TypeId GetTypeId (void);

  uint32_t GetTxAvailable (void) const;
  int Send (Ptr<Packet> p, uint32_t flags);

};

class PseudoBulkSocket : public PseudoSinkSocket
{
public:
  PseudoBulkSocket ();
  // ~PseudoBulkSocket();
  // static TypeId GetTypeId (void);

  uint32_t GetRxAvailable (void) const;
  Ptr<Packet> Recv (uint32_t maxSize, uint32_t flags);
};



class PseudoServerSocket : public PseudoSocket
{
public:
  PseudoServerSocket ();

  uint32_t GetTxAvailable () const;
  uint32_t GetRxAvailable () const;
  int Send (Ptr<Packet> p, uint32_t flags);
  Ptr<Packet> Recv (uint32_t maxSize, uint32_t flags);
private:
  uint32_t m_leftToSend;
  uint32_t m_leftToRead;
  Ptr<Packet> m_request;
};


class PseudoClientSocket : public PseudoSocket
{
public:
  PseudoClientSocket (Time startTime=Seconds(0.01));
  PseudoClientSocket (Ptr<RandomVariableStream>, Ptr<RandomVariableStream>, Time startTime=Seconds(0.1));

  uint32_t GetTxAvailable () const;
  uint32_t GetRxAvailable () const;
  int Send (Ptr<Packet> p, uint32_t flags);
  Ptr<Packet> Recv (uint32_t maxSize, uint32_t flags);

  void SetRequestStream (Ptr<RandomVariableStream>);
  void SetThinkStream (Ptr<RandomVariableStream>);
  void Start (Time);

  void SetTtfbCallback (void (*)(int, double, string), int, string);
  void SetTtlbCallback (void (*)(int, double, string), int, string);

private:
  void RequestPage ();
  uint32_t RoundUp (uint32_t,uint32_t);
  int m_leftToRead;
  int m_requestSize;
  int m_leftToSend;
  Ptr<Packet> m_request;
  Time m_requestSent;
  void (*ttfb_callback)(int, double, string);
  void (*ttlb_callback)(int, double, string);
  int m_ttfb_id;
  int m_ttlb_id;
  string m_ttfb_desc;
  string m_ttlb_desc;
  EventId m_startEvent;

  Ptr<RandomVariableStream> m_thinkTimeStream;
  Ptr<RandomVariableStream> m_requestSizeStream;
};


} // namespace ns3

#endif /* PSEUDO_SOCKET_H */