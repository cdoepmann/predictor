#include "dummy-tcp.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/log.h"
#include <limits>

namespace ns3 {

//
// Congestion Control
//

NS_LOG_COMPONENT_DEFINE ("DummyTcp");

NS_OBJECT_ENSURE_REGISTERED (TcpDummyCong);

TypeId
TcpDummyCong::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDummyCong")
    .SetParent<TcpCongestionOps> ()
    .SetGroupName ("Internet")
    .AddConstructor<TcpDummyCong> ()
  ;
  return tid;
}

TcpDummyCong::TcpDummyCong (void) : TcpCongestionOps ()
{
  NS_LOG_FUNCTION (this);
}

TcpDummyCong::TcpDummyCong (const TcpDummyCong& sock)
  : TcpCongestionOps (sock)
{
  NS_LOG_FUNCTION (this);
}

TcpDummyCong::~TcpDummyCong (void)
{
}

void
TcpDummyCong::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);
  NS_UNUSED (segmentsAcked);

  tcb->m_cWnd = std::numeric_limits<uint32_t>::max();
}

void
TcpDummyCong::CongestionStateSet (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState)
{
  NS_UNUSED (newState);
  tcb->m_cWnd = std::numeric_limits<uint32_t>::max();
}

void
TcpDummyCong::CwndEvent (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event)
{
  NS_UNUSED (event);
  tcb->m_cWnd = std::numeric_limits<uint32_t>::max();
}

std::string
TcpDummyCong::GetName () const
{
  return "TcpDummyCong";
}

uint32_t
TcpDummyCong::GetSsThresh (Ptr<const TcpSocketState> state,
                         uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << state << bytesInFlight);

  return std::numeric_limits<uint32_t>::max();
}

Ptr<TcpCongestionOps>
TcpDummyCong::Fork ()
{
  return CopyObject<TcpDummyCong> (this);
}

//
// Recovery
//

NS_OBJECT_ENSURE_REGISTERED (TcpDummyRecovery);

TypeId
TcpDummyRecovery::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDummyRecovery")
    .SetParent<TcpRecoveryOps> ()
    .SetGroupName ("Internet")
    .AddConstructor<TcpDummyRecovery> ()
  ;
  return tid;
}

TcpDummyRecovery::TcpDummyRecovery (void) : TcpRecoveryOps ()
{
  NS_LOG_FUNCTION (this);
}

TcpDummyRecovery::TcpDummyRecovery (const TcpDummyRecovery& sock)
  : TcpRecoveryOps (sock)
{
  NS_LOG_FUNCTION (this);
}

TcpDummyRecovery::~TcpDummyRecovery (void)
{
  NS_LOG_FUNCTION (this);
}

void
TcpDummyRecovery::EnterRecovery (Ptr<TcpSocketState> tcb, uint32_t dupAckCount,
                                uint32_t unAckDataCount, uint32_t lastSackedBytes)
{
  NS_LOG_FUNCTION (this << tcb << dupAckCount << unAckDataCount << lastSackedBytes);
  NS_UNUSED (unAckDataCount);
  NS_UNUSED (lastSackedBytes);
  NS_UNUSED (dupAckCount);
  tcb->m_cWnd = std::numeric_limits<uint32_t>::max();
}

void
TcpDummyRecovery::DoRecovery (Ptr<TcpSocketState> tcb, uint32_t lastAckedBytes,
                             uint32_t lastSackedBytes)
{
  NS_LOG_FUNCTION (this << tcb << lastAckedBytes << lastSackedBytes);
  NS_UNUSED (lastAckedBytes);
  NS_UNUSED (lastSackedBytes);
  tcb->m_cWnd = std::numeric_limits<uint32_t>::max();
}

void
TcpDummyRecovery::ExitRecovery (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  tcb->m_cWnd = std::numeric_limits<uint32_t>::max();
}

std::string
TcpDummyRecovery::GetName () const
{
  return "TcpDummyRecovery";
}

Ptr<TcpRecoveryOps>
TcpDummyRecovery::Fork ()
{
  return CopyObject<TcpDummyRecovery> (this);
}


} // namespace ns3

