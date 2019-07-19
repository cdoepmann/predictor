#include "dummy-tcp.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/log.h"

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

uint32_t
TcpDummyCong::SlowStart (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  if (segmentsAcked >= 1)
    {
      tcb->m_cWnd += tcb->m_segmentSize;
      NS_LOG_INFO ("In SlowStart, updated to cwnd " << tcb->m_cWnd << " ssthresh " << tcb->m_ssThresh);
      return segmentsAcked - 1;
    }

  return 0;
}


void
TcpDummyCong::CongestionAvoidance (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  if (segmentsAcked > 0)
    {
      double adder = static_cast<double> (tcb->m_segmentSize * tcb->m_segmentSize) / tcb->m_cWnd.Get ();
      adder = std::max (1.0, adder);
      tcb->m_cWnd += static_cast<uint32_t> (adder);
      NS_LOG_INFO ("In CongAvoid, updated to cwnd " << tcb->m_cWnd <<
                   " ssthresh " << tcb->m_ssThresh);
    }
}


void
TcpDummyCong::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  if (tcb->m_cWnd < tcb->m_ssThresh)
    {
      segmentsAcked = SlowStart (tcb, segmentsAcked);
    }

  if (tcb->m_cWnd >= tcb->m_ssThresh)
    {
      CongestionAvoidance (tcb, segmentsAcked);
    }

  /* At this point, we could have segmentsAcked != 0. This because RFC says
   * that in slow start, we should increase cWnd by min (N, SMSS); if in
   * slow start we receive a cumulative ACK, it counts only for 1 SMSS of
   * increase, wasting the others.
   *
   * // Incorrect assert, I am sorry
   * NS_ASSERT (segmentsAcked == 0);
   */
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

  return std::max (2 * state->m_segmentSize, bytesInFlight / 2);
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
  tcb->m_cWnd = tcb->m_ssThresh;
  tcb->m_cWndInfl = tcb->m_ssThresh + (dupAckCount * tcb->m_segmentSize);
}

void
TcpDummyRecovery::DoRecovery (Ptr<TcpSocketState> tcb, uint32_t lastAckedBytes,
                             uint32_t lastSackedBytes)
{
  NS_LOG_FUNCTION (this << tcb << lastAckedBytes << lastSackedBytes);
  NS_UNUSED (lastAckedBytes);
  NS_UNUSED (lastSackedBytes);
  tcb->m_cWndInfl += tcb->m_segmentSize;
}

void
TcpDummyRecovery::ExitRecovery (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  // Follow NewReno procedures to exit FR if SACK is disabled
  // (RFC2582 sec.3 bullet #5 paragraph 2, option 2)
  // For SACK connections, we maintain the cwnd = ssthresh. In fact,
  // this ACK was received in RECOVERY phase, not in OPEN. So we
  // are not allowed to increase the window
  tcb->m_cWndInfl = tcb->m_ssThresh.Get ();
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

