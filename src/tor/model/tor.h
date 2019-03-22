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

class Circuit;
class Connection;
class TorApp;


class Circuit : public BaseCircuit
{
public:
  Circuit (uint16_t, Ptr<Connection>, Ptr<Connection>, int, int);
  ~Circuit ();
  void DoDispose ();

  virtual Ptr<Packet> PopCell (CellDirection);
  virtual void PushCell (Ptr<Packet>, CellDirection);
  queue<Ptr<Packet> >* GetQueue (CellDirection);
  uint32_t GetQueueSize (CellDirection);
  uint32_t SendCell (CellDirection);

  Ptr<Connection> GetConnection (CellDirection);
  Ptr<Connection> GetOppositeConnection (CellDirection);
  Ptr<Connection> GetOppositeConnection (Ptr<Connection>);
  CellDirection GetDirection (Ptr<Connection>);
  CellDirection GetOppositeDirection (Ptr<Connection>);

  Ptr<Circuit> GetNextCirc (Ptr<Connection>);
  void SetNextCirc (Ptr<Connection>, Ptr<Circuit>);

  uint32_t GetPackageWindow ();
  void IncPackageWindow ();
  uint32_t GetDeliverWindow ();
  void IncDeliverWindow ();
   
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("Circuit")
      .SetParent (BaseCircuit::GetTypeId())
      .AddTraceSource ("PackageWindow",
                       "The vanilla Tor package window.",
                       MakeTraceSourceAccessor(&Circuit::package_window),
                       "ns3::TracedValueCallback::int32")
      .AddTraceSource ("DeliverWindow",
                       "The vanilla Tor deliver window.",
                       MakeTraceSourceAccessor(&Circuit::deliver_window),
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
  Ptr<Circuit> next_active_on_n_conn;
  Ptr<Circuit> next_active_on_p_conn;

  Ptr<Connection> p_conn;   /* The OR connection that is previous in this circuit. */
  Ptr<Connection> n_conn;   /* The OR connection that is next in this circuit. */

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




class Connection : public SimpleRefCount<Connection>
{
public:
  Connection (TorApp*, Ipv4Address, int);
  ~Connection ();

  Ptr<Circuit> GetActiveCircuits ();
  void SetActiveCircuits (Ptr<Circuit>);
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
  TorApp * GetTorApp() { return torapp; }

private:
  TorApp* torapp;
  Ipv4Address remote;
  Ptr<Socket> m_socket;

  buf_t inbuf; /**< Buffer holding left over data read over this connection. */
  buf_t outbuf; /**< Buffer holding left over data to write over this connection. */

  uint8_t m_conntype;
  bool reading_blocked;

  // Linked ring of circuits
  Ptr<Circuit> active_circuits;

  EventId read_event;
  EventId write_event;

  static map<Ipv4Address, string> remote_names;
};





class TorApp : public TorBaseApp
{
public:
  static TypeId GetTypeId (void);
  TorApp ();
  virtual ~TorApp ();
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
                           Ptr<PseudoClientSocket> clientSocket = 0);

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  Ptr<Circuit> GetCircuit (uint16_t);

  virtual Ptr<Connection> AddConnection (Ipv4Address, int);
  void AddActiveCircuit (Ptr<Connection>, Ptr<Circuit>);

// private:
  void HandleAccept (Ptr<Socket>, const Address& from);

  virtual void ConnReadCallback (Ptr<Socket>);
  virtual void ConnWriteCallback (Ptr<Socket>, uint32_t);
  void PackageRelayCell (Ptr<Connection> conn, Ptr<Packet> data);
  void PackageRelayCellImpl (uint16_t, Ptr<Packet>);
  void ReceiveRelayCell (Ptr<Connection> conn, Ptr<Packet> cell);
  void AppendCellToCircuitQueue (Ptr<Circuit> circ, Ptr<Packet> cell, CellDirection direction);
  Ptr<Circuit> LookupCircuitFromCell (Ptr<Packet>);
  void RefillReadCallback (int64_t);
  void RefillWriteCallback (int64_t);
  void GlobalBucketsDecrement (uint32_t num_read, uint32_t num_written);
  uint32_t RoundRobin (int base, int64_t bucket);
  Ptr<Connection> LookupConn (Ptr<Socket>);

  Ptr<Socket> listen_socket;
  vector<Ptr<Connection> > connections;
  map<uint16_t,Ptr<Circuit> > circuits;
  int m_windowStart;
  int m_windowIncrement;

  // Remember which connection is the next to read from (or write to).
  // Previous operations did not continue because the token bucket ran empty.
  Ptr<Connection> m_scheduleReadHead;
  Ptr<Connection> m_scheduleWriteHead;

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
