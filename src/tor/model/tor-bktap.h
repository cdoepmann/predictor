#ifndef __TOR_BKTAP_H__
#define __TOR_BKTAP_H__

#include "tor-base.h"
#include "cell-header.h"

#include "ns3/point-to-point-net-device.h"

#define ACK 1
#define FWD 2
#define FDBK 12
#define NS3_SOCK_STREAM 0
#define VEGASALPHA 3
#define VEGASBETA 6

namespace ns3 {

class BktapCircuit;
class UdpChannel;


class BaseCellHeader : public Header
{
public:
  uint16_t circId;
  uint8_t cellType;
  uint8_t flags;

  BaseCellHeader ()
  {
    circId = cellType = flags = 0;
  }

  TypeId
  GetTypeId () const
  {
    static TypeId tid = TypeId ("ns3::BaseCellHeader")
      .SetParent<Header> ()
      .AddConstructor<BaseCellHeader> ()
    ;
    return tid;
  }

  TypeId
  GetInstanceTypeId () const
  {
    return GetTypeId ();
  }

  void
  Print (ostream &os) const
  {
    os << "id=" << circId;
  }

  uint32_t
  GetSerializedSize () const
  {
    return (2 + 1 + 1);
  }

  void
  Serialize (Buffer::Iterator start) const
  {
    Buffer::Iterator i = start;
    i.WriteU16 (circId);
    i.WriteU8 (cellType);
    i.WriteU8 (flags);
  }

  uint32_t
  Deserialize (Buffer::Iterator start)
  {
    Buffer::Iterator i = start;
    circId = i.ReadU16 ();
    cellType = i.ReadU8 ();
    flags = i.ReadU8 ();
    return GetSerializedSize ();
  }
};


class UdpCellHeader : public Header
{
public:
  uint16_t circId;
  uint8_t cellType;
  uint8_t flags;
  uint32_t seq;
  uint16_t streamId;
  uint8_t digest[6];
  uint16_t length;
  uint8_t cmd;

  UdpCellHeader ()
  {
    circId = cellType = flags = seq = streamId = length = cmd = 0;
  }

  TypeId
  GetTypeId () const
  {
    static TypeId tid = TypeId ("ns3::UdpCellHeader")
      .SetParent<Header> ()
      .AddConstructor<UdpCellHeader> ()
    ;
    return tid;
  }

  TypeId
  GetInstanceTypeId () const
  {
    return GetTypeId ();
  }

  void
  Print (ostream &os) const
  {
    os << "id=" << circId;
    os << " seq=" << seq;
    if (cmd == RELAY_SENDME)
      {
        os << " SENDME";
      }
  }

  uint32_t
  GetSerializedSize () const
  {
    return (4 + 4 + 2 + 6 + 2 + 1);
  }

  void
  Serialize (Buffer::Iterator start) const
  {
    Buffer::Iterator i = start;
    i.WriteU16 (circId);
    i.WriteU8 (cellType);
    i.WriteU8 (flags);
    i.WriteU32 (seq);
    i.WriteU16 (streamId);
    i.Write (digest, 6);
    i.WriteU16 (length);
    i.WriteU8 (cmd);
  }

  uint32_t
  Deserialize (Buffer::Iterator start)
  {
    Buffer::Iterator i = start;
    circId = i.ReadU16 ();
    cellType = i.ReadU8 ();
    flags = i.ReadU8 ();
    seq = i.ReadU32 ();
    streamId = i.ReadU16 ();
    i.Read (digest, 6);
    length = i.ReadU16 ();
    cmd = i.ReadU8 ();
    return GetSerializedSize ();
  }
};

class FdbkCellHeader : public Header
{
public:
  uint16_t circId;
  uint8_t cellType;
  uint8_t flags;
  uint32_t ack;
  uint32_t fwd;

  FdbkCellHeader ()
  {
    circId = flags = ack = fwd = 0;
    cellType = FDBK;
  }

  TypeId
  GetTypeId () const
  {
    static TypeId tid = TypeId ("ns3::FdbkCellHeader")
      .SetParent<Header> ()
      .AddConstructor<FdbkCellHeader> ()
    ;
    return tid;
  }

  TypeId
  GetInstanceTypeId () const
  {
    return GetTypeId ();
  }

  void
  Print (ostream &os) const
  {
    os << "id=" << circId;
    os << " ack=" << ack << " fwd=" << fwd;
    if ((flags & ACK) != 0)
      {
        os << " ACK";
      }
    if ((flags & FWD) != 0)
      {
        os << " FWD";
      }
  }

  uint32_t
  GetSerializedSize () const
  {
    return  (2 + 1 + 1 + 4 + 4);
  }

  void
  Serialize (Buffer::Iterator start) const
  {
    Buffer::Iterator i = start;
    i.WriteU16 (circId);
    i.WriteU8 (cellType);
    i.WriteU8 (flags);
    i.WriteU32 (ack);
    i.WriteU32 (fwd);
  }

  uint32_t
  Deserialize (Buffer::Iterator start)
  {
    Buffer::Iterator i = start;
    circId = i.ReadU16 ();
    cellType = i.ReadU8 ();
    flags = i.ReadU8 ();
    ack = i.ReadU32 ();
    fwd = i.ReadU32 ();
    return GetSerializedSize ();
  }
};


class SimpleRttEstimator
{
public:
  map< uint32_t,Time > rttHistory;
  set<uint32_t> retx;
  Time estimatedRtt;
  Time devRtt;
  Time currentRtt;
  Time baseRtt;
  uint32_t cntRtt;
  uint32_t rttMultiplier;

  SimpleRttEstimator ()
  {
    rttMultiplier = 1;
    estimatedRtt = Time (0);
    devRtt = Time (0);
    baseRtt = Time (Seconds (42));
    ResetCurrRtt ();
  }

  void
  SentSeq (uint32_t seq)
  {
    if (rttHistory.size () == 0 || rttHistory.rbegin ()->first + 1 == seq)
      {
        // next seq, log it.
        rttHistory[seq] = Simulator::Now ();
      }
    else
      {
        //remember es retx
        retx.insert (seq);
      }
  }

  Time
  EstimateRtt (uint32_t ack)
  {
    Time rtt = Time (0);
    if (rttHistory.find (ack - 1) != rttHistory.end ())
      {
        if (retx.find (ack - 1) == retx.end ())
          {
            rtt = Simulator::Now () - rttHistory[ack - 1];
            AddSample (rtt);
            rttMultiplier = 1;
          }
      }
    retx.erase (ack - 1);
    rttHistory.erase (ack - 1);
    return rtt;
  }

  void
  AddSample (Time rtt)
  {
    if (rtt > 0)
      {
        double alpha = 0.125;
        double beta = 0.25;
        if (estimatedRtt > 0)
          {
            estimatedRtt = (1 - alpha) * estimatedRtt + alpha * rtt;
            devRtt = (1 - beta) * devRtt + beta* Abs (rtt - estimatedRtt);
          }
        else
          {
            estimatedRtt = rtt;
          }

        baseRtt = min (baseRtt,rtt);
        currentRtt = min (rtt,currentRtt);
        ++cntRtt;
      }
  }

  void
  ResetCurrRtt ()
  {
    currentRtt = Time (Seconds (10000));
    cntRtt = 0;
  }

  Time
  Rto ()
  {
    Time rto = estimatedRtt + 4 * devRtt;
    rto = rto * rttMultiplier;
    if (rto.GetMilliSeconds () < 1000)
      {
        return Time (MilliSeconds (1000));
      }
    return rto;
  }
};




class SeqQueue : public SimpleRefCount<SeqQueue>
{
public:
  uint32_t cwnd;
  uint32_t ssthresh;
  uint32_t nextTxSeq;
  uint32_t highestTxSeq;
  uint32_t tailSeq;
  uint32_t headSeq;
  uint32_t virtHeadSeq;
  uint32_t begRttSeq;
  uint32_t dupackcnt;
  map< uint32_t, Ptr<Packet> > cellMap;

  queue<Ptr<Packet> > ackq;

  SimpleRttEstimator virtRtt;
  SimpleRttEstimator actRtt;
  EventId retxEvent;

  SeqQueue ()
  {
    cwnd = 2;
    nextTxSeq = 1;
    highestTxSeq = 0;
    tailSeq = 0;
    headSeq = 0;
    virtHeadSeq = 0;
    begRttSeq = 1;
    ssthresh = pow (2,10);
    dupackcnt = 0;
  }

  bool
  Add ( Ptr<Packet> cell, uint32_t seq )
  {
    if (tailSeq < seq)
      {
        cellMap[seq] = cell;
        uint32_t tmpSeq = tailSeq;
        while (cellMap.find (tailSeq + 1) != cellMap.end ())
          {
            ++tailSeq;
          }

        if (headSeq == 0)
          {
            headSeq = virtHeadSeq = cellMap.begin ()->first;
          }

        if (tmpSeq < tailSeq)
          {
            return true;
          }
      }
    return false;
  }

  Ptr<Packet>
  GetCell (uint32_t seq)
  {
    Ptr<Packet> cell;
    if (cellMap.find (seq) != cellMap.end ())
      {
        cell = cellMap[seq];
      }
    return cell;
  }

  Ptr<Packet>
  GetNextCell ()
  {
    Ptr<Packet> cell;
    if (cellMap.find (nextTxSeq) != cellMap.end ())
      {
        cell = cellMap[nextTxSeq];
        ++nextTxSeq;
      }

    if (highestTxSeq < nextTxSeq)
      {
        highestTxSeq = nextTxSeq - 1;
      }

    return cell;
  }

  void
  DiscardUpTo (uint32_t seq)
  {
    while (cellMap.find (seq - 1) != cellMap.end ())
      {
        cellMap.erase (seq - 1);
        ++headSeq;
        --seq;
      }

    if (headSeq > nextTxSeq)
      {
        nextTxSeq = headSeq;
      }
  }

  uint32_t
  VirtSize ()
  {
    int diff = tailSeq - virtHeadSeq;
    return diff < 0 ? 0 : diff;
  }

  uint32_t
  Size ()
  {
    int diff = tailSeq - headSeq;
    return diff < 0 ? 0 : diff;
  }

  uint32_t
  Window ()
  {
    return cwnd - Inflight ();
  }

  uint32_t
  Inflight ()
  {
    return nextTxSeq - virtHeadSeq - 1;
  }

};



class UdpChannel : public SimpleRefCount<UdpChannel>
{
public:
  UdpChannel ();
  UdpChannel (Address,int);

  void SetSocket (Ptr<Socket>);
  uint8_t GetType ();
  bool SpeaksCells ();

  Ptr<Socket> m_socket;
  Address m_remote;
  uint8_t m_conntype;
  list<Ptr<BktapCircuit> > circuits;
  SimpleRttEstimator rttEstimator;
};




class BaseCircuit : public SimpleRefCount<BaseCircuit>
{
public:
  BaseCircuit ();
  BaseCircuit (uint32_t);
  virtual ~BaseCircuit ();

  uint32_t GetId ();
  CellDirection GetOppositeDirection (CellDirection direction);

  uint32_t GetBytesRead (CellDirection);
  uint32_t GetBytesWritten (CellDirection);
  void IncrementStats (CellDirection,uint32_t,uint32_t);
  void ResetStats ();

private:
  uint32_t m_id;

  uint32_t stats_p_bytes_read;
  uint32_t stats_p_bytes_written;
  uint32_t stats_n_bytes_read;
  uint32_t stats_n_bytes_written;
};


class BktapCircuit : public BaseCircuit
{
public:
  BktapCircuit (uint32_t);
  // ~BktapCircuit();

  Ptr<UdpChannel> inbound;
  Ptr<UdpChannel> outbound;

  Ptr<SeqQueue> inboundQueue;
  Ptr<SeqQueue> outboundQueue;

  CellDirection GetDirection (Ptr<UdpChannel>);
  Ptr<SeqQueue> GetQueue (CellDirection);
  Ptr<UdpChannel> GetChannel (CellDirection direction);
};




class TorBktapApp : public TorBaseApp
{
public:
  static TypeId GetTypeId (void);
  TorBktapApp ();
  ~TorBktapApp ();

  virtual void StartApplication (void);
  // virtual void StopApplication (void);
  virtual void DoDispose (void);
  void RefillReadCallback (int64_t);


  Ptr<UdpChannel> AddChannel (Address, int);
  Ptr<BktapCircuit> GetCircuit (uint32_t);
  Ptr<BktapCircuit> GetNextCircuit ();
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
                           Ptr<PseudoClientSocket> clientSocket=0);

  Ptr<Socket> m_socket;

  map<Address,Ptr<UdpChannel> > channels;
  map<uint16_t,Ptr<BktapCircuit> > circuits;
  map<uint16_t,Ptr<BktapCircuit> >::iterator circit;

  void ReadCallback (Ptr<Socket>);
  uint32_t ReadFromEdge (Ptr<Socket>);
  uint32_t ReadFromRelay (Ptr<Socket>);
  void PackageRelayCell (Ptr<BktapCircuit>, CellDirection, Ptr<Packet>);
  void ReceivedRelayCell (Ptr<BktapCircuit>, CellDirection, Ptr<Packet>);
  void ReceivedAck (Ptr<BktapCircuit>, CellDirection, Ptr<Packet>);
  void ReceivedFwd (Ptr<BktapCircuit>, CellDirection, Ptr<Packet>);
  void CongestionAvoidance (Ptr<SeqQueue>, Time);
  Ptr<UdpChannel> LookupChannel (Ptr<Socket>);

  void WriteCallback ();
  uint32_t FlushPendingCell (Ptr<BktapCircuit>, CellDirection,bool = false);
  void SendEmptyAck (Ptr<BktapCircuit>, CellDirection, uint8_t, uint32_t);
  void ScheduleRto (Ptr<BktapCircuit>, CellDirection, bool = false);
  void Rto (Ptr<BktapCircuit>, CellDirection);

  EventId writeevent;
  EventId readevent;
  Ptr<Queue> m_devQ;
  uint32_t m_devQlimit;
};


} /* end namespace ns3 */
#endif /* __TOR_BKTAP_H__ */
