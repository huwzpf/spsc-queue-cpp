#include "atomic_spsc_queue.hpp"
#include "simple_spsc_queue.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include <format>

namespace
{

    constexpr std::size_t item_count = 1000000;
    constexpr std::size_t repeat_count = 20;
    constexpr std::size_t default_capacity = 1024;
    constexpr std::array<std::size_t, 3> standard_capacities{64, 1024, 8192};
    constexpr std::size_t heavy_cycles = 128;

    enum class QueueKind
    {
        simple,
        atomic,
    };

    enum class Mode
    {
        blocking,
        nonblocking
    };

    enum class Scenario
    {
        blocking_standard,
        nonblocking_standard,
        big_payload,
        producer_heavy,
        consumer_heavy
    };

    struct BigPayload
    {
        std::uint64_t seq = 0;
        std::uint64_t a = 1;
        std::uint64_t b = 2;
        std::uint64_t c = 3;
        std::uint64_t d = 4;
        std::uint64_t e = 5;
        std::uint64_t f = 6;
        std::uint64_t g = 7;
    };

    struct BenchCase
    {
        Scenario scenario = Scenario::blocking_standard;
        std::size_t capacity = default_capacity;
    };

    struct Aggregate
    {
        QueueKind queue = QueueKind::simple;
        BenchCase bench_case{};
        double avg_elapsed_ms = 0.0;
        double stdev_elapsed_ms = 0.0;
    };

    void busy_cycles(std::size_t cycles)
    {
        for (std::size_t i = 0; i < cycles; ++i)
        {
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
    }

    const char *to_string(QueueKind v)
    {
        switch (v)
        {
        case QueueKind::simple:
            return "simple";
        case QueueKind::atomic:
            return "atomic";
        }
        return "unknown";
    }

    const char *to_string(Mode v)
    {
        return v == Mode::blocking ? "blocking" : "nonblocking";
    }

    const char *to_string(Scenario v)
    {
        switch (v)
        {
        case Scenario::blocking_standard:
            return "standard";
        case Scenario::nonblocking_standard:
            return "standard";
        case Scenario::big_payload:
            return "big-payload";
        case Scenario::producer_heavy:
            return "producer-heavy";
        case Scenario::consumer_heavy:
            return "consumer-heavy";
        }
        return "unknown";
    }

    Mode mode_for(Scenario s)
    {
        return s == Scenario::nonblocking_standard ? Mode::nonblocking : Mode::blocking;
    }

    std::vector<BenchCase> make_cases()
    {
        std::vector<BenchCase> out;

        for (std::size_t cap : standard_capacities)
        {
            out.push_back(BenchCase{Scenario::blocking_standard, cap});
            out.push_back(BenchCase{Scenario::nonblocking_standard, cap});
        }

        out.push_back(BenchCase{Scenario::big_payload, default_capacity});
        out.push_back(BenchCase{Scenario::producer_heavy, default_capacity});
        out.push_back(BenchCase{Scenario::consumer_heavy, default_capacity});

        return out;
    }

    template <typename Payload>
    Payload make_payload(std::size_t seq)
    {
        if constexpr (std::is_same_v<Payload, int>)
        {
            return static_cast<int>(seq);
        }
        else
        {
            Payload payload{};
            payload.seq = seq;
            return payload;
        }
    }

    template <typename Payload>
    std::uint64_t payload_seq(const Payload &v)
    {
        if constexpr (std::is_same_v<Payload, int>)
        {
            return static_cast<std::uint64_t>(v);
        }
        else
        {
            return v.seq;
        }
    }

    template <typename Queue, typename Payload>
    double run_benchmark(std::size_t capacity, Mode mode, std::size_t producer_cycles, std::size_t consumer_cycles, std::size_t items)
    {
        Queue q(capacity);
        std::size_t consumed = 0;

        const auto start = std::chrono::steady_clock::now();

        if (mode == Mode::blocking)
        {
            std::jthread producer ([&]{
                for (std::size_t i = 0; i < items; ++i)
                {
                    busy_cycles(producer_cycles);
                    const bool pushed = q.push(make_payload<Payload>(i));
                    assert(pushed);
                }
                q.close();
            });
            
            std::jthread consumer([&]{
                std::uint64_t expected = 0;
                while (true)
                {
                    auto value = q.pop();
                    if (!value.has_value())
                    {
                        break;
                    }

                    assert (payload_seq<Payload>(*value) == expected);
                    busy_cycles(consumer_cycles);
                    ++expected;
                    ++consumed;
                }
            });
        } else {
            std::jthread producer ([&]{
                for (std::size_t i = 0; i < items; ++i)
                {
                    busy_cycles(producer_cycles);
                    while (!q.try_push(make_payload<Payload>(i))) {}
                }
                q.close();
            });
            
            std::jthread consumer([&]{
                std::uint64_t expected = 0;
                while (true)
                {
                    auto value = q.try_pop();
                    if (!value.has_value())
                    {
                        if (q.done())
                        {
                            break;
                        }
                        continue;
                    }

                    assert (payload_seq<Payload>(*value) == expected);
                    busy_cycles(consumer_cycles);
                    ++expected;
                    ++consumed;
                }
            });
        }

        assert(consumed == items);

        const auto end = std::chrono::steady_clock::now();

        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    template <template <class> class QueueTemplate>
    double run_case(const BenchCase &bc, std::size_t items)
    {
        switch (bc.scenario)
        {
        case Scenario::blocking_standard:
            return run_benchmark<QueueTemplate<int>, int>(bc.capacity, Mode::blocking, 0, 0, items);
        case Scenario::nonblocking_standard:
            return run_benchmark<QueueTemplate<int>, int>(bc.capacity, Mode::nonblocking, 0, 0, items);
        case Scenario::big_payload:
            return run_benchmark<QueueTemplate<BigPayload>, BigPayload>(bc.capacity, Mode::blocking, 0, 0, items);
        case Scenario::producer_heavy:
            return run_benchmark<QueueTemplate<int>, int>(bc.capacity, Mode::blocking, heavy_cycles, 0, items);
        case Scenario::consumer_heavy:
            return run_benchmark<QueueTemplate<int>, int>(bc.capacity, Mode::blocking, 0, heavy_cycles, items);
        }
        std::abort();
    }

    template <template <class> class QueueTemplate>
    void run_for_queue(QueueKind queue, const std::vector<BenchCase> &cases, std::vector<Aggregate> &aggregates)
    {
        for (const BenchCase &bc : cases)
        {
            std::cout << std::format("[{}] Running {} {} cap={}\n",
                                     to_string(queue),
                                     to_string(mode_for(bc.scenario)),
                                     to_string(bc.scenario),
                                     bc.capacity);

            std::vector<double> runs;
            runs.reserve(repeat_count);
            for (std::size_t i = 0; i < repeat_count; ++i)
            {
                runs.push_back(run_case<QueueTemplate>(bc, item_count));
            }

            Aggregate out;
            out.queue = queue;
            out.bench_case = bc;

            std::vector<double> elapsed;
            elapsed.reserve(runs.size());

            for (const auto r : runs)
            {
                out.avg_elapsed_ms += r;
                elapsed.push_back(r);
            }

            out.avg_elapsed_ms /= static_cast<double>(runs.size());

            double elapsed_var = 0.0;
            for (double v : elapsed)
            {
                const double d = v - out.avg_elapsed_ms;
                elapsed_var += d * d;
            }
            out.stdev_elapsed_ms = std::sqrt(elapsed_var / static_cast<double>(elapsed.size()));

            aggregates.push_back(out);
        }
    }

    void print_table(const std::vector<Aggregate> &rows)
    {
        std::cout << std::format("{:<8}{:<15}{:<15}{:<15}{:<15}{:<15}\n",
                                 "queue", "mode", "scenario", "cap",
                                 "avg ms", "stdev ms");

        for (const Aggregate &r : rows)
        {
            std::cout << std::format("{:<8}{:<15}{:<15}{:<15}{:<13.2f}{:<13.2f}\n",
                                     to_string(r.queue),
                                     to_string(mode_for(r.bench_case.scenario)),
                                     to_string(r.bench_case.scenario),
                                     r.bench_case.capacity,
                                     r.avg_elapsed_ms,
                                     r.stdev_elapsed_ms);
        }
    }

} // namespace

int main(int argc, char **argv)
{
    std::cout << std::format("Starting benchmark for items={} repeats={}\n", item_count, repeat_count);

    const std::vector<BenchCase> cases = make_cases();
    std::vector<Aggregate> aggregates;
    run_for_queue<simple_spsc_queue>(QueueKind::simple, cases, aggregates);
    run_for_queue<atomic_spsc_queue>(QueueKind::atomic, cases, aggregates);

    print_table(aggregates);
    return 0;
}
