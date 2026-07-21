#!/bin/bash
# Experiment 2 -- scheduling efficiency. Native run, no SGX required.
#
# NOTE: the binary's flag is --experiment 9, not 2. The numbering is offset;
# runExp9_SchedulingEfficiency sweeps fog nodes {5..50} and writes to
# exp2_scheduling_efficiency/.
#
#   ./run_exp2.sh                 # 30 epochs, incremental build
#   EPOCHS=5 ./run_exp2.sh        # short debug run
#   REBUILD=1 ./run_exp2.sh       # force make clean first
#   DEBUG=1 ./run_exp2.sh         # trace every command

EXP_FLAG=9
EXP_NAME="Experiment 2 (Scheduling Efficiency)"
OUT_SUBDIR="exp2_scheduling_efficiency"

source "$(dirname "${BASH_SOURCE[0]}")/run_exp_common.sh"
run_experiment
