#!/bin/bash
<<<<<<< HEAD
# Experiment 1 -- aggregation ablation. Native run, no SGX required.
#
# NOTE: the binary's flag is --experiment 8, not 1. The numbering is offset;
# runExp8_AblationAggregation writes to exp1_ablation_aggregation/.
#
#   ./run_exp1.sh                      # 30 epochs, incremental build
#   EPOCHS=2 ./run_exp1.sh             # short debug run
#   FAILURE_RATE=0.10 ./run_exp1.sh    # sensitivity analysis
#   REBUILD=1 ./run_exp1.sh            # force make clean first
#   DEBUG=1 ./run_exp1.sh              # trace every command
#
# Without FAILURE_RATE the experiment uses EXP1_ABLATION_FAILURE_RATE (0.50,
# see config.hpp). That is outside the 0.02-0.35 range swept by the
# failure-rate experiment and must be stated wherever these results appear.

EXP_FLAG=8
EXP_NAME="Experiment 1 (Aggregation Ablation)"
OUT_SUBDIR="exp1_ablation_aggregation"

source "$(dirname "${BASH_SOURCE[0]}")/run_exp_common.sh"
run_experiment
=======
set -e

echo "=========================================="
echo "    Running Experiment 1 (Ablation)"
echo "=========================================="

ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"

echo "[1/1] Running PLOSHA-RMFR (Ours) - Experiment 1 (Ablation)"
cd "$ROOT_DIR/schemes/plosha_rmfr/src"
make clean && make

if [ ! -d "$ROOT_DIR/schemes/plosha_rmfr/exp1_ablation_aggregation" ]; then
  mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp1_ablation_aggregation"
fi

./plosha_rmfr --experiment 8 --epochs 30 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"

echo "✔ Experiment 1 (Ablation) completed."
>>>>>>> ff7433bfa40cda9811d3441c063eeaa8cd9cafe3
