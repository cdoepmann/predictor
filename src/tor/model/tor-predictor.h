#ifndef __TOR_PREDICTOR_H__
#define __TOR_PREDICTOR_H__

#include "tor-base.h"
#include "cell-header.h"
#include "pytalk.h"

#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"

#include <functional>
#include <thread>
#include <future>
#include <deque>
#include <ostream>
#include <cmath>

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
class Trajectory;
class MultiCellDecoder;


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
  void ScheduleWrite ();
  void ScheduleWriteWrapper (int64_t);
  void ScheduleRead (Time = Seconds (0));
  Ptr<Socket> GetSocket ();
  void SetSocket (Ptr<Socket>);

  void SetControlSocket (Ptr<Socket> socket);
  Ptr<Socket> GetControlSocket ();
  uint32_t ReadControl (vector<Ptr<Packet>> &);

  Ipv4Address GetRemote ();
  Ptr<PredConnection> GetRemoteConn ();
  uint32_t GetOutbufSize ();
  uint32_t GetInbufSize ();

  static void RememberName (Ipv4Address, string);
  string GetRemoteName ();
  TorPredApp * GetTorApp() { return torapp; }

  static void RememberConnection (Ipv4Address from, Ipv4Address to, Ptr<PredConnection> conn);

  // Get the (theoretical) base delay to the remote site
  Time GetBaseRtt ();

  void SendConnLevelCell (Ptr<Packet>);

  // Schedule the reception of a cell at the remote site after the appropriate
  // time span (network delay), WITHOUT using real network communication.
  void BeamConnLevelCell (Ptr<Packet> cell);

  // Trigger the reception of a connection-level cell that was "beamed".
  // This has to be static because ns-3 doesn't offer callbacks that
  // point at member functions AND are bound at the same time
  void ReceiveBeamedConnLevelCell(Ptr<Packet> cell);

  size_t CountCircuits()
  {
    Ptr<PredCircuit> first_circuit = GetActiveCircuits ();

    if (!first_circuit) {
      return 0;
    }
    
    size_t count = 1;
    auto next_circuit = first_circuit->GetNextCirc(this);
    while (next_circuit != first_circuit)
    {
      count++;
      next_circuit = next_circuit->GetNextCirc(this);
    }

    return count;
  };

  uint64_t GetDataSent()
  {
    // uint64_t result = 0;

    // auto first_circuit = GetActiveCircuits ();
    // auto this_circuit = first_circuit;
    // do {
    //   NS_ASSERT(this_circuit);
    //   auto direction = this_circuit->GetDirection(this);
    //   result += this_circuit->GetBytesWritten(direction);
      
    //   this_circuit = this_circuit->GetNextCirc(this);
    // }
    // while (this_circuit != first_circuit);

    // return result;

    return m_data_sent;
  };

  uint64_t m_data_sent;

protected:

private:
  TorPredApp* torapp;
  Ipv4Address remote;
  Ptr<Socket> m_socket;
  Ptr<Socket> m_controlsocket;

  pred_buf_t inbuf; /**< Buffer holding left over data read over this connection. */
  pred_buf_t outbuf; /**< Buffer holding left over data to write over this connection. */

  vector<uint8_t> control_inbuf;

  uint8_t m_conntype;

  // Linked ring of circuits
  Ptr<PredCircuit> active_circuits;

  EventId read_event;
  EventId write_event;

  static map<Ipv4Address, string> remote_names;
  static map<pair<Ipv4Address,Ipv4Address>, Ptr<PredConnection>> remote_connections;
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
  
  void HandleControlAccept (Ptr<Socket>, const Address& from);
  virtual void ConnReadControlCallback (Ptr<Socket>);
  Ptr<PredConnection> LookupConnByControlSocket (Ptr<Socket>);

  Ptr<Socket> listen_socket;
  Ptr<Socket> control_socket;
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

  // Handle an incoming connection-level cell
  void HandleDirectCell (Ptr<PredConnection> conn, Ptr<Packet> cell);

protected:
  virtual void DoDispose (void);

  // Determine if a received cell is on the connection level (a.k.a. direct)
  bool IsDirectCell (Ptr<Packet> cell);

  // Decoder to re-assemble previously fragmented multi-cells
  map<Ptr<PredConnection>, MultiCellDecoder> multicell_decoders;
};


// Helper class to carry out calls to the optimizer in parallel if they happen
// at the same simulation time
class BatchedExecutor {
public:
  // Add a task for computation
  void Compute(std::function<rapidjson::Document*()> worker, Callback<void,rapidjson::Document*> cb) {
    calls.push_back(std::make_pair(worker, cb));

    compute_event.Cancel ();
    compute_event = Simulator::ScheduleNow(&BatchedExecutor::DoCompute, this);
  }

  // Carry out the computation
  void DoCompute()
  {
    std::vector<std::thread> threads;

    std::mutex input_mutex;
    std::mutex output_mutex;

    // Make sure the results vector is not re-allocated during the computation,
    // so the references stay valid
    results.reserve(calls.size());

    for (int i=0; i<8; i++) {
      threads.push_back(
        std::thread{
          [&input_mutex,&output_mutex,this] {
            while (true) {
              // pointer to where we will save the result
              decltype(this->results)::value_type * result;

              // get the next job
              std::pair<std::function<rapidjson::Document*()>,Callback<void,rapidjson::Document*>> job;
              {
                std::lock_guard<std::mutex> lock{input_mutex};

                if (this->calls.size() == 0)
                  return;
                
                job = this->calls.front();
                this->calls.pop_front();

                // Push a dummy result now and alter it later with the real result.
                // This way, we ensure that the results vector has the same order
                // as the original calls (no variation due to lock contention
                // and other races).
                // The reference stays valid because we avoid re-allocation and
                // the vector is only grown, not shrunken before all threads are done.
                this->results.push_back(
                  std::make_pair(MakeNullCallback<void,rapidjson::Document*>(), nullptr)
                );
                result = &this->results.back();
              }

              // do the computation
              auto doc = job.first();

              // save the result
              {
                std::lock_guard<std::mutex> lock{output_mutex};
                
                *result = std::make_pair(job.second, doc);
              }
            } // loop in lambda
          } // lambda
        } // thread
      ); // push to vector
    } // for loop

    // wait for the threads to finish
    for (auto&& t : threads) {
      t.join();
    }

    // trigger the callbacks
    for (auto result : this->results) {
      result.first(result.second);
    }

    results.clear ();
  }

protected:
  // The list of computations to carry out
  std::deque<
    std::pair<
      std::function<rapidjson::Document*()>,
      Callback<void,rapidjson::Document*>
    >
  > calls;

  // The results of the computations
  std::vector<
    std::pair<
      Callback<void,rapidjson::Document*>,
      rapidjson::Document*
    >
  > results;
  
  // The event that will trigger the parallel processing
  EventId compute_event;
};

extern PyScript dumper;

// The interface to our model-predictive controller
class PredController : public Object {
public:
  PredController (Ptr<TorPredApp> app) : 
    app{app},
    horizon{20},
    time_step{MilliSeconds(40)},
    pyscript{"/home/christoph/nstor/src/tor/model/solver.sh"}
  {
    dumper.dump("time-step",
                "node", app->GetNodeName(),
                "seconds", time_step.GetSeconds()
    );
  };
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

  // Start the regular optimization schedule
  void Start();

  // Carry out the regular optimization step, based on current local information
  // as well as information provided by our neighboring relays.
  void Optimize();

  // Called when the optimization step is complete.
  void OptimizeDone(rapidjson::Document * doc);

  // Based on the predicted v_out values, calculate a concrete output plan per
  // connection.
  void CalculateSendPlan();

  void DumpConnNames();

  // Assemble information to be sent to our neighbors, after optimization
  void SendToNeighbors();

  // Get the number of steps in the horizon
  size_t Horizon () { return horizon; };

  // Get the time step per horizon element
  Time TimeStep () { return time_step; };

  // Get the maximum data rate we can achieve
  DataRate MaxDataRate () { return max_datarate; };

  // Handle a feedback cell received from another relay.
  // The input packet is a re-assembled multicell.
  void HandleFeedback(Ptr<PredConnection> conn, Ptr<Packet> cell)
  {
    // Is this an input or output connection?
    if (IsInConn(conn))
    {
      HandleInputFeedback(conn, cell);
    }
    else
    {
      NS_ASSERT(IsOutConn(conn));
      HandleOutputFeedback(conn, cell);
    }
  };

  // Get the time of the next optimization step
  Time GetNextOptimizationTime() { return ns3::TimeStep(optimize_event.GetTs()); }

protected:
  // The application this controller belongs to
  Ptr<TorPredApp> app;

  // The number of steps in the horizon
  size_t horizon;

  // The time step per horizon element
  Time time_step;

  // The connections that are used for receiving and sending data
  //
  // Note: After being used for the first time, these sets are not allowed to
  // be modified anymore since the order identifies the contained elements.
  set<Ptr<PredConnection>> in_conns;
  set<Ptr<PredConnection>> out_conns;

  // Helper functions to lookup the index of a given connection in the data vectors
  bool IsInConn(Ptr<PredConnection>);
  bool IsOutConn(Ptr<PredConnection>);
  size_t GetInConnIndex(Ptr<PredConnection>);
  size_t GetOutConnIndex(Ptr<PredConnection>);

  // Handle a feedback cell received from a predecessor.
  // The input packet is a re-assembled multicell.
  void HandleInputFeedback(Ptr<PredConnection> conn, Ptr<Packet> cell);

  // Handle a feedback cell received from a successor.
  // The input packet is a re-assembled multicell.
  void HandleOutputFeedback(Ptr<PredConnection> conn, Ptr<Packet> cell);

  // The number of circuits handled by this relay
  size_t num_circuits;

  // Interface for talking to the python component
  PyScript pyscript;

  // Event of the next optimization step
  EventId optimize_event;

  // Convert between DataRate and packets/s
  static double to_packets_sec(DataRate rate);
  static DataRate to_datarate(double pps);

  // Convert vector of trajectories to vector of double vectors
  vector<vector<double>> to_double_vectors(vector<Trajectory> trajectories);

  // Convert vector of trajectories to vector of double vectors while
  // transposing the matrix (?), such that the highest-level list corresponds
  // to the horizon steps.
  vector<vector<double>> transpose_to_double_vectors(vector<Trajectory> trajectories);

  // The maximum data rate of this relay
  DataRate max_datarate;

  // Token buckets for the per-connection rate limiting based on v_out
  map<Ptr<PredConnection>, uint64_t> conn_buckets;

  // Remember how much data had been sent over a connection at the time of the
  // last optimization.
  map<Ptr<PredConnection>, uint64_t> conn_last_sent;

  // Grant access to application for using the per-connection token buckets
  friend void TorPredApp::ConnWriteCallback(Ptr<Socket>, uint32_t);

  // (shared) pool executor to run the optimizations in parallel
  static BatchedExecutor executor;

  // Parse rapidjson object into a vector of trajectories, checking the number
  // of parsed trajectories
  void ParseIntoTrajectories(const rapidjson::Value& array, vector<Trajectory>& target, Time first_time, size_t expected_traj);

  // Parse rapidjson object containing the special multidimensional composition
  // matrix into a vector of vectors of trajectories
  void ParseCvOutIntoTrajectories(const rapidjson::Value& array, vector<vector<Trajectory>>& target, Time first_time, size_t expected_traj_outer);
  void ParseCvInIntoTrajectories(const rapidjson::Value& array, vector<vector<Trajectory>>& target, Time first_time, size_t connections);

  // Merge two trajectories
  void MergeTrajectories(Trajectory& target, Trajectory& source);
  void MergeTrajectories(vector<Trajectory>& target, vector<Ptr<Trajectory>>& source);

  vector<Trajectory> pred_v_in;
  vector<Trajectory> pred_v_in_max;
  vector<Trajectory> pred_v_in_req;
  vector<Trajectory> pred_v_out;
  vector<Trajectory> pred_v_out_max;
  vector<Trajectory> pred_s_buffer;
  vector<Trajectory> pred_s_circuit;
  vector<Trajectory> pred_bandwidth_load_target;
  vector<Trajectory> pred_bandwidth_load_source;
  vector<Trajectory> pred_memory_load_target;
  vector<Trajectory> pred_memory_load_source;
  vector<Trajectory> pred_bandwidth_load_local; // only contains one element
  vector<Trajectory> pred_memory_load_local;    // only contains one element

  vector<vector<Trajectory>> pred_cv_in;
  vector<vector<Trajectory>> pred_cv_out;
};


class Trajectory : public SimpleRefCount<Trajectory> {
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

  string Format()
  {
    stringstream ss;
    ss << "[" << GetTime().GetSeconds() << "]";
    for (auto& val : Elements())
    {
      ss << " " << val;
    }
    return ss.str();
  }

  // Get the time step
  Time GetTimeStep() { return time_step; }

protected:
  // The time step per element
  Time time_step;

  // Time of the first element in the horizon
  Time first_time;

  // Elements in the horizon
  vector<double> elements;
};

class HiddenDataTag : public Tag
{
public:
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (TagBuffer i) const;
  virtual void Deserialize (TagBuffer i);
  virtual void Print (std::ostream &os) const;

  // these are our accessors to our tag structure
  void SetData (Ptr<Packet>);
  Ptr<Packet> GetData (void) const;
private:
  vector<uint8_t> hidden_data;
};

// Helper classes to transfer data that does not fit into a single cell.
class MultiCellDecoder {
public:
  MultiCellDecoder() : finished{false} {};

  void AddCell(Ptr<Packet> cell) {
    NS_ASSERT(!finished);
    NS_ASSERT(cell);
    cell = cell->Copy();

    HiddenDataTag tag;
    NS_ASSERT( cell->FindFirstMatchingByteTag (tag) );
    cout << "Got tag: ";
    cell->PrintByteTags(cout);
    cout << endl;
    packet = tag.GetData();

    finished = true;
  };

  bool IsReady() { return finished; }

  Ptr<Packet> GetAndReset() {
    NS_ASSERT(finished);

    Ptr<Packet> result = packet;
    finished = false;
    packet = nullptr;

    return result;
  }

private:
  Ptr<Packet> packet;
  bool finished;
};

class MultiCellEncoder {
public:
  // Create a series of smaller cells from a packet too large for a single cell
  static vector<Ptr<Packet>> encode(Ptr<Packet> large_packet) {
    Ptr<Packet> dummy_packet = Create<Packet> (1);
    HiddenDataTag tag;
    tag.SetData (large_packet);

    dummy_packet->AddByteTag(tag);
    
    vector<Ptr<Packet>> result;
    result.push_back(dummy_packet);

    return std::move(result);
  }
};

class TrajectoryHeader : public Header
{
public:

  TrajectoryHeader () : m_trajectory{0} { }

  TypeId
  GetTypeId () const
  {
    static TypeId tid = TypeId ("ns3::TrajectoryHeader")
      .SetParent<Header> ()
      .AddConstructor<TrajectoryHeader> ()
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
    if (m_trajectory) {
      os << "first_time=" << m_trajectory->GetTime().GetSeconds() << " ";
      os << "time_step=" << m_trajectory->GetTimeStep().GetSeconds() << " ";
      os << "num_elements=" << m_trajectory->Steps();
    }
    else {
      os << "(empty/invalid trajectory)";
    }
  }

  uint32_t
  GetSerializedSize () const
  {
    NS_ASSERT(m_trajectory);
    return 2*sizeof(double) + 1*sizeof(uint16_t) + m_trajectory->Steps()*sizeof(double);
  }

  void
  Serialize (Buffer::Iterator start) const
  {
    NS_ASSERT(m_trajectory);
    Buffer::Iterator iter = start;

    WriteDouble(iter, m_trajectory->GetTime().GetSeconds());
    WriteDouble(iter, m_trajectory->GetTimeStep().GetSeconds());
    iter.WriteU16((uint16_t) m_trajectory->Steps());

    for (auto&& val : m_trajectory->Elements()) {
      WriteDouble(iter, val);
    }
  }

  uint32_t
  Deserialize (Buffer::Iterator start)
  {
    Buffer::Iterator iter = start;
    double first_time = ReadDouble(iter);
    double time_step = ReadDouble(iter);
    uint16_t num_elements = iter.ReadU16();

    m_trajectory = Create<Trajectory>(Seconds(time_step), Seconds(first_time));

    for (int i=0; i<(int)num_elements; i++) {
      double val = ReadDouble(iter);
      m_trajectory->Elements().push_back(val);
    }

    return GetSerializedSize ();
  }

  // Copy a trajectory into this header object
  void SetTrajectory(Ptr<Trajectory> origin) {
    m_trajectory = origin;
  }

  Ptr<Trajectory> GetTrajectory() { return m_trajectory; }

protected:
  Ptr<Trajectory> m_trajectory;

private:
  double ReadDouble(Buffer::Iterator& iter) {
    double v;
    uint8_t *buf = (uint8_t *)&v;

    for (uint32_t i = 0; i < sizeof (double); ++i, ++buf) {
      *buf = iter.ReadU8 ();
    }

    return v;
  }

  void WriteDouble(Buffer::Iterator& iter, double v) const {
    uint8_t *buf = (uint8_t *)&v;

    for (uint32_t i = 0; i < sizeof (double); ++i, ++buf) {
      iter.WriteU8 (*buf);
    }
  }
};

// Stores the meaning of what a serialized trajectory denotes (symbol), as
// regarded by the *sender*.
enum class FeedbackTrajectoryKind : uint8_t {VInMax, VOut, CvOut, MemoryLoad, BandwidthLoad};

string FormatFeedbackKind(FeedbackTrajectoryKind kind);

std::ostream & operator << (std::ostream & os, const FeedbackTrajectoryKind & kind);

class FeedbackKindHeader : public Header
{
public:
  FeedbackKindHeader () { }

  TypeId
  GetTypeId () const
  {
    static TypeId tid = TypeId ("ns3::FeedbackKindHeader")
      .SetParent<Header> ()
      .AddConstructor<FeedbackKindHeader> ()
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
    os << "kind=" << (uint8_t) kind;
  }

  uint32_t
  GetSerializedSize () const
  {
    return 1;
  }

  void
  Serialize (Buffer::Iterator start) const
  {
    Buffer::Iterator iter = start;
    iter.WriteU8((uint8_t) kind);
  }

  uint32_t
  Deserialize (Buffer::Iterator start)
  {
    Buffer::Iterator iter = start;
    kind = (FeedbackTrajectoryKind) iter.ReadU8() ;
    return GetSerializedSize ();
  }

  void SetKind(FeedbackTrajectoryKind new_kind) { kind = new_kind; }

  FeedbackTrajectoryKind GetKind() { return kind; }

protected:
  FeedbackTrajectoryKind kind;
};

// Generate and parse messages that are exchanged between the controllers of
// neighboring relays.
class PredFeedbackMessage {
public:
  // Construct by parsing an existing packet
  PredFeedbackMessage(Ptr<Packet> packet) {
    // parse the packet
    packet = packet->Copy();
    while (packet->GetSize() > 0) {
      FeedbackKindHeader kind;
      packet->RemoveHeader(kind);

      TrajectoryHeader traj;
      packet->RemoveHeader(traj);

      Add(kind.GetKind(), traj.GetTrajectory());
    }
  };

  // Construct an empty message to be filled with local information
  PredFeedbackMessage() { };

  // Add a trajectory to this message
  void Add(FeedbackTrajectoryKind key, Ptr<Trajectory> value) {
    auto entry = make_pair(key, value);
    entries.push_back(entry);
  }
  void Add(FeedbackTrajectoryKind key, Trajectory& value) {
    // this relies on the copy constructor of Trajectory
    Add(key, Create<Trajectory> (value));
  }

  // Get the first contained trajectory of a given type
  const Ptr<Trajectory> Get(FeedbackTrajectoryKind key) {
    for (auto&& entry : entries) {
      if (entry.first == key)
        return entry.second;
    }
    NS_ABORT_MSG("feedback message does not contain an entry of kind " << key);
    return 0;
  }

  // Get all contained trajectories of a given type
  const vector<Ptr<Trajectory>> GetAll(FeedbackTrajectoryKind key) {
    vector<Ptr<Trajectory>> result;
    for (auto&& entry : entries) {
      if (entry.first == key)
        result.push_back(entry.second);
    }
    return result;
  }

  // Construct a packet from the previously added trajectories
  Ptr<Packet> MakePacket() {
    Ptr<Packet> packet = Create<Packet> ();

    for (auto& kv : entries) {
      FeedbackKindHeader kind;
      kind.SetKind(kv.first);

      TrajectoryHeader traj;
      traj.SetTrajectory(kv.second);

      packet->AddHeader(traj);
      packet->AddHeader(kind);
    }

    return packet;
  }

protected:
  vector<std::pair<FeedbackTrajectoryKind,Ptr<Trajectory>>> entries;
};

} //namespace ns3

#endif /* __TOR_PREDICTOR_H__ */
