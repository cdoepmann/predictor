PredicTor
---------

This is a prototype implementation of *PredicTor*, a predictive traffic scheduler for the Tor network,
based on distributed model predictive control (MPC).

PredicTor is currently implemented as an extension of [nstor](https://github.com/tschorsch/nstor),
which, in turn, is built on top of the [ns-3 network simulator](https://www.nsnam.org/).
This repository contains the source code of nstor (including ns-3) and adds PredicTor.
PredicTor mainly lives in the following files:

- [src/tor/model/tor-predictor.h](src/tor/model/tor-predictor.h)
- [src/tor/model/tor-predictor.cc](src/tor/model/tor-predictor.cc)
- [src/tor/model/solver.py](src/tor/model/solver.py)
- [src/tor/model/solver.sh](src/tor/model/solver.sh)

## Usage

Running simulations of PredicTor currently requires a few other components
to be available:

- [`predictor-core`](https://github.com/cdoepmann/predictor-core) provides the definition of the underlying optimization problem and the optimizer (Python)
- [Coin-HSL / MA27](http://www.hsl.rl.ac.uk/ipopt/) is used internally as the underlying solver. You need to register for free in order to obtain a copy (binary download or compile from source)

While not strictly necessary, it is recommended to run `predictor-core` using Anaconda to handle its dependencies.

If you have all the dependencies ready, create a `.predictorrc` in the root directory of this project and give the dependencies' locations as follows:

```
PREDICTOR_PATH_OPTIMIZER="<path to predictor-core>"
PREDICTOR_PATH_COINHSL="<path to hsl-bin/lib>"
PREDICTOR_PATH_CONDA="<path to anaconda>"
```

You can then run PredicTor experiments using the standard ns-3 commands, for example using the evaluation script at [scratch/tor-predictor-example.cc](scratch/tor-predictor-example.cc).
