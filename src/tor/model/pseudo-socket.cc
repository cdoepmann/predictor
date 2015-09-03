#include "pseudo-socket.h"
#include "stdio.h"

NS_LOG_COMPONENT_DEFINE ("PseudoSocket");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (PseudoSocket);

// static TypeId PseudoSocket::GetTypeId (void) {
//   static TypeId tid = TypeId ("ns3::PseudoSocket")
//     .SetParent<Socket> ()
//   ;
//   return tid;
// }

PseudoSocket::PseudoSocket ()
{
}

enum Socket::SocketErrno
PseudoSocket::GetErrno (void) const
{
  return ERROR_NOTERROR;
}

enum Socket::SocketType
PseudoSocket::GetSocketType (void) const
{
  return NS3_SOCK_STREAM;
}

Ptr<Node>
PseudoSocket::GetNode (void) const
{
  return 0;
}

int
PseudoSocket::Bind (void)
{
  return 0;
}

int
PseudoSocket::Bind6 (void)
{
  return 0;
}

int
PseudoSocket::Bind (const Address &address)
{
  return 0;
}

int
PseudoSocket::Close (void)
{
  return 0;
}

int
PseudoSocket::ShutdownSend (void)
{
  return 0;
}

int
PseudoSocket::ShutdownRecv (void)
{
  return 0;
}

int
PseudoSocket::Connect (const Address &address)
{
  return 0;
}

int
PseudoSocket::Listen (void)
{
  return 0;
}

uint32_t
PseudoSocket::GetTxAvailable (void) const
{
  return 0;
}

int
PseudoSocket::Send (Ptr<Packet> p, uint32_t flags)
{
  return 0;
}

int
PseudoSocket::SendTo (Ptr<Packet> p, uint32_t flags, const Address &address)
{
  //TODO handle fromAddress
  return Send (p, flags);
}

uint32_t
PseudoSocket::GetRxAvailable (void) const
{
  return 0;
}

Ptr<Packet>
PseudoSocket::Recv (uint32_t maxSize, uint32_t flags)
{
  return 0;
}

Ptr<Packet>
PseudoSocket::RecvFrom (uint32_t maxSize, uint32_t flags, Address &fromAddress)
{
  //TODO handle fromAddress
  return Recv (maxSize, flags);
}

int
PseudoSocket::GetSockName (Address &address) const
{
  return 0;
}

bool
PseudoSocket::SetAllowBroadcast (bool allowBroadcast)
{
  return false;
}

bool
PseudoSocket::GetAllowBroadcast () const
{
  return false;
}


// static TypeId PseudoSinkSocket::GetTypeId (void) {
//   static TypeId tid = TypeId ("ns3::PseudoSinkSocket")
//     .SetParent<Socket> ()
//   ;
//   return tid;
// }

PseudoSinkSocket::PseudoSinkSocket ()
{
}

uint32_t
PseudoSinkSocket::GetTxAvailable (void) const
{
  return numeric_limits<uint32_t>::max ();
}

int
PseudoSinkSocket::Send (Ptr<Packet> p, uint32_t flags)
{
  if (p)
    {
      int bytesSent = p->GetSize ();
      Simulator::ScheduleNow (&PseudoSinkSocket::NotifyDataSent, this, bytesSent);
      Simulator::ScheduleNow (&PseudoSinkSocket::NotifySend, this, GetTxAvailable ());
      return bytesSent;
    }

  return -1;
}




// static TypeId PseudoBulkSocket::GetTypeId (void) {
//   static TypeId tid = TypeId ("ns3::PseudoBulkSocket")
//     .SetParent<Socket> ()
//   ;
//   return tid;
// }

PseudoBulkSocket::PseudoBulkSocket ()
{
}

uint32_t
PseudoBulkSocket::GetRxAvailable (void) const
{
  return numeric_limits<uint32_t>::max ();
}

// What about PacketTags, i.e. SocketAddressTag?
Ptr<Packet>
PseudoBulkSocket::Recv (uint32_t maxSize, uint32_t flags)
{
  Simulator::ScheduleNow (&PseudoBulkSocket::NotifyDataRecv, this);
  return Create<Packet> (maxSize);
}




PseudoServerSocket::PseudoServerSocket ()
{
  m_leftToSend = 0;
  m_leftToRead = PACKET_PAYLOAD_SIZE;
  m_request = 0;
}

uint32_t
PseudoServerSocket::GetRxAvailable () const
{
  return m_leftToSend;
}

uint32_t
PseudoServerSocket::GetTxAvailable () const
{
  if (m_leftToSend <= 0)
    {
      return m_leftToRead;
    }

  return 0;
}

Ptr<Packet>
PseudoServerSocket::Recv (uint32_t maxSize, uint32_t flags)
{
  if (m_leftToSend <= 0)
    {
      return 0;
    }

  if (maxSize >= m_leftToSend)
    {
      Ptr<Packet> p = Create<Packet> (m_leftToSend);
      m_leftToSend = 0;
      m_leftToRead = PACKET_PAYLOAD_SIZE;
      Simulator::ScheduleNow (&PseudoServerSocket::NotifySend, this, GetTxAvailable ());
      Simulator::ScheduleNow (&PseudoServerSocket::NotifyDataSent, this, 0);
      return p;
    }
  else
    {
      m_leftToSend -= maxSize;
      Simulator::ScheduleNow (&PseudoServerSocket::NotifyDataRecv, this);
      return Create<Packet> (maxSize);
    }
}

int
PseudoServerSocket::Send (Ptr<Packet> p, uint32_t flags)
{
  if (m_leftToSend > 0)
    {
      return 0;
    }

  //NS_ASSERT(p->GetSize() == PACKET_PAYLOAD_SIZE);
  //deal with fragmented requests
  if (p->GetSize () < PACKET_PAYLOAD_SIZE)
    {
      // cout << "fragment" << endl;
      if (m_request)
        {
          uint8_t data[p->GetSize () + m_request->GetSize ()];
          m_request->CopyData (data, m_request->GetSize ());
          p->CopyData (&data[m_request->GetSize ()], p->GetSize ());
          m_request = Create<Packet> (data, p->GetSize () + m_request->GetSize ());
        }
      else
        {
          m_request = p;
        }
    }
  else
    {
      m_request = p;
    }

  m_leftToRead -= p->GetSize ();

  if (m_leftToRead <= 0)
    {
      // read requested bytes
      RequestHeader h;
      m_request->PeekHeader (h);
      m_leftToSend = h.GetRequestSize ();
      Simulator::ScheduleNow (&PseudoServerSocket::NotifyDataRecv, this);
    }
  else
    {
      Simulator::ScheduleNow (&PseudoServerSocket::NotifySend, this, GetTxAvailable ());
    }

  return p->GetSize ();
}





PseudoClientSocket::PseudoClientSocket (Time startTime)
{
  //default: bulk sender
  Ptr<ConstantRandomVariable> rng_request = CreateObject<ConstantRandomVariable> ();
  rng_request->SetAttribute ("Constant", DoubleValue (pow (2,30)));
  m_requestSizeStream = rng_request;

  Ptr<ConstantRandomVariable> rng_think = CreateObject<ConstantRandomVariable> ();
  rng_think->SetAttribute ("Constant", DoubleValue (0));
  m_thinkTimeStream = rng_think;

  m_leftToSend = PACKET_PAYLOAD_SIZE;
  m_leftToRead = 0;
  ttlb_callback = 0;
  ttfb_callback = 0;
  m_ttlb_id = 0;
  m_ttfb_id = 0;

  m_startEvent = Simulator::Schedule (startTime, &PseudoClientSocket::NotifyDataRecv, this);
}


PseudoClientSocket::PseudoClientSocket (Ptr<RandomVariableStream> requestStream, Ptr<RandomVariableStream> thinkStream, Time startTime)
{
  m_leftToRead = 0;
  ttlb_callback = 0;
  ttfb_callback = 0;
  m_requestSizeStream = requestStream;
  m_thinkTimeStream = thinkStream;
  m_leftToSend = PACKET_PAYLOAD_SIZE;

  m_startEvent = Simulator::Schedule (startTime, &PseudoClientSocket::NotifyDataRecv, this);
}

void
PseudoClientSocket::SetRequestStream (Ptr<RandomVariableStream> requestStream)
{
  if (requestStream)
    {
      m_requestSizeStream = requestStream;
    }
}

void
PseudoClientSocket::SetThinkStream (Ptr<RandomVariableStream> thinkStream)
{
  if (thinkStream)
    {
      m_thinkTimeStream = thinkStream;
    }
}

void
PseudoClientSocket::Start (Time startTime)
{
  m_startEvent.Cancel ();
  m_startEvent = Simulator::Schedule (startTime, &PseudoClientSocket::NotifyDataRecv, this);
}

uint32_t
PseudoClientSocket::GetRxAvailable () const
{
  if (m_leftToRead <= 0)
    {
      return m_leftToSend;
    }

  return 0;
}

uint32_t
PseudoClientSocket::GetTxAvailable () const
{
  return m_leftToRead;
}


Ptr<Packet>
PseudoClientSocket::Recv (uint32_t maxSize, uint32_t flags)
{
  if (m_leftToRead > 0)
    {
      return 0;
    }

  NS_ASSERT (maxSize > 0);
  if (m_leftToSend == PACKET_PAYLOAD_SIZE)
    {
      // prepare new request
      RequestHeader h;
      m_requestSize = m_requestSizeStream->GetInteger ();
      m_requestSize = RoundUp (m_requestSize,PACKET_PAYLOAD_SIZE);
      h.SetRequestSize (m_requestSize);
      m_request = Create<Packet> (PACKET_PAYLOAD_SIZE - h.GetSerializedSize ());
      m_request->AddHeader (h);
    }

  uint32_t size = min (maxSize,m_request->GetSize ());
  Ptr<Packet> p = m_request->CreateFragment (0,size);
  m_request->RemoveAtStart (size);
  m_leftToSend -= size;

  if (m_request->GetSize () == 0)
    {
      m_leftToRead = m_requestSize;
    }

  m_requestSent = Simulator::Now ();
  Simulator::ScheduleNow (&PseudoClientSocket::NotifyDataRecv, this);
  return p;
}

int
PseudoClientSocket::Send (Ptr<Packet> p, uint32_t flags)
{
  if (m_leftToRead <= 0)
    {
      return 0;
    }

  if (m_leftToRead == m_requestSize)
    {
      Time ttfb = Time (Simulator::Now () - m_requestSent);
      if (ttfb_callback)
        {
          ttfb_callback (m_ttfb_id, ttfb.GetSeconds (), m_ttlb_desc);
        }
    }

  uint32_t size = p->GetSize ();
  m_leftToRead -= size;

  if (m_leftToRead <= 0)
    {
      m_leftToRead = 0;
      Time ttlb = Time (Simulator::Now () - m_requestSent);
      if (ttlb_callback)
        {
          ttlb_callback (m_ttlb_id, ttlb.GetSeconds (), m_ttlb_desc);
        }
      Simulator::Schedule (Seconds (m_thinkTimeStream->GetValue ()), &PseudoClientSocket::RequestPage, this);
    }

  Simulator::ScheduleNow (&PseudoClientSocket::NotifySend, this, GetTxAvailable ());
  Simulator::ScheduleNow (&PseudoClientSocket::NotifyDataSent, this, size);
  return size;
}


void
PseudoClientSocket::RequestPage ()
{
  m_leftToSend = PACKET_PAYLOAD_SIZE;
  Simulator::ScheduleNow (&PseudoClientSocket::NotifyDataRecv, this);
}

uint32_t
PseudoClientSocket::RoundUp (uint32_t num, uint32_t multiple)
{
  if (multiple == 0)
    {
      return num;
    }

  uint32_t rest = num % multiple;
  if (rest == 0)
    {
      return num;
    }
  return num + multiple - rest;
}



void
PseudoClientSocket::SetTtfbCallback (void (*ttfb)(int, double, string), int id, string desc)
{
  m_ttfb_id = id;
  m_ttfb_desc = desc;
  ttfb_callback = ttfb;
}

void
PseudoClientSocket::SetTtlbCallback (void (*ttlb)(int, double, string), int id, string desc)
{
  m_ttlb_id = id;
  m_ttlb_desc = desc;
  ttlb_callback = ttlb;
}



RequestHeader::RequestHeader ()
{
  m_requestSize = 0;
}

TypeId RequestHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RequestHeader")
    .SetParent<Header> ()
    .AddConstructor<RequestHeader> ()
  ;
  return tid;
}

TypeId RequestHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t RequestHeader::GetSerializedSize (void) const
{
  return 4;
}
void RequestHeader::Print (ostream &os) const   /*TODO*/
{
}
void RequestHeader::SetRequestSize (uint32_t size)
{
  m_requestSize = size;
}
uint32_t RequestHeader::GetRequestSize (void) const
{
  return m_requestSize;
}

void
RequestHeader::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteU32 (m_requestSize);
}

uint32_t
RequestHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_requestSize = i.ReadU32 ();
  return GetSerializedSize ();
}


} // namespace ns3