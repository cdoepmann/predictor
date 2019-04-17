#!/usr/bin/python3

import sys
import json
import time
import traceback
import copy

import numpy as np
from optimal_traffic_scheduler import optimal_traffic_scheduler

class Handler:
    def __init__(self):
        self.ots = None

    def setup(self, **kwargs):
        setup_dict = dict()
        for k in ['v_max','s_max','dt','N_steps']:
            setup_dict[k] = kwargs[k]

        # parse weights
        setup_dict['weights'] = dict()
        while len(kwargs['weights']) > 0:
            k = kwargs['weights'].pop(0)
            v = kwargs['weights'].pop(0)
            setup_dict['weights'][k] = float(v)

        #return {'debug': repr(setup_dict)}
        
        self.ots = optimal_traffic_scheduler(setup_dict)

        self.ots.setup(
            n_in = kwargs['n_in'],
            n_out = kwargs['n_out'],
            input_circuits = kwargs['input_circuits'],
            output_circuits = kwargs['output_circuits'],
            output_delay = np.array(kwargs['output_delays']),
        )

        return setup_dict

    def solve(self, **kwargs):
        def inner_nparray(l):
            return [np.array([x]).T for x in l]

        def process_cv_in(orig):
            res = []
            for h in orig:
                for con in h:
                    res.append(np.array(con).reshape(-1,1))
            return np.array([res])

        self.ots.solve(
            inner_nparray(kwargs['s_buffer_0']),
            inner_nparray(kwargs['s_circuit_0']),
            inner_nparray(kwargs['s_transit_0']),
            inner_nparray(kwargs['v_in_req']),
            process_cv_in(kwargs['cv_in']),
            inner_nparray(kwargs['v_out_max']),
            inner_nparray(kwargs['bandwidth_load_target']),
            inner_nparray(kwargs['memory_load_target']),
            inner_nparray(kwargs['bandwidth_load_source']),
            inner_nparray(kwargs['memory_load_source']),
            np.array(kwargs['output_delay']),
        )

        def make_serializable(x):
            if isinstance(x, dict):
                return {k: make_serializable(v) for k,v in x.items()}
            if isinstance(x, np.ndarray):
                return x.tolist()
            if isinstance(x, list):
                return [make_serializable(v) for v in x]
            return x

        # print(repr(self.ots.predict[-1]))
        return make_serializable(self.ots.predict[-1])


if __name__ == '__main__':
#    buffer = ""
#    while True:
#        c = sys.stdin.read(1)
#        print(c, repr(c), buffer)
#        if len(c) == 0:
#            break

    handler = Handler()

    with open('/tmp/pytalk.out', 'a') as f:
        for line in sys.stdin:
            try:
                # print(repr(line))

                obj = json.loads(line)

                print('{}'.format(obj), end='', file=f)

                funcname = obj['cmd']
                del obj['cmd']

                func = getattr(handler, funcname)
                obj = func(**obj)

                if obj is None:
                    obj = dict()

                obj['ok'] = True
                # raise Exception("oh my gawd!")
                print(json.dumps(obj))
                sys.stdout.flush()

            except Exception as e:
                obj = {
                    'ok': False,
                    'exception': f'{e}',
                    'stacktrace': traceback.format_exc(),
                }
                print(json.dumps(obj))
                sys.stdout.flush()

            finally:
                print(' -> {}'.format(obj), file=f)
                f.flush()
