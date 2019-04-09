#!/usr/bin/python3

import sys
import json
import time
import traceback

class Handler:
    def foo(self, **kwargs):
        pass

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

                print(' -> {}'.format(obj), file=f)
                f.flush()

            except Exception as e:
                obj = {
                    'ok': False,
                    'exception': f'{e}',
                    'stacktrace': traceback.format_exc(),
                }
                print(json.dumps(obj))
                sys.stdout.flush()
