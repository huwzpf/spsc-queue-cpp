# spsc-queue-cpp

C++20 project implementing and comparing two bounded single-producer/single-consumer (SPSC) queues:

- `simple_spsc_queue<T>`: mutex + condition variable implementation.
- `atomic_spsc_queue<T>`: Ring buffer using atomics with spin/yield waits.

The project includes:
- A benchmark executable (`bench`) for comparing queue behavior across scenarios.
- A GoogleTest suite (`queue_tests`) that runs the same black-box tests for both queue variants and for two payload types (`int`, `std::vector<int>`).

## Project Structure
```
.
├── include/
│   ├── atomic_spsc_queue.hpp
│   └── simple_spsc_queue.hpp
├── src/
│   └── main.cpp
├── tests/
│   └── queue_tests.cpp
├── CMakeLists.txt
└── README.md
```

## API Summary
Each queue exposes:

- `bool try_push(U&& item)`
- `bool push(U&& item)` (blocking)
- `std::optional<T> try_pop()`
- `std::optional<T> pop()` (blocking)
- `void close() const`
- `bool done() const` (`closed && empty`)
- `std::size_t capacity() const`

Producer thread calls `push()` to add items, and consumer thread calls `pop()` to retrieve them. Both operations have non-blocking (`try_push()`, `try_pop()`) and blocking variants. Push operations return `false` if the queue is full or already closed, and pop operations return `std::nullopt` if the queue is empty. Blocking variants will wait until space/items are available or until the queue is closed.
The `close()` method signals that no more items will be produced, allowing the consumer to detect completion via `done()`.

## Assumptions
- Exactly one producer thread and one consumer thread access a queue instance.
- Capacity is fixed at construction and must be greater than zero.
- Queue object lifetime must exceed the lifetime of producer/consumer threads that access it.
- Destructors call `close()` as best-effort wakeup, but caller is still responsible for orderly thread shutdown.
- Atomic queue currently uses `std::vector<T>` as the ring storage by design. This was chosen to simplify implementation, as move-assignment into pre-constructed slots  requires less manual lifetime management code.
  An alternative would be raw uninitialized storage  - `std::byte[]` / `std::aligned_storage` + placement new/destruct. Tradeoff: Raw storage can remove the `default_initializable<T>` requirement and avoid eager default construction of all slots, but it adds complexity, so it was not implemented.

## Build
Configure and build:

```bash
cmake -S . -B build
cmake --build build -j
```

Main targets:
- `bench`
- `queue_tests`

## Unit Tests
```bash
cmake --build build --target queue_tests -j
ctest --test-dir build --output-on-failure
```

Stability test:
```bash
ctest --test-dir build --output-on-failure --repeat until-fail:1000 -R "FunctionalTest" --timeout 5
```

## Benchmark
```bash
cmake --build build --target bench -j
./build/bench
```

Benchmark scenarios are all executed for fixed item count of 1 000 000 elements. The elements are pushed to the queue by a producer thread and consumed by consumer thread. Overall execution time is measured across 20 repeats. Executed scenarios along with tested queue capacities are:
- blocking functions (`push()` / `pop()`) for queue of `int` types with capacities: 64, 1024, 8192
- nonblocking functions (`try_push()` / `try_pop()`) for queue of `int` with capacities: 64, 1024, 8192
- blocking functions for queue of bigger types (64 bytes) with capacity 1024
- producer-heavy workload - blocking functions  for queue of `int` types with capacity 1024
- consumer-heavy workload - blocking functions  for queue of `int` types with capacity 1024

Reported metrics:
- `avg ms`
- `stdev ms`

### Example benchmark results
```
queue   mode           scenario       cap            avg ms       stdev ms       
simple  blocking       standard       64             446.72       60.03        
simple  nonblocking    standard       64             221.05       21.51        
simple  blocking       standard       1024           171.75       8.10         
simple  nonblocking    standard       1024           187.84       5.31         
simple  blocking       standard       8192           158.76       5.59         
simple  nonblocking    standard       8192           179.29       20.95        
simple  blocking       big-payload    1024           247.37       21.18        
simple  blocking       producer-heavy 1024           196.34       11.25        
simple  blocking       consumer-heavy 1024           222.42       7.34         
atomic  blocking       standard       64             45.36        3.34         
atomic  nonblocking    standard       64             43.39        1.51         
atomic  blocking       standard       1024           46.31        5.66         
atomic  nonblocking    standard       1024           44.07        2.89         
atomic  blocking       standard       8192           47.48        15.41        
atomic  nonblocking    standard       8192           44.58        3.33         
atomic  blocking       big-payload    1024           38.00        2.18         
atomic  blocking       producer-heavy 1024           60.37        4.98         
atomic  blocking       consumer-heavy 1024           50.39        2.47  
```

***Note about benchmark results:***
The `simple_spsc_queue` is generally slower than the `atomic_spsc_queue` due to the overhead of mutexes and condition variables, but the benchmark does not measure CPU utilization and does not cature the fact that `atomic_spsc_queue` is more CPU intensive due to spin waits.



## Coverage (Linux + GCC)
Coverage is enabled only for Linux + GCC via `ENABLE_COVERAGE`.

```bash
cmake -S . -B build -DENABLE_COVERAGE=ON
cmake --build build --target queue_tests -j
ctest --test-dir build --output-on-failure
```

Generate HTML report (requires `lcov` and `genhtml`):

```bash
lcov --capture --directory build --output-file build/coverage.info
lcov --remove build/coverage.info '/usr/*' '*/_deps/*' '*test*' --output-file build/coverage.filtered.info
genhtml build/coverage.filtered.info --output-directory build/coverage-html
```

Open:
- `build/coverage-html/index.html`

Current coverage:

```
Summary coverage rate:
  lines......: 100.0% (117 of 117 lines)
  functions..: 100.0% (48 of 48 functions)
```
