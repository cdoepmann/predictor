
# Setup (per relay)

- do NOT record history
- setup_dict
  - **v_max**: read+write total packets per second
    - token_bucket*2 ?
  - **s_max**: maximum packets in server (storage)
    - irgendeinen Wert ??
    - maximum queue length?
  - **dt**: timestep
    - relation to latency?
    - maybe ~ RTT
    - same for all?
  - n_steps/weights: ok

- setup() --> **build from topology helper**
  - n_in:  number of incoming connections
  - n_out: number of outgoing connections
  - input_circuits: for each in. connection, list of circuit IDs in that con.
  - output_circuits: for each out. connection, list of circuit IDs in that con.


# Solving

- consider timestamps of trajectories from other relays
- solve() advances time of node by one dt
  - **what is self.time used for?**
  - **any extrapolation, yet?**

- s_buffer_0: current per-out-connection queue length
- s_circuit_0: current per-circuit queue length
- (traj.) **v_in_req**: requested incoming packets/s (?) per in-conn
- (traj.) **cv_in**: for each in-conn, list of circuit shares of that conn.
- (traj.) {bandwidth,memory}_load_{target,source} per-connection pred. of relays
  - **unit**?

# Connection Handling

- depending on the directions of the different circuits, a connection can act both as an input as well as an output connection
- therefore, in the context of the controller, regard these as distinct connections
- identify by (Conn, in/out)

- send to INBOUND without any window/congestion handling (for now)