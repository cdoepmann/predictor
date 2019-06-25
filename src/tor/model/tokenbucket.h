
#ifndef __TOKENBUCKET_H__
#define __TOKENBUCKET_H__

#include "ns3/simulator.h"
#include "ns3/data-rate.h"

namespace ns3 {


class TokenBucket
{
public:
  TokenBucket ();
  TokenBucket (DataRate,DataRate,Time);
  ~TokenBucket ();

  void StartBucket (Time = Seconds (0));
  void SetRefilledCallback (Callback<void,int64_t>);
  uint32_t GetSize ();
  void Decrement (uint32_t);
  void SetRate (DataRate, DataRate);

private:
  void Refill ();
  void RefillPartial ();

  double m_bucket;
  DataRate m_rate;
  DataRate m_burst;
  Time m_refilltime;
  EventId m_refillevent;

  Callback<void,int64_t> m_refilled;
};

} //namespace ns3

#endif /* __TOKENBUCKET_H__ */
