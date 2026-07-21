#!/bin/bash
set -e

echo "=========================================="
echo "    Running Experiment 2 (Scheduling Efficiency)"
echo "=========================================="

ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"

echo "[1/4] Running PLOSHA-RMFR (Ours) - Scheduling Efficiency"
cd "$ROOT_DIR/schemes/plosha_rmfr/src"
make clean && make
if [ ! -d "$ROOT_DIR/schemes/plosha_rmfr/exp2_scheduling_efficiency" ]; then
  mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp2_scheduling_efficiency"
fi
./plosha_rmfr --experiment 9 --epochs 30 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"

echo "[2/4] Running Fault-Tolerant Workflow (Ref[37]) - Scheduling Efficiency"
cd "$ROOT_DIR/schemes/fault_tolerant_workflow/src"
make clean && make
mkdir -p ../exp2_scheduling_efficiency
./ftworkflow --experiment 9 --dataset "$DATASET_PATH"

echo "[3/4] Running FedDQN (Ref[22]) - Scheduling Efficiency"
cd "$ROOT_DIR/schemes/fed_dqn/src"
make clean && make
mkdir -p ../exp2_scheduling_efficiency
./exp9_scheduling_efficiency

echo "[4/4] Running FT-Serverless Edge (Ref[38]) - Scheduling Efficiency"
cd "$ROOT_DIR/schemes/ft_serverless_edge/src"
make clean && make

BASE_SEED=12345
mkdir -p ../exp2_scheduling_efficiency
echo "num_fog_nodes,scheduling_latency_ms,workload_imbalance" > ../exp2_scheduling_efficiency/results.csv
for v in 5 10 15 20 25 30 35 40 45 50; do
  line=$(./ft_serverless_sim --experiment 9 --variable $v --cloudlets $v --sensors 12600 --seed $BASE_SEED --dataset "$DATASET_PATH")
  sched_lat=$(echo "$line" | awk -F',' '{print $2}')
  imbalance=$(echo "$line" | awk -F',' '{print $3}')
  printf "%s,%.6f,%.6f\n" "$v" "${sched_lat:-0}" "${imbalance:-0}" >> ../exp2_scheduling_efficiency/results.csv
done

echo "✔ Experiment 2 (Scheduling Efficiency) completed."
