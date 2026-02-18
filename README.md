# spsc-queue-cpp

## Build
Configure and build:
`cmake -S . -B build && cmake --build build`

Build benchmark target only:
`cmake --build build --target bench`

## Benchmark
Run benchmark:
`./build/bench`

The benchmark includes:
- `blocking standard` across capacities `64`, `1024`, `8192`
- `nonblocking standard` across capacities `64`, `1024`, `8192`
- `blocking big-payload` at capacity `1024`
- `blocking producer-heavy` at capacity `1024`
- `blocking consumer-heavy` at capacity `1024`

Reported metrics:
- `avg ms`
- `stdev ms`
- `avg ops/s`
- `stdev ops/s`

Notes:
- Benchmark always runs all queues: `simple`, `spin`, `wait`.
- Repeat count is fixed in `main.cpp`.
- Workload skew is induced with fixed busy-cycle work (no sleeps).
