#ifndef __TOR_H__
#define __TOR_H__

#include <map>
#include <set>
#include <stdio.h>
#include <queue>

#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/internet-module.h"

#include "tor-base.h"
#include "cell-header.h"
#include "pseudo-socket.h"

namespace ns3 {

#define CIRCWINDOW_START 1000
#define CIRCWINDOW_INCREMENT 100
#define STREAMWINDOW_START 500
#define STREAMWINDOW_INCREMENT 50

#define OR_CONN 0
#define EDGE_CONN 1

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


class Circuit : public SimpleRefCount<Circuit> {
public:
  Circuit(uint32_t, Ptr<Connection>, Ptr<Connection>);
  ~Circuit();

  Ptr<Packet> pop_cell(CellDirection);
  void push_cell(Ptr<Packet>, CellDirection);

  std::queue<Ptr<Packet> >* get_queue(CellDirection);

  Ptr<Connection> get_connection(CellDirection);
  Ptr<Connection> get_opposite_connection(CellDirection);
  Ptr<Connection> get_opposite_connection(Ptr<Connection>);

  CellDirection get_direction(Ptr<Connection>);
  CellDirection get_opposite_direction(Ptr<Connection>);
  CellDirection get_opposite_direction(CellDirection);

  Ptr<Circuit> get_next_circ(Ptr<Connection>);
  void set_next_circ(Ptr<Connection>, Ptr<Circuit>);

  uint32_t get_queue_size(CellDirection);

  uint32_t get_id();

  uint32_t get_stats_bytes_read(CellDirection);
  uint32_t get_stats_bytes_written(CellDirection);
  void inc_stats_bytes(CellDirection,uint32_t,uint32_t);
  void reset_stats_bytes();

  uint32_t get_package_window();
  void inc_package_window();

  uint32_t get_deliver_window();
  void inc_deliver_window();

  void DoDispose();

  uint32_t send_cell(CellDirection);

private:
  Ptr<Packet> pop_queue(std::queue<Ptr<Packet> >*);

  bool is_relay_sendme(Ptr<Packet>);
  Ptr<Packet> create_sendme_cell();

  int circ_id;

  std::queue<Ptr<Packet> > *p_cellQ;
  std::queue<Ptr<Packet> > *n_cellQ;

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




class Connection : public SimpleRefCount<Connection> {
public:

  Connection(TorApp*, Ipv4Address, int);
  ~Connection();

  Ptr<Circuit> get_active_circuits();
  void set_active_circuits(Ptr<Circuit>);

  uint8_t get_type();
  bool is_blocked();
  void set_blocked(bool);
  uint32_t read(std::vector<Ptr<Packet> >*, uint32_t);
  uint32_t write(uint32_t);

  Ptr<Socket> get_socket();
  void set_socket(Ptr<Socket>);
  Ipv4Address get_remote();

  uint32_t get_outbuf_size();
  uint32_t get_inbuf_size();

  void schedule_write(Time = Seconds(0));
  void schedule_read(Time = Seconds(0));

  void SetRandomVariableStreams(Ptr<RandomVariableStream>, Ptr<RandomVariableStream>);
  Ptr<RandomVariableStream> GetRequestStream();
  Ptr<RandomVariableStream> GetThinkStream();

  void SetTtfbCallback(void (*)(int, double, std::string), int, std::string="");
  void SetTtlbCallback(void (*)(int, double, std::string), int, std::string="");
  void RegisterCallbacks();
private:
  TorApp* torapp;
  Ipv4Address remote;
  Ptr<Socket> socket;

  buf_t inbuf; /**< Buffer holding left over data read over this connection. */
  buf_t outbuf; /**< Buffer holding left over data to write over this connection. */

  uint8_t conn_type;
  bool reading_blocked;

  // Linked ring of circuits
  Ptr<Circuit> active_circuits;

  EventId read_event;
  EventId write_event;

  Ptr<RandomVariableStream> m_rng_request;
  Ptr<RandomVariableStream> m_rng_think;

  void (*m_ttfb_callback)(int, double, std::string);
  void (*m_ttlb_callback)(int, double, std::string);
  int m_ttfb_id;
  int m_ttlb_id;
  std::string m_ttfb_desc;
  std::string m_ttlb_desc;
};





class TorApp : public TorBaseApp
{
public:
  static TypeId GetTypeId (void);
  TorApp ();
  virtual ~TorApp ();
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int);
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int, Ptr<RandomVariableStream>, Ptr<RandomVariableStream>);

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  Ptr<Circuit> GetCircuit(uint32_t circid);

  virtual Ptr<Connection> AddConnection (Ipv4Address, int);
  void AddActiveCircuit (Ptr<Connection>, Ptr<Circuit>);

// private:
  void HandleAccept (Ptr<Socket>, const Address& from);

  virtual void conn_read_callback (Ptr<Socket>);
  virtual void conn_write_callback (Ptr<Socket>, uint32_t);
  void package_relay_cell (Ptr<Connection> conn, Ptr<Packet> data);
  void package_relay_cell_impl (int, Ptr<Packet>);
  void receive_relay_cell (Ptr<Connection> conn, Ptr<Packet> cell);
  void append_cell_to_circuit_queue (Ptr<Circuit> circ, Ptr<Packet> cell, CellDirection direction);
  Ptr<Circuit> lookup_circuit_from_cell (Ptr<Packet>);

  void refill_read_callback (int64_t);
  void refill_write_callback (int64_t);
  void global_buckets_decrement (uint32_t num_read, uint32_t num_written);
  uint32_t round_robin (int base, int64_t bucket);

  Ptr<Connection> lookup_conn (Ptr<Socket>);

  Ptr<Socket> listen_socket;
  std::vector<Ptr<Connection> > connections;
  std::map<uint16_t,Ptr<Circuit> > circuits;

protected:
  virtual void DoDispose (void);

};


} //namespace ns3

#endif /* __TOR_H__ */
