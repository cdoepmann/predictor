#!/usr/bin/python3

import sys
import json
import time
import traceback
import copy

import numpy as np
from optimal_traffic_scheduler import optimal_traffic_scheduler

class Wrap:
    def __init__(self, name, func, argnames=None):
        self.name = name
        self.func = func
        self.argnames = argnames

    def __call__(self, *args, **kwargs):
        res = ''

        def print_argname(i):
            nonlocal res
            if self.argnames:
                res += '    # ' + self.argnames[i] + '\n'

        i = 0
        res += self.name + '(\n'

        for x in args:
            print_argname(i)
            res += '    ' + repr(x).replace('\n', ' ') + ',\n'
            i += 1

        for k,v in kwargs.items():
            print_argname(i)
            res += '    ' + str(k) + ' = ' + repr(v).replace('\n', ' ') + ',\n'
            i += 1

        res += ')\n'
        
        res = res.replace('array', 'np.array')

        # print(res)
        return self.func(*args, **kwargs)

class Handler:
    def __init__(self):
        self.ots = None
        self.relay = None

    def setup(self, **kwargs):
        self.relay = kwargs['relay']

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
        
        # print('# ' + self.relay)
        self.ots = Wrap('ots = optimal_traffic_scheduler', optimal_traffic_scheduler)(setup_dict)

        # print('# ' + self.relay)
        Wrap('ots.setup', self.ots.setup)(
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

        # print('# ' + self.relay)
        Wrap('ots.solve', self.ots.solve, argnames=[ 's_buffer_0', 's_circuit_0', 's_transit_0', 'v_in_req', 'cv_in', 'v_out_max', 'bandwidth_load_target', 'memory_load_target', 'bandwidth_load_source', 'memory_load_source', 'output_delay' ])(
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
        # print("pre-obj:", self.ots.predict[-1])
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
                # print("obj:", obj)
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
