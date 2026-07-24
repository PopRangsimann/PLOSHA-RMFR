#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 2: Exp 2 (Scheduling Efficiency) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 2 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"

# Rename PLOSHA-RMFR native output to _native for plot compatibility
for d in "$ROOT_DIR/schemes/plosha_rmfr/exp"*; do
  if [ -e "$d" ] && [[ ! "$d" == *"_native" ]]; then
    if [ -d "${d}_native" ]; then
      rm -rf "${d}_native"
    fi
    mv "$d" "${d}_native"
  fi
done


cd "$ROOT_DIR/schemes/fed_dqn/src" && make clean && make -j4
echo "  -> Running Exp 2 (Scheduling Efficiency)..."
mkdir -p ../exp2_scheduling_efficiency
./exp9_scheduling_efficiency

cd "$ROOT_DIR/schemes/ft_serverless_edge/src" && make clean && make -j4

NUM_SEEDS=10
BASE_SEED=12345
run_avg() {
  local exp=$1 var=$2; shift 2
  local sum1=0 sum2=0 sum3=0
  for s in $(seq $BASE_SEED $((BASE_SEED + NUM_SEEDS - 1))); do
    local line
    line=$(./ft_serverless_sim --experiment $exp --variable $var --seed $s "$@" --dataset "$DATASET_PATH")
    local m1 m2 m3
    m1=$(echo "$line" | awk -F',' '{print $2}')
    m2=$(echo "$line" | awk -F',' '{print $3}')
    m3=$(echo "$line" | awk -F',' '{print $4}')
    sum1=$(echo "$sum1 + ${m1:-0}" | bc -l)
    sum2=$(echo "$sum2 + ${m2:-0}" | bc -l)
    sum3=$(echo "$sum3 + ${m3:-0}" | bc -l)
  done
  printf "%s,%.6f,%.6f,%.6f\n" "$var" \
    "$(echo "$sum1 / $NUM_SEEDS" | bc -l)" \
    "$(echo "$sum2 / $NUM_SEEDS" | bc -l)" \
    "$(echo "$sum3 / $NUM_SEEDS" | bc -l)"
}

echo "  -> Running Exp 2 (Scheduling Efficiency)..."
mkdir -p ../exp2_scheduling_efficiency
echo "num_fog_nodes,scheduling_latency_ms,workload_imbalance" > ../exp2_scheduling_efficiency/results.csv
for v in 5 10 15 20 25 30 35 40 45 50; do
  line=$(./ft_serverless_sim --experiment 9 --variable $v --cloudlets $v --sensors 12600 --seed $BASE_SEED --dataset "$DATASET_PATH")
  sched_lat=$(echo "$line" | awk -F',' '{print $2}')
  imbalance=$(echo "$line" | awk -F',' '{print $3}')
  printf "%s,%.6f,%.6f\n" "$v" "${sched_lat:-0}" "${imbalance:-0}" >> ../exp2_scheduling_efficiency/results.csv
done
echo "Instance 2 Completed!"
