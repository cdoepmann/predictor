#ifndef __TOR_PREDICTOR_H__
#define __TOR_PREDICTOR_H__

#include "tor-base.h"
#include "cell-header.h"
#include "pytalk.h"

#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"

namespace ns3 {

// TODO: do not define here
#define CELL_PAYLOAD_SIZE 498
#define CELL_NETWORK_SIZE 512

struct pred_buf_t
{
  uint32_t size; // How many bytes is this buffer holding right now?
  uint8_t data[CELL_NETWORK_SIZE]; //Left-over chunk, or NULL for none.
};

class PredCircuit;
class PredConnection;
class TorPredApp;
class PredController;


class PredCircuit : public BaseCircuit
{
public:
  PredCircuit (uint16_t, Ptr<PredConnection>, Ptr<PredConnection>);
  ~PredCircuit ();
  void DoDispose ();

  virtual Ptr<Packet> PopCell (CellDirection);
  virtual void PushCell (Ptr<Packet>, CellDirection);
  queue<Ptr<Packet> >* GetQueue (CellDirection);
  uint32_t GetQueueSize (CellDirection);

  Ptr<PredConnection> GetConnection (CellDirection);
  Ptr<PredConnection> GetOppositeConnection (CellDirection);
  Ptr<PredConnection> GetOppositeConnection (Ptr<PredConnection>);
  CellDirection GetDirection (Ptr<PredConnection>);
  CellDirection GetOppositeDirection (Ptr<PredConnection>);

  Ptr<PredCircuit> GetNextCirc (Ptr<PredConnection>);
  void SetNextCirc (Ptr<PredConnection>, Ptr<PredCircuit>);
   
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("PredCircuit")
      .SetParent (BaseCircuit::GetTypeId())
      ;
    return tid;
  }

protected:
  Ptr<Packet> PopQueue (queue<Ptr<Packet> >*);

  queue<Ptr<Packet> > *p_cellQ;
  queue<Ptr<Packet> > *n_cellQ;

  //Next circuit in the doubly-linked ring of circuits waiting to add cells to {n,p}_conn.
  Ptr<PredCircuit> next_active_on_n_conn;
  Ptr<PredCircuit> next_active_on_p_conn;

  Ptr<PredConnection> p_conn;   /* The OR connection that is previous in this circuit. */
  Ptr<PredConnection> n_conn;   /* The OR connection that is next in this circuit. */
};

class PredConnection : public SimpleRefCount<PredConnection>
{
public:
  PredConnection (TorPredApp*, Ipv4Address, int);
  ~PredConnection ();

  Ptr<PredCircuit> GetActiveCircuits ();
  void SetActiveCircuits (Ptr<PredCircuit>);
  uint8_t GetType ();
  bool SpeaksCells ();
  uint32_t Read (vector<Ptr<Packet> >*, uint32_t);
  uint32_t Write (uint32_t);
  void ScheduleWrite (Time = Seconds (0));
  void ScheduleRead (Time = Seconds (0));
  Ptr<Socket> GetSocket ();
  void SetSocket (Ptr<Socket>);
  Ipv4Address GetRemote ();
  uint32_t GetOutbufSize ();
  uint32_t GetInbufSize ();

  static void RememberName (Ipv4Address, string);
  string GetRemoteName ();
  TorPredApp * GetTorApp() { return torapp; }

  // Get the current RTT estimate or 0 if there is none
  Time EstimateRtt ();

  // Get the number of bytes currently in transit. This differs from "BytesInFlight"
  // in that we do not want to count retransmissions etc., but focus on the
  // high-level progress of the "reliable byte stream" service.
  uint32_t GetBytesInTransit ();

  // Get the (theoretical) base delay to the remote site
  Time GetBaseRtt ();

private:
  TorPredApp* torapp;
  Ipv4Address remote;
  Ptr<Socket> m_socket;

  // make the RTT estimation accessible
  Ptr<RttEstimator> rtt_estimator;

  // Highest sequence number sent by the socket, used for calculating the
  // sequence number range that is currently in transit. We do this in order to
  // avoid counting retransmissions etc. of the TCP logic.
  SequenceNumber32 max_sent_seq;

  // callback to update the maximum seq number sent so far
  void UpdateMaxSentSeq (SequenceNumber32 old_value, SequenceNumber32 new_value);

  // Get the highest sequence number that was transmitted over the underlying socket
  SequenceNumber32 GetHighestTxSeq () { return max_sent_seq; }

  pred_buf_t inbuf; /**< Buffer holding left over data read over this connection. */
  pred_buf_t outbuf; /**< Buffer holding left over data to write over this connection. */

  uint8_t m_conntype;

  // Linked ring of circuits
  Ptr<PredCircuit> active_circuits;

  EventId read_event;
  EventId write_event;

  static map<Ipv4Address, string> remote_names;
};





class TorPredApp : public TorBaseApp
{
public:
  static TypeId GetTypeId (void);
  TorPredApp ();
  virtual ~TorPredApp ();
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
                           Ptr<PseudoClientSocket> clientSocket = 0);

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  Ptr<PredCircuit> GetCircuit (uint16_t);

  virtual Ptr<PredConnection> AddConnection (Ipv4Address, int);
  void AddActiveCircuit (Ptr<PredConnection>, Ptr<PredCircuit>);

// private:
  void HandleAccept (Ptr<Socket>, const Address& from);

  virtual void ConnReadCallback (Ptr<Socket>);
  virtual void ConnWriteCallback (Ptr<Socket>, uint32_t);
  void PackageRelayCell (Ptr<PredConnection> conn, Ptr<Packet> data);
  void PackageRelayCellImpl (uint16_t, Ptr<Packet>);
  void ReceiveRelayCell (Ptr<PredConnection> conn, Ptr<Packet> cell);
  void AppendCellToCircuitQueue (Ptr<PredCircuit> circ, Ptr<Packet> cell, CellDirection direction);
  Ptr<PredCircuit> LookupCircuitFromCell (Ptr<Packet>);
  void RefillReadCallback (int64_t);
  void RefillWriteCallback (int64_t);
  void GlobalBucketsDecrement (uint32_t num_read, uint32_t num_written);
  uint32_t RoundRobin (int base, int64_t bucket);
  Ptr<PredConnection> LookupConn (Ptr<Socket>);

  Ptr<Socket> listen_socket;
  vector<Ptr<PredConnection> > connections;
  map<uint16_t,Ptr<PredCircuit> > circuits;

  // Remember which connection is the next to read from (or write to).
  // Previous operations did not continue because the token bucket ran empty.
  Ptr<PredConnection> m_scheduleReadHead;
  Ptr<PredConnection> m_scheduleWriteHead;

  // Callback to trigger after a new socket is established
  TracedCallback<Ptr<TorBaseApp>, // this app
                 CellDirection,   // the direction of the new socket
                 Ptr<Socket>      // the new socket itself
                 > m_triggerNewSocket;
  typedef void (* TorNewSocketCallback) (Ptr<TorBaseApp>, CellDirection, Ptr<Socket>);

  // Callback to trigger after a new pseudo server socket is added
  TracedCallback<Ptr<TorBaseApp>, // this app
                 int,              // circuit id
                 Ptr<PseudoServerSocket>      // the new pseudo socket itself
                 > m_triggerNewPseudoServerSocket;
  typedef void (* TorNewPseudoServerSocketCallback) (Ptr<TorBaseApp>, int, Ptr<PseudoServerSocket>);

  // The controller used for our optimization-based scheduling
  Ptr<PredController> controller;

protected:
  virtual void DoDispose (void);

};


// The interface to our model-predictive controller
class PredController : public Object {
public:
  PredController (Ptr<TorPredApp> app) : 
    app{app},
    horizon{20},
    time_step{MilliSeconds(10)},
    pyscript{"python3 /home/christoph/nstor/src/tor/model/solver.py"}
  {};
  // virtual ~PredController ();
  
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("PredController")
      .SetParent (Object::GetTypeId());
    return tid;
  }

  // Remember a connection that we receive data from
  void AddInputConnection(Ptr<PredConnection>);

  // Remember a connection that we send data to
  void AddOutputConnection(Ptr<PredConnection>);

  // Setup the optimizer. This requires that all connections/circuits have
  // been added before.
  void Setup();

  // Carry out the regular optimization step, based on current local information
  // as well as information provided by our neighboring relays.
  void Optimize();

  // Get the number of steps in the horizon
  size_t Horizon () { return horizon; };

  // Get the time step per horizon element
  Time TimeStep () { return time_step; };

protected:
  // The application this controller belongs to
  Ptr<TorPredApp> app;

  // The number of steps in the horizon
  size_t horizon;

  // The time step per horizon element
  Time time_step;

  // The connections that are used for receiving and sending data
  set<Ptr<PredConnection>> in_conns;
  set<Ptr<PredConnection>> out_conns;

  // Interface for talking to the python component
  PyScript pyscript;

  // Convert between DataRate and packets/s
  static double to_packets_sec(DataRate rate);
  static DataRate to_datarate(double pps);
};

struct time_out_of_range;

class Trajectory {
public:
  Trajectory(Ptr<PredController> controller, Time first_time) : time_step{controller->TimeStep()}, first_time{first_time} {};
  Trajectory(Time time_step, Time first_time) : time_step{time_step}, first_time{first_time} {};

  // Interpolate this trajectory to fit the time of its first element to the given time
  Trajectory InterpolateToTime (Time target_time);

  // Get a reference to the elements
  vector<double>& Elements() { return elements; }

  // Get the number of steps in this trajectory
  size_t Steps () { return elements.size(); };

  // Get the time of a given trajectory element, defaulting to the first one
  Time GetTime(size_t element = 0, bool allow_out_of_bounds = false)
  {
    if(!allow_out_of_bounds)
      NS_ASSERT(element <= Steps());
    
    return first_time + element * time_step;
  }

  // Get the time of the last stored element
  Time LastTime() 
  {
    NS_ASSERT(Steps() > 0);
    return GetTime(Steps() - 1);
  }

  // Get the value of the trajectory at a given time
  double GetValue(Time time)
  {
    NS_ASSERT(time >= GetTime());
    NS_ASSERT(time <= LastTime());

    size_t index = static_cast<size_t>((time - GetTime()) / time_step);
    NS_ASSERT(index >= 0);
    NS_ASSERT(index < Steps());

    return elements[index];
  }

  // Get the value of the trajectory at a given time
  double GetValueFailsafe(Time time, double lower = 0.0)
  {
    if(time < GetTime())
      return lower;
    
    if(time > LastTime())
    {
      NS_ASSERT (Steps() > 0);
      return elements[Steps() - 1];
    }

    return GetValue(time);
  }

  // Based on the given time, get the next point in time at which an element of
  // the trajectory is either really stored, or would be stored if no later
  // element exists.
  Time GetNextStepAfter(Time time)
  {
    if(time < GetTime())
      return GetTime();

    size_t index = static_cast<size_t>((time - GetTime()) / time_step);
    return GetTime(index + 1, true);
  }

  // Get the average value of the trajectory in a given time span
  double GetAverageValue(Time t1, Time t2)
  {
    double sum = 0.0;

    Time last_time = t1;
    double last_value = GetValueFailsafe(t1);

    while(last_time < t2)
    {
      Time new_time = GetNextStepAfter(last_time);

      // do not go beyond t2
      new_time = std::min(new_time, t2);

      sum += last_value * ((new_time-last_time).GetSeconds() / (t2-t1).GetSeconds());

      last_time = new_time;
      last_value = GetValueFailsafe(new_time);
    }

    return sum;
  }

protected:
  // The time step per element
  Time time_step;

  // Time of the first element in the horizon
  Time first_time;

  // Elements in the horizon
  vector<double> elements;
};

} //namespace ns3

#endif /* __TOR_H__ */
