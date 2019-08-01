#!/bin/bash

SOLVER_REPO="/home/christoph/forschung/paper-predictor/toropti_scheduler"

# >>> conda init >>>
# !! Contents within this block are managed by 'conda init' !!
__conda_setup="$(CONDA_REPORT_ERRORS=false '/home/christoph/anaconda3/bin/conda' shell.bash hook 2> /dev/null)"
if [ $? -eq 0 ]; then
    \eval "$__conda_setup"
else
    if [ -f "/home/christoph/anaconda3/etc/profile.d/conda.sh" ]; then
        . "/home/christoph/anaconda3/etc/profile.d/conda.sh"
        CONDA_CHANGEPS1=false conda activate base
    else
        # \export PATH="/home/christoph/anaconda3/bin:$PATH"
        export PATH="/home/christoph/anaconda3/bin:$PATH"
    fi
fi
unset __conda_setup
# <<< conda init <<<

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd $SOLVER_REPO
conda activate py36_64bit_new
cd - 2>&1 > /dev/null

# make MA27 solver available
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/christoph/forschung/paper-predictor/casadi/hsl-bin/lib

# cat - | PYTHONPATH=$SOLVER_REPO/OTS python3 $SCRIPT_DIR/solver.py # | tee -a /tmp/pytalk.rawout
cat - | PYTHONPATH=$SOLVER_REPO/OTS python3 $SCRIPT_DIR/solver.py | tee -a /tmp/pytalk.rawout
