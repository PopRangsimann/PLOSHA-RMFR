#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 3: Exp 3 (Failure Rate) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 3 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"

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
echo "  -> Running Exp 3 (Failure Rate)..."
mkdir -p ../exp3_failure_rate
./exp4_failure_rate

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

echo "  -> Running Exp 3 (Failure Rate)..."
mkdir -p ../exp3_failure_rate
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp3_failure_rate/results.csv
for v in 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20 0.25 0.30 0.35; do
  run_avg 4 $v >> ../exp3_failure_rate/results.csv
done
echo "Instance 3 Completed!"
