#!/usr/bin/python3

import sys
import json
import time
import traceback
import copy

import numpy as np
from optimal_traffic_scheduler import optimal_traffic_scheduler
from casadi import DM

from copy import deepcopy

# https://gist.github.com/Wilfred/7889868
def flatten_list(nested_list):
    """Flatten an arbitrarily nested list, without recursion (to avoid
    stack overflows). Returns a new list, the original list is unchanged.
    >> list(flatten_list([1, 2, 3, [4], [], [[[[[[[[[5]]]]]]]]]]))
    [1, 2, 3, 4, 5]
    >> list(flatten_list([[1, 2], 3]))
    [1, 2, 3]
    """
    nested_list = deepcopy(nested_list)

    while nested_list:
        sublist = nested_list.pop(0)

        if isinstance(sublist, list):
            nested_list = sublist + nested_list
        else:
            yield sublist

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

        with open('/tmp/pytalk.calls', 'a') as f:
            print(res, file=f)

        return self.func(*args, **kwargs)

class Handler:
    def __init__(self):
        self.ots = None
        self.relay = None

    def setup(self, **kwargs):
        self.relay = kwargs['relay']

        setup_dict = dict()
        for k in ['v_in_max_total','v_out_max_total','s_c_max_total','scaling','dt','N_steps']:
            setup_dict[k] = kwargs[k]

        #return {'debug': repr(setup_dict)}
        
        # print('# ' + self.relay)
        self.ots = Wrap(f'({self.relay}/{kwargs["time"]}) ots = optimal_traffic_scheduler', optimal_traffic_scheduler)(setup_dict)

        # print('# ' + self.relay)
        Wrap(f'({self.relay}/{kwargs["time"]}) ots.setup', self.ots.setup)(
            n_in = kwargs['n_in'],
            n_out = kwargs['n_out'],
            input_circuits = kwargs['input_circuits'],
            output_circuits = kwargs['output_circuits'],
        )

        return setup_dict

    def solve(self, **kwargs):
        def inner_nparray(l):
            return [np.array([x]).T for x in l]

        def outer_nparray(l):
            return np.array([l]).T

        # print('# ' + self.relay)
        Wrap(f'({self.relay}/{kwargs["time"]}) ots.solve', self.ots.solve, argnames=['s_buffer_0', 'v_out_max', 's_buffer_source', 'v_out_source', 'control_delta'])(
            outer_nparray(kwargs['s_buffer_0']),
            inner_nparray(kwargs['v_out_max']),
            inner_nparray(kwargs['s_buffer_source']),
            inner_nparray(kwargs['v_out_source']),
            kwargs['control_delta'],
        )

        def make_serializable(x):
            if isinstance(x, dict):
                return {k: make_serializable(v) for k,v in x.items()}
            if isinstance(x, np.ndarray):
                return x.tolist()
            if isinstance(x, DM):
                return np.array(x).tolist()
            if isinstance(x, list):
                return [make_serializable(v) for v in x]
            return x

        # print(repr(self.ots.predict[-1]))
        # print("pre-obj:", self.ots.predict[-1])
        return make_serializable(make_serializable(self.ots.predict))


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
                simtime = obj['time']

                print('{}'.format(obj), end='', file=f)

                funcname = obj['cmd']
                del obj['cmd']

                func = getattr(handler, funcname)
                obj = func(**obj)

                if obj is None:
                    obj = dict()

                obj['ok'] = True
                obj['relay'] = handler.relay
                obj['time'] = simtime

                print("::: PyTalk Output :::")
                print(json.dumps(obj))
                sys.stdout.flush()

            except Exception as e:
                obj = {
                    'ok': False,
                    'relay': handler.relay,
                    'time': simtime,
                    'exception': f'{e}',
                    'stacktrace': traceback.format_exc(),
                }
                print("::: PyTalk Output :::")
                print(json.dumps(obj))
                sys.stdout.flush()

            finally:
                print(' -> {}'.format(obj), file=f)
                f.flush()
