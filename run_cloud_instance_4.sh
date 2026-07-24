#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 4: Exp 4 (Loss Exposure) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 4 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"

# Rename PLOSHA-RMFR native output to _native for plot compatibility
for d in "$ROOT_DIR/schemes/plosha_rmfr/exp"*; do
  if [ -e "$d" ] && [[ ! "$d" == *"_native" ]]; then
    if [ -d "${d}_native" ]; then
      rm -rf "${d}_native"
    fi
    mv "$d" "${d}_native"
  fi
done


cd "$ROOT_DIR/schemes/robust_iiot/src" && make clean && make -j4
echo "  -> Running Exp 4 (Loss Exposure)..."
mkdir -p ../exp4_loss_exposure
./exp5_loss_exposure

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

echo "  -> Running Exp 4 (Loss Exposure)..."
mkdir -p ../exp4_loss_exposure
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp4_loss_exposure/results.csv
for v in 1 2 3 4 5 6 7 8 9 10 12 14 16 18 20; do
  run_avg 5 $v >> ../exp4_loss_exposure/results.csv
done
echo "Instance 4 Completed!"
