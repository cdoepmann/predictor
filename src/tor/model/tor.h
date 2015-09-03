#ifndef __TOR_H__
#define __TOR_H__

#include "tor-base.h"
#include "cell-header.h"

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


class Circuit : public SimpleRefCount<Circuit>
{
public:
  Circuit (uint32_t, Ptr<Connection>, Ptr<Connection>);
  ~Circuit ();
  void DoDispose ();

  Ptr<Packet> PopCell (CellDirection);
  void PushCell (Ptr<Packet>, CellDirection);
  queue<Ptr<Packet> >* GetQueue (CellDirection);
  uint32_t GetQueueSize (CellDirection);
  uint32_t SendCell (CellDirection);

  Ptr<Connection> GetConnection (CellDirection);
  Ptr<Connection> GetOppositeConnection (CellDirection);
  Ptr<Connection> GetOppositeConnection (Ptr<Connection>);
  CellDirection GetDirection (Ptr<Connection>);
  CellDirection GetOppositeDirection (Ptr<Connection>);
  CellDirection GetOppositeDirection (CellDirection);

  Ptr<Circuit> GetNextCirc (Ptr<Connection>);
  void SetNextCirc (Ptr<Connection>, Ptr<Circuit>);

  uint32_t GetId ();
  uint32_t GetStatsBytesRead (CellDirection);
  uint32_t GetStatsBytesWritten (CellDirection);
  void IncStatsBytes (CellDirection,uint32_t,uint32_t);
  void ResetStatsBytes ();

  uint32_t GetPackageWindow ();
  void IncPackageWindow ();
  uint32_t GetDeliverWindow ();
  void IncDeliverWindow ();

private:
  Ptr<Packet> PopQueue (queue<Ptr<Packet> >*);
  bool IsSendme (Ptr<Packet>);
  Ptr<Packet> CreateSendme ();

  int circ_id;

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
  int package_window;
  /** How many relay data cells will we deliver (write to edge streams)
   * on this circuit? When deliver_window gets low, we send some
   * circuit-level sendme cells to indicate that we're willing to accept
   * more. */
  int deliver_window;

  uint32_t stats_p_bytes_read;
  uint32_t stats_p_bytes_written;

  uint32_t stats_n_bytes_read;
  uint32_t stats_n_bytes_written;

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
};





class TorApp : public TorBaseApp
{
public:
  static TypeId GetTypeId (void);
  TorApp ();
  virtual ~TorApp ();
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
                           Ptr<PseudoClientSocket> clientSocket=0);

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  Ptr<Circuit> GetCircuit (uint32_t circid);

  virtual Ptr<Connection> AddConnection (Ipv4Address, int);
  void AddActiveCircuit (Ptr<Connection>, Ptr<Circuit>);

// private:
  void HandleAccept (Ptr<Socket>, const Address& from);

  virtual void ConnReadCallback (Ptr<Socket>);
  virtual void ConnWriteCallback (Ptr<Socket>, uint32_t);
  void PackageRelayCell (Ptr<Connection> conn, Ptr<Packet> data);
  void PackageRelayCellImpl (int, Ptr<Packet>);
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

protected:
  virtual void DoDispose (void);

};


} //namespace ns3

#endif /* __TOR_H__ */
