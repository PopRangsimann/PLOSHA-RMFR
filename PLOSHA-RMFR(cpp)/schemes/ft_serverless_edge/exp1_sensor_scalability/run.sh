#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/../src" && pwd)"
DATASET="$(cd "$SCRIPT_DIR/../../../dataset" && pwd)/plosha_dataset.csv"
BIN="$SRC_DIR/ft_serverless_sim"

if [ ! -f "$BIN" ]; then
    g++ -O2 -std=c++17 -o "$BIN" "$SRC_DIR"/main.cpp "$SRC_DIR"/ft_network.cpp "$SRC_DIR"/ft_algorithms.cpp "$SRC_DIR"/ft_experiments.cpp
fi

OUTPUT="$SCRIPT_DIR/results.csv"
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > "$OUTPUT"
for n in 500 1000 1500 2000 2500 3000 3500 4000 4500 5000; do
    "$BIN" --experiment 1 --variable $n --cloudlets 10 --dataset "$DATASET" >> "$OUTPUT"
done
echo "exp1 done: $OUTPUT"
