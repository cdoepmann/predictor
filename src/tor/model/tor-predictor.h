#ifndef __TOR_H__
#define __TOR_H__

#include "tor-base.h"
#include "cell-header.h"

#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"

namespace ns3 {

#define CIRCWINDOW_START 1000
#define CIRCWINDOW_INCREMENT 100
#define STREAMWINDOW_START 500
#define STREAMWINDOW_INCREMENT 50

#define CELL_PAYLOAD_SIZE 498
#define CELL_NETWORK_SIZE 512

struct buf_t
{
  uint32_t size; // How many bytes is this buffer holding right now?
  uint8_t data[CELL_NETWORK_SIZE]; //Left-over chunk, or NULL for none.
};

class PredCircuit;
class PredConnection;
class TorPredApp;


class PredCircuit : public BaseCircuit
{
public:
  PredCircuit (uint16_t, Ptr<PredConnection>, Ptr<PredConnection>, int, int);
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

  uint32_t GetPackageWindow ();
  void IncPackageWindow ();
  uint32_t GetDeliverWindow ();
  void IncDeliverWindow ();
   
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("PredCircuit")
      .SetParent (BaseCircuit::GetTypeId())
      .AddTraceSource ("PackageWindow",
                       "The vanilla Tor package window.",
                       MakeTraceSourceAccessor(&PredCircuit::package_window),
                       "ns3::TracedValueCallback::int32")
      .AddTraceSource ("DeliverWindow",
                       "The vanilla Tor deliver window.",
                       MakeTraceSourceAccessor(&PredCircuit::deliver_window),
                       "ns3::TracedValueCallback::int32")
      ;
    return tid;
  }

protected:
  Ptr<Packet> PopQueue (queue<Ptr<Packet> >*);
  bool IsSendme (Ptr<Packet>);
  Ptr<Packet> CreateSendme ();

  queue<Ptr<Packet> > *p_cellQ;
  queue<Ptr<Packet> > *n_cellQ;

  //Next circuit in the doubly-linked ring of circuits waiting to add cells to {n,p}_conn.
  Ptr<PredCircuit> next_active_on_n_conn;
  Ptr<PredCircuit> next_active_on_p_conn;

  Ptr<PredConnection> p_conn;   /* The OR connection that is previous in this circuit. */
  Ptr<PredConnection> n_conn;   /* The OR connection that is next in this circuit. */

  /** How many relay data cells can we package (read from edge streams)
   * on this circuit before we receive a circuit-level sendme cell asking
   * for more? */
  TracedValue<int32_t> package_window;
  /** How many relay data cells will we deliver (write to edge streams)
   * on this circuit? When deliver_window gets low, we send some
   * circuit-level sendme cells to indicate that we're willing to accept
   * more. */
  TracedValue<int32_t> deliver_window;

  int m_windowStart;
  int m_windowIncrement;
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

private:
  TorPredApp* torapp;
  Ipv4Address remote;
  Ptr<Socket> m_socket;

  buf_t inbuf; /**< Buffer holding left over data read over this connection. */
  buf_t outbuf; /**< Buffer holding left over data to write over this connection. */

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
  int m_windowStart;
  int m_windowIncrement;

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

protected:
  virtual void DoDispose (void);

};


} //namespace ns3

#endif /* __TOR_H__ */
