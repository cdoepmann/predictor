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
  uint32_t SendCell (CellDirection);

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
  bool IsBlocked ();
  void SetBlocked (bool);
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
  bool reading_blocked;

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

protected:
  // The application this controller belongs to
  Ptr<TorPredApp> app;

  // The connections that are used for receiving and sending data
  set<Ptr<PredConnection>> in_conns;
  set<Ptr<PredConnection>> out_conns;

  // Interface for talking to the python component
  PyScript pyscript;

  // Convert between DataRate and packets/s
  static double to_packets_sec(DataRate rate);
  static DataRate to_datarate(double pps);
};


} //namespace ns3

#endif /* __TOR_H__ */
