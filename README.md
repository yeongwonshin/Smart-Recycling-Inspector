# AI-Powered Smart Recycling Inspection Pipeline

**Course fit:** Parallel & Distributed Computing course project for undergraduate scope.

This project implements a React-based AI waste inspection platform. Users upload one or more waste images through the React dashboard. A Node.js gateway receives the images and forwards them through **gRPC** to a C++ inspection server. The C++ server performs **OpenMP-parallel batch preprocessing, feature extraction, contamination scoring, and category inference**, then returns category, confidence, contamination status, explanation, disposal guide, and performance breakdown.

The project is intentionally designed around the project rubric:

- **Distributed middleware:** gRPC + Protocol Buffers between gateway and C++ inspection server.
- **Parallel computing:** OpenMP-based parallel processing for image batches.
- **Structural decoupling:** React UI, Node gateway, C++ gRPC service, engine module, experiments, and documentation are separated.
- **Scalability evaluation:** benchmark client and plotting script generate throughput, latency, and speedup graphs.

---

## 1. System Architecture

```text
┌─────────────────────────────────────────────────────────────────────┐
│                         React Dashboard                              │
│  - image upload                                                       │
│  - batch size / thread count controls                                 │
│  - result cards, explanations, disposal guide, performance charts     │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ HTTP multipart/form-data
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Node.js Gateway                              │
│  - REST endpoint for browser                                          │
│  - converts uploaded files into protobuf bytes                        │
│  - forwards request to C++ server through gRPC                        │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ gRPC + Protocol Buffers
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     C++ gRPC Inspection Server                       │
│  - receives image batch                                               │
│  - calls OpenMP parallel inspection engine                            │
│  - returns prediction, contamination score, timing breakdown          │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ in-process function calls
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   OpenMP SmartWasteInspector Engine                  │
│  - parallel image decode/preprocess/feature extraction                │
│  - heuristic AI classifier fallback                                  │
│  - contamination rule model                                           │
│  - no shared mutable state per image; results written by index        │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. Why this satisfies the assignment

| Assignment requirement | Implementation in this repository |
| --- | --- |
| Parallel acceleration | `backend/cpp/src/inspector_engine.cpp` uses `#pragma omp parallel for schedule(dynamic)` for image-batch processing. |
| Distributed communication | `proto/recycling.proto` defines gRPC services; Node gateway calls the C++ gRPC server. |
| Structural decoupling | UI, gateway, protobuf schema, C++ server, engine, benchmark tools, and docs are separated. |
| Scalability analysis | `backend/cpp/src/benchmark_client.cpp`, `experiments/run_all.sh`, and `experiments/plot_results.py` produce quantitative graphs. |
| Reproducibility | Docker Compose, CMake, README instructions, and benchmark scripts are included. |

---

## 3. Technology Stack

- **Frontend:** React + Vite
- **Browser gateway:** Node.js + Express + Multer
- **Distributed middleware:** gRPC + Protocol Buffers
- **Parallel computing:** C++17 + OpenMP
- **Image processing:** OpenCV
- **Build:** CMake
- **Deployment:** Docker Compose
- **Experiment graphs:** Python + pandas + matplotlib

---

## 4. Quick Start with Docker Compose

```bash
docker compose up --build
```

Open the dashboard:

```text
http://localhost:5173
```

Services:

```text
React dashboard:       http://localhost:5173
Node gateway:          http://localhost:8080
C++ gRPC server:       localhost:50051
```

Upload images from `data/sample_images/` or your own waste images.

---

## 5. Run the C++ server locally

Install dependencies on Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake protobuf-compiler protobuf-compiler-grpc \
  libprotobuf-dev libgrpc++-dev libopencv-dev pkg-config
```

Build:

```bash
cmake -S backend/cpp -B build/cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/cpp -j
```

Run server:

```bash
./build/cpp/recycling_server 0.0.0.0:50051
```

Run benchmark client while the server is running:

```bash
mkdir -p results
./build/cpp/recycling_benchmark \
  --server localhost:50051 \
  --out results/benchmark.csv \
  --images 128 \
  --width 640 \
  --height 480 \
  --threads 1,2,4,8 \
  --batches 8,16,32,64
```

Generate graphs:

```bash
python3 -m pip install -r experiments/requirements.txt
python3 experiments/plot_results.py --input results/benchmark.csv --out results/graphs
```

---

## 6. Run frontend and gateway locally

Run Node gateway:

```bash
cd backend/gateway
npm install
GRPC_TARGET=localhost:50051 npm start
```

Run React frontend:

```bash
cd frontend
npm install
VITE_API_BASE_URL=http://localhost:8080 npm run dev -- --host 0.0.0.0
```

---

## 7. Core API

### gRPC service

Defined in `proto/recycling.proto`:

```proto
service RecyclingInspector {
  rpc AnalyzeImages(AnalyzeRequest) returns (AnalyzeResponse);
  rpc Benchmark(BenchmarkRequest) returns (BenchmarkResponse);
}
```

### HTTP gateway endpoint

```http
POST /api/analyze
Content-Type: multipart/form-data

fields:
  images: one or more image files
  thread_count: OpenMP thread count
  batch_size: logical batch size recorded for experiments
```

---

## 8. Parallelization Strategy

The main parallel region is image-level parallelism:

```cpp
#pragma omp parallel for schedule(dynamic)
for (int i = 0; i < static_cast<int>(jobs.size()); ++i) {
    results[i] = AnalyzeOne(jobs[i]);
}
```

Each image can be decoded, resized, converted to HSV/grayscale, and scored independently. Therefore the implementation avoids shared mutable state and writes each result into a preallocated vector slot. This keeps synchronization overhead low.

Additional per-image computations include:

- decode timing
- resize and normalization
- HSV/grayscale conversion
- edge-density extraction
- color-region ratios
- contamination score
- category inference
- disposal recommendation generation

---

## 9. Performance Experiments

The benchmark client supports the following evaluation dimensions:

1. **Thread count scaling**
   - threads: 1, 2, 4, 8
   - fixed image count and image size
   - output: throughput and speedup

2. **Batch size scaling**
   - batch sizes: 8, 16, 32, 64
   - fixed thread count
   - output: average latency and throughput

3. **Image size scaling**
   - modify `--width` and `--height`
   - output: preprocessing and inference timing growth

4. **Network/serialization overhead**
   - benchmark client measures end-to-end gRPC latency
   - server also reports internal processing breakdown
   - difference approximates communication and serialization overhead

Generated graphs:

```text
results/graphs/throughput_vs_threads.png
results/graphs/speedup_vs_threads.png
results/graphs/latency_vs_batch_size.png
results/graphs/processing_breakdown.png
```

---

## 10. Suggested Report Structure

Use `docs/report_checklist_ko.md` for a Korean report skeleton that matches the project description.

Recommended sections:

1. Abstract & Problem Definition
2. System Architecture Diagram
3. Implementation Details
4. Performance Evaluation & Bottleneck Analysis
5. Reproducibility

---

## 11. Important Notes

- The included classifier is a lightweight heuristic AI module designed to make the system runnable without a large external model file.
- For a stronger final version, replace `HeuristicClassify()` in `inspector_engine.cpp` with an ONNX Runtime model call while keeping the same gRPC/OpenMP architecture.
- The main grading strength of this project is the system pipeline, not perfect recycling classification accuracy.
