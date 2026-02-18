# spsc-queue-cpp

## Build
Configure:
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
Build: 
`cmake --build build`

## Unit Tests
Build tests:
`cmake --build build --target queue_tests`

Run tests:
`ctest --test-dir build --output-on-failure`

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

Notes:
- Benchmark always runs all queues: `simple`, `spin`, `wait`.
- Repeat count is fixed in `main.cpp`.
- Workload skew is induced with fixed busy-cycle work (no sleeps).

## Coverage (Linux + GCC)
Coverage is supported only on Linux with GCC via `ENABLE_COVERAGE`.

Configure with coverage enabled:
`cmake -S . -B build -DENABLE_COVERAGE=ON`

Build and run unit tests:
`cmake --build build --target queue_tests`
`ctest --test-dir build --output-on-failure`

Generate HTML report (requires `lcov` and `genhtml`):
```
lcov --capture --directory build --output-file build/coverage.info
lcov --remove build/coverage.info '/usr/*' '*/_deps/*' '*test*' --output-file build/coverage.filtered.info
genhtml build/coverage.filtered.info --output-directory build/coverage-html
```

Open report:
`build/coverage-html/index.html`
