#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 6: SGX Overhead (PLOSHA only) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src"
make clean && make -j4

gramine-manifest --no-check -Dlog_level=error \
  -Ddataset_dir="$ROOT_DIR/dataset" \
  -Doutput_dir="$ROOT_DIR/schemes/plosha_rmfr" \
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
