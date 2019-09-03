#!/bin/bash

die()
{
    echo "$1" 1>&2
    exit 1
}

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ -f "$SCRIPT_DIR/.predictorrc" ]
then
    . "$SCRIPT_DIR/.predictorrc"
fi

if [ -z "$PREDICTOR_PATH_OPTIMIZER" ]
then
    die "Path to the optimizer not given. Please set PREDICTOR_PATH_OPTIMIZER."
fi

if [ -z "$PREDICTOR_PATH_COINHSL" ]
then
    die "Path to the coinhsl bin folder not given. Please set PREDICTOR_PATH_COINHSL."
fi

if [ -z "$PREDICTOR_PATH_CONDA" ]
then
    die "Path to the conda installation not given. Please set PREDICTOR_PATH_CONDA."
fi

eval "$($PREDICTOR_PATH_CONDA/bin/conda shell.bash hook)"

cd $PREDICTOR_PATH_OPTIMIZER
conda activate py36_64bit_new
cd - 2>&1 > /dev/null

# make MA27 solver available
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PREDICTOR_PATH_COINHSL

cat - | PYTHONPATH=$PREDICTOR_PATH_OPTIMIZER/OTS python3 $SCRIPT_DIR/solver.py # | tee -a /tmp/pytalk.rawout
# cat - | PYTHONPATH=$PREDICTOR_PATH_OPTIMIZER/OTS python3 $SCRIPT_DIR/solver.py | tee -a /tmp/pytalk.rawout
