import os
import stat

def write_script(filename, content):
    with open(filename, "w", newline='\n') as f:
        f.write(content)
    st = os.stat(filename)
    os.chmod(filename, st.st_mode | stat.S_IEXEC)

ft_serverless_helper = """
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
  printf "%s,%.6f,%.6f,%.6f\\n" "$var" \\
    "$(echo "$sum1 / $NUM_SEEDS" | bc -l)" \\
    "$(echo "$sum2 / $NUM_SEEDS" | bc -l)" \\
    "$(echo "$sum3 / $NUM_SEEDS" | bc -l)"
}
"""

backup_plosha = """
# Rename PLOSHA-RMFR native output to _native for plot compatibility
for d in "$ROOT_DIR/schemes/plosha_rmfr/exp"*; do
  if [ -e "$d" ] && [[ ! "$d" == *"_native" ]]; then
    if [ -d "${d}_native" ]; then
      rm -rf "${d}_native"
    fi
    mv "$d" "${d}_native"
  fi
done
"""

script1 = f"""#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 1: Exp 1 & Exp 6 (PLOSHA Only) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 1 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
./plosha_rmfr --experiment 6 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
{backup_plosha}

echo "Instance 1 Completed!"
"""

script2 = f"""#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 2: Exp 2 (Scheduling Efficiency) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 2 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
{backup_plosha}

cd "$ROOT_DIR/schemes/fed_dqn/src" && make clean && make -j4
echo "  -> Running Exp 2 (Scheduling Efficiency)..."
mkdir -p ../exp2_scheduling_efficiency
./exp9_scheduling_efficiency

cd "$ROOT_DIR/schemes/ft_serverless_edge/src" && make clean && make -j4
{ft_serverless_helper}
echo "  -> Running Exp 2 (Scheduling Efficiency)..."
mkdir -p ../exp2_scheduling_efficiency
echo "num_fog_nodes,scheduling_latency_ms,workload_imbalance" > ../exp2_scheduling_efficiency/results.csv
for v in 5 10 15 20 25 30 35 40 45 50; do
  line=$(./ft_serverless_sim --experiment 9 --variable $v --cloudlets $v --sensors 12600 --seed $BASE_SEED --dataset "$DATASET_PATH")
  sched_lat=$(echo "$line" | awk -F',' '{{print $2}}')
  imbalance=$(echo "$line" | awk -F',' '{{print $3}}')
  printf "%s,%.6f,%.6f\\n" "$v" "${{sched_lat:-0}}" "${{imbalance:-0}}" >> ../exp2_scheduling_efficiency/results.csv
done
echo "Instance 2 Completed!"
"""

script3 = f"""#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 3: Exp 3 (Failure Rate) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 3 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
{backup_plosha}

cd "$ROOT_DIR/schemes/fed_dqn/src" && make clean && make -j4
echo "  -> Running Exp 3 (Failure Rate)..."
mkdir -p ../exp3_failure_rate
./exp4_failure_rate

cd "$ROOT_DIR/schemes/ft_serverless_edge/src" && make clean && make -j4
{ft_serverless_helper}
echo "  -> Running Exp 3 (Failure Rate)..."
mkdir -p ../exp3_failure_rate
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp3_failure_rate/results.csv
for v in 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20 0.25 0.30 0.35; do
  run_avg 4 $v >> ../exp3_failure_rate/results.csv
done
echo "Instance 3 Completed!"
"""

script4 = f"""#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 4: Exp 4 (Loss Exposure) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 4 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
{backup_plosha}

cd "$ROOT_DIR/schemes/robust_iiot/src" && make clean && make -j4
echo "  -> Running Exp 4 (Loss Exposure)..."
mkdir -p ../exp4_loss_exposure
./exp5_loss_exposure

cd "$ROOT_DIR/schemes/ft_serverless_edge/src" && make clean && make -j4
{ft_serverless_helper}
echo "  -> Running Exp 4 (Loss Exposure)..."
mkdir -p ../exp4_loss_exposure
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp4_loss_exposure/results.csv
for v in 1 2 3 4 5 6 7 8 9 10 12 14 16 18 20; do
  run_avg 5 $v >> ../exp4_loss_exposure/results.csv
done
echo "Instance 4 Completed!"
"""

script5 = f"""#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 5: Exp 5 (Recovery Comm) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 5 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
{backup_plosha}

echo "Instance 5 Completed!"
"""

script6 = f"""#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 6: SGX Overhead (PLOSHA only) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src"
make clean && make -j4

gramine-manifest --no-check -Dlog_level=error \\
  -Ddataset_dir="$ROOT_DIR/dataset" \\
  -Doutput_dir="$ROOT_DIR/schemes/plosha_rmfr" \\
  plosha_rmfr.manifest.template plosha_rmfr.manifest

if [ ! -f "$HOME/.config/gramine/enclave-key.pem" ]; then
  echo "  -> Generating SGX enclave private key..."
  gramine-sgx-gen-private-key
fi

echo "  -> Signing SGX enclave..."
gramine-sgx-sign --manifest plosha_rmfr.manifest --output plosha_rmfr.manifest.sgx

echo "  -> Executing via Gramine-SGX (all experiments)..."
gramine-sgx plosha_rmfr --experiment all --epochs 10 --dataset /dataset/plosha_dataset.csv --output /output
echo "Instance 6 Completed!"
"""

write_script("run_cloud_instance_1.sh", script1)
write_script("run_cloud_instance_2.sh", script2)
write_script("run_cloud_instance_3.sh", script3)
write_script("run_cloud_instance_4.sh", script4)
write_script("run_cloud_instance_5.sh", script5)
write_script("run_cloud_instance_6.sh", script6)
