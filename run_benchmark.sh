#!/bin/bash
set -e

echo "=========================================="
echo "    PLOSHA-RMFR Benchmark Runner"
echo "=========================================="

ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"

# ---------------------------------------------------------
# 1a. PLOSHA-RMFR (Native — for fair comparison with baselines)
# ---------------------------------------------------------
echo "[1/5] Running PLOSHA-RMFR (Ours) — Native mode..."
cd "$ROOT_DIR/schemes/plosha_rmfr/src"
make clean && make
echo "  -> Executing natively (all experiments)..."
./plosha_rmfr --experiment all --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
echo "✔ PLOSHA-RMFR (native) completed."
echo ""

# ---------------------------------------------------------
# 1b. PLOSHA-RMFR (Intel SGX — for SGX overhead comparison)
# ---------------------------------------------------------
echo "[1b/5] Running PLOSHA-RMFR (Ours) in Intel SGX..."
# Copy native results to backup before SGX overwrites them
for d in exp1_sensor_scalability exp2_fog_scalability exp3_workload_intensity exp4_failure_rate exp5_loss_exposure exp6_recovery_comm exp7_aflto; do
  if [ -d "$ROOT_DIR/schemes/plosha_rmfr/$d" ]; then
    mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/${d}_native"
    cp -r "$ROOT_DIR/schemes/plosha_rmfr/$d/"* "$ROOT_DIR/schemes/plosha_rmfr/${d}_native/" 2>/dev/null || true
  fi
done

gramine-manifest --no-check -Dlog_level=error \
  -Ddataset_dir="$ROOT_DIR/dataset" \
  -Doutput_dir="$ROOT_DIR/schemes/plosha_rmfr" \
  plosha_rmfr.manifest.template plosha_rmfr.manifest

# Generate enclave signing key if it does not exist globally
if [ ! -f "$HOME/.config/gramine/enclave-key.pem" ]; then
  echo "  -> Generating SGX enclave private key..."
  gramine-sgx-gen-private-key
fi

echo "  -> Signing SGX enclave..."
gramine-sgx-sign --manifest plosha_rmfr.manifest --output plosha_rmfr.manifest.sgx

echo "  -> Executing via Gramine-SGX (all experiments)..."
gramine-sgx plosha_rmfr --experiment all --epochs 10 --dataset /dataset/plosha_dataset.csv --output /output
echo "✔ PLOSHA-RMFR (SGX) completed."
echo ""

# ---------------------------------------------------------
# 2. Fault-Tolerant Workflow (Ref[37])
# ---------------------------------------------------------
echo "[2/5] Running Fault-Tolerant Workflow (Ref[37])..."
cd "$ROOT_DIR/schemes/fault_tolerant_workflow/src"
make clean && make
echo "  -> Executing all experiments..."
./ftworkflow --experiment all --dataset "$DATASET_PATH"
echo "✔ FT-Workflow completed."
echo ""

# ---------------------------------------------------------
# 3. Robust IIoT (Ref[24])
# ---------------------------------------------------------
echo "[3/5] Running Robust IIoT (Ref[24])..."
cd "$ROOT_DIR/schemes/robust_iiot/src"
make clean && make
echo "  -> Running Exp 1 (Sensor Scalability)..."
./exp1_sensor_scalability
echo "  -> Running Exp 3 (Workload Intensity)..."
./exp3_workload_intensity
echo "  -> Running Exp 5 (Loss Exposure)..."
./exp5_loss_exposure
echo "✔ Robust IIoT completed."
echo ""

# ---------------------------------------------------------
# 4. FedDQN (Ref[22])
# ---------------------------------------------------------
echo "[4/5] Running FedDQN (Ref[22])..."
cd "$ROOT_DIR/schemes/fed_dqn/src"
make clean && make
echo "  -> Running Exp 1 (Sensor Scalability)..."
./exp1_sensor_scalability
echo "  -> Running Exp 2 (Fog Scalability)..."
./exp2_fog_scalability
echo "  -> Running Exp 3 (Workload Intensity)..."
./exp3_workload_intensity
echo "  -> Running Exp 4 (Failure Rate)..."
./exp4_failure_rate
echo "✔ FedDQN completed."
echo ""

# ---------------------------------------------------------
# 5. FT-Serverless Edge (Ref[38])
# ---------------------------------------------------------
echo "[5/5] Running FT-Serverless Edge (Ref[38])..."
cd "$ROOT_DIR/schemes/ft_serverless_edge/src"
make clean && make

echo "  -> Running Exp 1 (Sensor Scalability)..."
mkdir -p ../exp1_sensor_scalability
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp1_sensor_scalability/results.csv
for v in 500 1000 1500 2000 2500 3000 3500 4000 4500 5000; do
  ./ft_serverless_sim --experiment 1 --variable $v --sensors $v --dataset "$DATASET_PATH" >> ../exp1_sensor_scalability/results.csv
done

echo "  -> Running Exp 2 (Fog Scalability)..."
mkdir -p ../exp2_fog_scalability
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp2_fog_scalability/results.csv
for v in 5 10 15 20 25 30 35 40 45 50; do
  ./ft_serverless_sim --experiment 2 --variable $v --cloudlets $v --dataset "$DATASET_PATH" >> ../exp2_fog_scalability/results.csv
done

echo "  -> Running Exp 3 (Workload Intensity)..."
mkdir -p ../exp3_workload_intensity
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp3_workload_intensity/results.csv
for v in 1 2 3 4 5 6 7 8 9 10; do
  ./ft_serverless_sim --experiment 3 --variable $v --dataset "$DATASET_PATH" >> ../exp3_workload_intensity/results.csv
done

echo "  -> Running Exp 4 (Failure Rate)..."
mkdir -p ../exp4_failure_rate
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp4_failure_rate/results.csv
for v in 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20; do
  ./ft_serverless_sim --experiment 4 --variable $v --dataset "$DATASET_PATH" >> ../exp4_failure_rate/results.csv
done

echo "  -> Running Exp 5 (Loss Exposure)..."
mkdir -p ../exp5_loss_exposure
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp5_loss_exposure/results.csv
for v in 1 2 3 4 5 6 7 8 9 10 12 14 16 18 20; do
  ./ft_serverless_sim --experiment 5 --variable $v --dataset "$DATASET_PATH" >> ../exp5_loss_exposure/results.csv
done

echo "  -> Running Exp 6 (Recovery Communication)..."
mkdir -p ../exp6_recovery_comm
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp6_recovery_comm/results.csv
for v in 1 2 3 4 5 6 7 8 9 10; do
  ./ft_serverless_sim --experiment 6 --variable $v --dataset "$DATASET_PATH" >> ../exp6_recovery_comm/results.csv
done
echo "✔ FT-Serverless Edge completed."
echo ""

# ---------------------------------------------------------
# 6. Generate Plots
# ---------------------------------------------------------
echo "[6/6] Generating Comparison Plots..."
cd "$ROOT_DIR/plots"
python3 generate_plots.py
echo "✔ Plots generated in plots/output/"
echo ""
echo "=========================================="
echo "    Benchmark run fully complete!"
echo "=========================================="
