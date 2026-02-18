# spsc-queue-cpp

C++20 project implementing a bounded single-producer/single-consumer (SPSC) queuee - implemented with ring buffer using atomics and spin/yield waits for blocking access.

The project includes:
- A benchmark executable (`bench`) for comparing queue behavior across scenarios.
- A GoogleTest suite (`queue_tests`) that runs  black-box tests for two payload types (`int`, `std::vector<int>`).

*This is `interview` branch of this project, on `main` branch there is also `simple_spsc_queue` implementation using mutexes and condition variables, which is not included in this branch to keep the code shorter*

*Tested using g++ (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0*
## Project Structure

 - ***Queue implementation*** : `include/spsc_queue.hpp`
 - ***Benchmark***: `src/main.cpp`
 - ***Tests***: `tests/queue_tests.cpp`

```
.
├── include/
│   ├── spsc_queue.hpp
├── src/
│   └── main.cpp
├── tests/
│   └── queue_tests.cpp
├── CMakeLists.txt
└── README.md
```

## API Summary
API exposed by the queue:

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
mode           scenario                 cap            avg ms       stdev ms     
blocking       standard (int)           64             49.88        6.04         
nonblocking    standard (int)           64             46.53        5.93         
blocking       standard (int)           1024           45.14        1.18         
nonblocking    standard (int)           1024           45.33        1.09         
blocking       standard (int)           8192           43.60        6.55         
nonblocking    standard (int)           8192           45.62        2.18         
blocking       big-payload              1024           41.14        0.97         
blocking       producer-heavy (int)     1024           56.77        1.32         
blocking       consumer-heavy (int)     1024           49.88        1.20   
```

As a comparison, the same benchmark was executed for a simple SPSC queue implementation using mutexes and condition variables (available on `main` branch). Results:

```
mode           scenario                 cap            avg ms       stdev ms  
blocking       standard (int)           1024           171.75       8.10         
nonblocking    standard (int)           1024           187.84       5.31 
```




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
  lines......: 96.5% (55 of 57 lines)
  functions..: 100.0% (20 of 20 functions)
```
