# 최종 보고서 작성 체크리스트

## 1. Abstract & Problem Definition

본 프로젝트는 캠퍼스와 공공장소에서 발생하는 분리배출 오류를 줄이기 위한 AI 기반 스마트 분리수거 판별 시스템을 구현한다. 사용자가 업로드한 폐기물 이미지를 분석하여 플라스틱, 종이, 캔, 유리, 음식물, 일반쓰레기 등으로 분류하고, 라벨 미제거, 음식물 오염, 혼합 재질 위험과 같은 오염 상태를 함께 판단한다. 이 문제는 단순 이미지 분류를 넘어 여러 이미지를 빠르게 처리해야 하는 데이터 집약적 문제이므로, 이미지 배치 전처리와 특징 추출 과정에서 CPU 병목이 발생한다. 따라서 본 시스템은 gRPC 기반 분산 통신 구조와 OpenMP 기반 병렬 이미지 처리 엔진을 결합하여 처리량과 지연시간을 개선하는 것을 목표로 한다.

## 2. System Architecture Diagram

보고서에는 다음 경계를 반드시 표시한다.

```text
React Dashboard
  -> HTTP multipart upload
Node Gateway
  -> gRPC + Protocol Buffers
C++ gRPC Inspection Server
  -> in-process call
OpenMP Parallel Inspection Engine
  -> category / contamination / timing result
```

필수 설명 요소:

- 데이터 흐름
- 네트워크 경계
- gRPC 미들웨어 단계
- OpenMP thread organization
- 병렬 가속 단계
- 결과 반환 및 로깅

## 3. Implementation Details

반드시 포함할 내용:

- `proto/recycling.proto`의 메시지 구조
- Node gateway가 브라우저 HTTP 요청을 gRPC 요청으로 변환하는 방식
- 이미지 bytes 직렬화 방식
- batch size와 thread count가 전달되는 방식
- OpenMP `parallel for schedule(dynamic)` 사용 이유
- 각 이미지가 독립적으로 처리되므로 synchronization overhead가 낮다는 점
- decode, preprocessing, feature extraction, inference, postprocessing timing 측정 방식

## 4. Performance Evaluation & Bottleneck Analysis

권장 실험:

1. Thread count scaling
   - thread count: 1, 2, 4, 8
   - 측정: throughput, speedup, latency

2. Batch size scaling
   - batch size: 8, 16, 32, 64
   - 측정: average latency, throughput

3. gRPC overhead analysis
   - e2e latency와 server wall time 비교
   - 차이를 network/serialization overhead로 근사

필수 그래프:

- Throughput vs. thread count
- Speedup vs. thread count
- Latency vs. batch size
- gRPC overhead vs. batch size
- Processing breakdown

병목 분석 예시:

- 이미지 크기가 커질수록 decode/preprocess 비용이 증가한다.
- batch size가 작으면 gRPC 호출 overhead의 비중이 커진다.
- thread 수가 증가하면 처음에는 throughput이 증가하지만, 일정 수준 이후에는 memory bandwidth와 OpenCV 내부 처리 비용 때문에 speedup이 둔화된다.
- 각 이미지는 독립 처리되므로 lock contention은 낮지만, 큰 이미지 batch에서는 memory allocation과 cache locality가 주요 병목이 된다.

## 5. Reproducibility

README에 다음 명령어가 포함되어야 한다.

```bash
docker compose up --build
cmake -S backend/cpp -B build/cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/cpp -j
./build/cpp/recycling_server 0.0.0.0:50051
./experiments/run_all.sh
python3 experiments/plot_results.py --input results/benchmark.csv --out results/graphs
```

## 6. 평가 기준 대응표

| 평가 기준 | 프로젝트 대응 |
| --- | --- |
| System Architecture & Middleware Design | React, Node gateway, C++ gRPC server, OpenMP engine 분리 |
| Parallel Computing & Acceleration | OpenMP batch image processing |
| Performance Evaluation & Analysis | thread/batch/gRPC overhead 실험 및 그래프 |
| Reproducibility & Documentation | Docker Compose, CMake, README, 실험 스크립트 |
