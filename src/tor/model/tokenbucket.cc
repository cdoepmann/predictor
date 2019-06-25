
#include "tokenbucket.h"
#include "ns3/log.h"

using namespace std;
namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TokenBucket");

TokenBucket::TokenBucket ()
{
  NS_LOG_FUNCTION (this);
}

TokenBucket::TokenBucket (DataRate rate, DataRate burst, Time refilltime)
{
  NS_LOG_FUNCTION (this);
  m_rate = rate;
  m_burst = burst;
  m_refilltime = refilltime;
}

TokenBucket::~TokenBucket ()
{
  NS_LOG_FUNCTION (this);
  Simulator::Remove (m_refillevent);
}

void
TokenBucket::SetRefilledCallback (Callback<void,int64_t> cb)
{
  m_refilled = cb;
}

void
TokenBucket::StartBucket (Time offset)
{
  m_bucket = m_burst.GetBitRate () * m_refilltime.GetSeconds () / 8.0;
  m_refillevent = Simulator::Schedule (offset,&TokenBucket::Refill, this);
}

uint32_t
TokenBucket::GetSize ()
{
  if (m_bucket <= 0)
    {
      return 0;
    }
  return static_cast<uint32_t> (m_bucket);
}

void
TokenBucket::Decrement (uint32_t n)
{
  m_bucket -= static_cast<double> (n);
}

void
TokenBucket::SetRate (DataRate rate, DataRate burst)
{
  m_rate = rate;
  m_burst = burst;

  // Refill the bucket to the new rate, but take into account that some time
  // of the current bucket interval may have passed already.
  Refill ();
}

void
TokenBucket::RefillPartial ()
{
  double interval = (TimeStep(m_refillevent.GetTs()) - Simulator::Now()).GetSeconds ();
  int64_t prev_bucket = GetSize ();
  double rate = m_rate.GetBitRate () * interval / 8.0;
  double burst = m_burst.GetBitRate () * interval / 8.0;
  m_bucket += rate;
  if (burst < m_bucket)
    {
      m_bucket = burst;
    }

  if (!m_refilled.IsNull ())
    {
      m_refilled (prev_bucket);
    }

  m_refillevent = Simulator::Schedule (m_refilltime,&TokenBucket::Refill, this);
}

void
TokenBucket::Refill ()
{
  int64_t prev_bucket = GetSize ();
  double rate = m_rate.GetBitRate () * m_refilltime.GetSeconds () / 8.0;
  double burst = m_burst.GetBitRate () * m_refilltime.GetSeconds () / 8.0;
  m_bucket += rate;
  if (burst < m_bucket)
    {
      m_bucket = burst;
    }

  if (!m_refilled.IsNull ())
    {
      m_refilled (prev_bucket);
    }

  m_refillevent = Simulator::Schedule (m_refilltime,&TokenBucket::Refill, this);
}

} //namespace ns3
