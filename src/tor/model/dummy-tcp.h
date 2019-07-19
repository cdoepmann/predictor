
#ifndef DUMMYTCP_H
#define DUMMYTCP_H

#include "ns3/tcp-socket-state.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-recovery-ops.h"

namespace ns3 {

class TcpDummyCong : public TcpCongestionOps
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  TcpDummyCong ();

  /**
   * \brief Copy constructor.
   * \param sock object to copy.
   */
  TcpDummyCong (const TcpDummyCong& sock);

  ~TcpDummyCong ();

  std::string GetName () const;

  virtual void IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);
  virtual uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb,
                                uint32_t bytesInFlight);

  virtual Ptr<TcpCongestionOps> Fork ();

  virtual void CongestionStateSet (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState);
  virtual void CwndEvent (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event);

};

class TcpDummyRecovery : public TcpRecoveryOps
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Constructor
   */
  TcpDummyRecovery ();

  /**
   * \brief Copy constructor.
   * \param recovery object to copy.
   */
  TcpDummyRecovery (const TcpDummyRecovery& recovery);

  /**
   * \brief Constructor
   */
  virtual ~TcpDummyRecovery () override;

  virtual std::string GetName () const override;

  virtual void EnterRecovery (Ptr<TcpSocketState> tcb, uint32_t dupAckCount,
                              uint32_t unAckDataCount, uint32_t lastSackedBytes) override;

  virtual void DoRecovery (Ptr<TcpSocketState> tcb, uint32_t lastAckedBytes,
                           uint32_t lastSackedBytes) override;

  virtual void ExitRecovery (Ptr<TcpSocketState> tcb) override;

  virtual Ptr<TcpRecoveryOps> Fork () override;
};

} // namespace ns3

#endif // DUMMYTCP_H