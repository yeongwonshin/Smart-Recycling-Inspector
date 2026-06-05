#!/usr/bin/env bash
set -euo pipefail

SERVER="${SERVER:-localhost:50051}"
OUT_DIR="${OUT_DIR:-results}"
mkdir -p "${OUT_DIR}"

if [[ ! -x "./build/cpp/recycling_benchmark" ]]; then
  echo "benchmark executable not found. Build first:"
  echo "  cmake -S backend/cpp -B build/cpp -DCMAKE_BUILD_TYPE=Release"
  echo "  cmake --build build/cpp -j"
  exit 1
fi

./build/cpp/recycling_benchmark \
  --server "${SERVER}" \
  --out "${OUT_DIR}/benchmark.csv" \
  --images 128 \
  --width 640 \
  --height 480 \
  --threads 1,2,4,8 \
  --batches 8,16,32,64

python3 experiments/plot_results.py --input "${OUT_DIR}/benchmark.csv" --out "${OUT_DIR}/graphs"

echo "Benchmark CSV: ${OUT_DIR}/benchmark.csv"
echo "Graphs: ${OUT_DIR}/graphs"
