#!/bin/bash
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
