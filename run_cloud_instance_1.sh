#!/bin/bash
set -e
ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
echo "=== Instance 1: Exp 1 & Exp 6 (PLOSHA Only) ==="

cd "$ROOT_DIR/schemes/plosha_rmfr/src" && make clean && make -j4
./plosha_rmfr --experiment 1 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
./plosha_rmfr --experiment 6 --epochs 10 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"

# Rename PLOSHA-RMFR native output to _native for plot compatibility
for d in "$ROOT_DIR/schemes/plosha_rmfr/exp"*; do
  if [ -e "$d" ] && [[ ! "$d" == *"_native" ]]; then
    if [ -d "${d}_native" ]; then
      rm -rf "${d}_native"
    fi
    mv "$d" "${d}_native"
  fi
done


echo "Instance 1 Completed!"
