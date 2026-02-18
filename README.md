# spsc-queue-cpp

C++20 project implementing and comparing three bounded single-producer/single-consumer (SPSC) queues:

- `simple_spsc_queue<T>`: mutex + condition variable implementation.
- `atomic_spin_spsc_queue<T>`: Ring buffer using atomics with spin/yield waits.
- `atomic_wait_spsc_queue<T>`: Ring buffer using atomics with `atomic::wait/notify`.

The project includes:
- A benchmark executable (`bench`) for comparing queue behavior across scenarios.
- A GoogleTest suite (`queue_tests`) that runs the same behavioral tests for all queue variants and payload types (`int`, `std::vector<int>`).

## API Summary
Each queue exposes:

- `bool try_push(U&& item)`
- `bool push(U&& item)` (blocking)
- `std::optional<T> try_pop()`
- `std::optional<T> pop()` (blocking)
- `void close()`
- `std::size_t capacity() const`

Close semantics:
- After `close()`, pushes fail.
- Pops continue draining already queued items.
- Once drained, pops return `std::nullopt`.

## Assumptions
- Exactly one producer thread and one consumer thread access a queue instance.
- Capacity is fixed at construction and must be greater than zero.
- Queue object lifetime must exceed the lifetime of producer/consumer threads that access it.
- Destructors call `close()` as best-effort wakeup, but caller is still responsible for orderly thread shutdown.
- Both atomic queues currently use `std::vector<T>` as the ring storage by design. This was chosen to simplify implementation,
  as move-assignment into pre-constructed slots is requires less manual lifetime management code.
  An alternative would be raw uninitialized storage (`std::byte[]` / `std::aligned_storage` + placement new/destruct).
  Tradeoff: Raw storage can remove the `default_initializable<T>` requirement and avoid eager default construction of all slots, 
  but it adds substantial complexity, so it was not implemented.

## Build
Configure and build (Release):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
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

## Benchmark
```bash
./build/bench
```

Benchmark scenarios:
- blocking standard (`cap`: 64, 1024, 8192)
- nonblocking standard (`cap`: 64, 1024, 8192)
- blocking big-payload (`cap`: 1024)
- blocking producer-heavy (`cap`: 1024)
- blocking consumer-heavy (`cap`: 1024)

Reported metrics:
- `avg ms`
- `stdev ms`

### Example benchmark results
```
queue   mode           scenario       cap            avg ms         stdev ms
simple  blocking       standard       64             413.52       57.00        
simple  nonblocking    standard       64             201.53       3.86         
simple  blocking       standard       1024           162.37       4.58         
simple  nonblocking    standard       1024           180.93       8.55         
simple  blocking       standard       8192           158.08       7.32         
simple  nonblocking    standard       8192           170.60       4.86         
simple  blocking       big-payload    1024           243.56       14.25        
simple  blocking       producer-heavy 1024           191.79       3.31         
simple  blocking       consumer-heavy 1024           214.15       5.49         
spin    blocking       standard       64             45.26        1.62         
spin    nonblocking    standard       64             46.49        1.73         
spin    blocking       standard       1024           45.92        1.74         
spin    nonblocking    standard       1024           46.01        1.77         
spin    blocking       standard       8192           45.63        1.52         
spin    nonblocking    standard       8192           47.34        4.50         
spin    blocking       big-payload    1024           46.59        3.28         
spin    blocking       producer-heavy 1024           61.53        5.73         
spin    blocking       consumer-heavy 1024           54.33        4.83         
wait    blocking       standard       64             523.25       31.38        
wait    nonblocking    standard       64             53.75        4.49         
wait    blocking       standard       1024           443.58       37.08        
wait    nonblocking    standard       1024           53.04        8.15         
wait    blocking       standard       8192           416.41       40.85        
wait    nonblocking    standard       8192           47.73        1.21         
wait    blocking       big-payload    1024           466.46       8.64         
wait    blocking       producer-heavy 1024           531.50       11.68        
wait    blocking       consumer-heavy 1024           331.79       16.20 
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
