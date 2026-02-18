#include "atomic_spin_spsc_queue.hpp"
#include "atomic_wait_spsc_queue.hpp"
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
        spin,
        wait
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

    struct BenchRun
    {
        double elapsed_ms = 0.0;
        double throughput_ops_per_sec = 0.0;
        bool order_ok = true;
        bool count_ok = true;
    };

    struct Aggregate
    {
        QueueKind queue = QueueKind::simple;
        BenchCase bench_case{};
        double avg_elapsed_ms = 0.0;
        double stdev_elapsed_ms = 0.0;
        double avg_throughput = 0.0;
        double stdev_throughput = 0.0;
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
        case QueueKind::spin:
            return "spin";
        case QueueKind::wait:
            return "wait";
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
    BenchRun run_benchmark(std::size_t capacity, Mode mode, std::size_t producer_cycles, std::size_t consumer_cycles, std::size_t items)
    {
        Queue q(capacity);
        bool order_ok = true;
        bool producer_ok = true;
        bool consumer_ok = true;
        std::size_t consumed = 0;

        const auto start = std::chrono::steady_clock::now();

        std::jthread producer([&]
                              {
        for (std::size_t i = 0; i < items; ++i)
        {
            busy_cycles(producer_cycles);
            if (mode == Mode::blocking)
            {
                if (!q.push(make_payload<Payload>(i)))
                {
                    producer_ok = false;
                    q.close();
                    break;
                }
            }
            else
            {
                while (!q.try_push(make_payload<Payload>(i)))
                {
                }
            }
        } });

        std::jthread consumer([&]
                              {
        std::uint64_t expected = 0;
        while (consumed < items)
        {
            auto value = (mode == Mode::blocking) ? q.pop() : q.try_pop();
            if (!value.has_value())
            {
                if (mode == Mode::blocking)
                {
                    consumer_ok = false;
                    break;
                }
                continue;
            }

            if (payload_seq<Payload>(*value) != expected)
            {
                order_ok = false;
            }
            busy_cycles(consumer_cycles);
            ++expected;
            ++consumed;
        } });

        producer.join();
        consumer.join();
        const auto end = std::chrono::steady_clock::now();

        BenchRun run;
        run.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        run.throughput_ops_per_sec = static_cast<double>(items) / (run.elapsed_ms / 1000.0);
        run.order_ok = order_ok;
        run.count_ok = (mode == Mode::blocking)
                           ? (producer_ok && consumer_ok && consumed == items)
                           : (consumed == items);
        return run;
    }

    // 1) blocking_standard
    template <typename Queue>
    BenchRun benchmark_blocking_standard(std::size_t capacity, std::size_t items)
    {
        return run_benchmark<Queue, int>(capacity, Mode::blocking, 0, 0, items);
    }

    // 2) nonblocking_standard
    template <typename Queue>
    BenchRun benchmark_nonblocking_standard(std::size_t capacity, std::size_t items)
    {
        return run_benchmark<Queue, int>(capacity, Mode::nonblocking, 0, 0, items);
    }

    // 3) big_payload
    template <typename Queue>
    BenchRun benchmark_big_payload(std::size_t capacity, std::size_t items)
    {
        return run_benchmark<Queue, BigPayload>(capacity, Mode::blocking, 0, 0, items);
    }

    // 4) producer_heavy
    template <typename Queue>
    BenchRun benchmark_producer_heavy(std::size_t capacity, std::size_t items)
    {
        return run_benchmark<Queue, int>(capacity, Mode::blocking, 0, heavy_cycles, items);
    }

    // 5) consumer_heavy
    template <typename Queue>
    BenchRun benchmark_consumer_heavy(std::size_t capacity, std::size_t items)
    {
        return run_benchmark<Queue, int>(capacity, Mode::blocking, heavy_cycles, 0, items);
    }

    template <template <class> class QueueTemplate>
    BenchRun run_case(const BenchCase &bc, std::size_t items)
    {
        switch (bc.scenario)
        {
        case Scenario::blocking_standard:
            return benchmark_blocking_standard<QueueTemplate<int>>(bc.capacity, items);
        case Scenario::nonblocking_standard:
            return benchmark_nonblocking_standard<QueueTemplate<int>>(bc.capacity, items);
        case Scenario::big_payload:
            return benchmark_big_payload<QueueTemplate<BigPayload>>(bc.capacity, items);
        case Scenario::producer_heavy:
            return benchmark_producer_heavy<QueueTemplate<int>>(bc.capacity, items);
        case Scenario::consumer_heavy:
            return benchmark_consumer_heavy<QueueTemplate<int>>(bc.capacity, items);
        }
        std::abort();
    }

    template <template <class> class QueueTemplate>
    void run_for_queue(QueueKind queue, const std::vector<BenchCase> &cases, std::vector<Aggregate> &aggregates)
    {
        for (const BenchCase &bc : cases)
        {
            std::cout << std::format("[{}] {} {} cap={}\n",
                                     to_string(queue),
                                     to_string(mode_for(bc.scenario)),
                                     to_string(bc.scenario),
                                     bc.capacity);

            std::vector<BenchRun> runs;
            runs.reserve(repeat_count);
            for (std::size_t i = 0; i < repeat_count; ++i)
            {
                BenchRun run = run_case<QueueTemplate>(bc, item_count);
                assert(run.order_ok);
                assert(run.count_ok);
                runs.push_back(run);
            }

            Aggregate out;
            out.queue = queue;
            out.bench_case = bc;

            std::vector<double> elapsed;
            std::vector<double> throughputs;
            elapsed.reserve(runs.size());
            throughputs.reserve(runs.size());

            for (const BenchRun &r : runs)
            {
                out.avg_elapsed_ms += r.elapsed_ms;
                out.avg_throughput += r.throughput_ops_per_sec;
                elapsed.push_back(r.elapsed_ms);
                throughputs.push_back(r.throughput_ops_per_sec);
            }

            out.avg_elapsed_ms /= static_cast<double>(runs.size());
            out.avg_throughput /= static_cast<double>(runs.size());

            double elapsed_var = 0.0;
            for (double v : elapsed)
            {
                const double d = v - out.avg_elapsed_ms;
                elapsed_var += d * d;
            }
            out.stdev_elapsed_ms = std::sqrt(elapsed_var / static_cast<double>(elapsed.size()));

            double throughput_var = 0.0;
            for (double v : throughputs)
            {
                const double d = v - out.avg_throughput;
                throughput_var += d * d;
            }
            out.stdev_throughput = std::sqrt(throughput_var / static_cast<double>(throughputs.size()));

            aggregates.push_back(out);
        }
    }

    void print_table(const std::vector<Aggregate> &rows)
    {
        std::cout << std::format("{:<8}{:<12}{:<15}{:<10}{:<12}{:<12}{:<16}{:<16}\n",
                                 "queue", "mode", "scenario", "cap",
                                 "avg ms", "stdev ms", "avg ops/s", "stdev ops/s");

        for (const Aggregate &r : rows)
        {
            std::cout << std::format("{:<8}{:<12}{:<15}{:<10}{:<12.2f}{:<12.2f}{:<16.2f}{:<16.2f}\n",
                                     to_string(r.queue),
                                     to_string(mode_for(r.bench_case.scenario)),
                                     to_string(r.bench_case.scenario),
                                     r.bench_case.capacity,
                                     r.avg_elapsed_ms,
                                     r.stdev_elapsed_ms,
                                     r.avg_throughput,
                                     r.stdev_throughput);
        }
    }

} // namespace

int main(int argc, char **argv)
{
    if (argc != 1)
    {
        std::cerr << "Usage: " << argv[0] << "\n";
        return 1;
    }

    const std::vector<BenchCase> cases = make_cases();
    std::string capacities;
    for (std::size_t cap : standard_capacities)
    {
        if (!capacities.empty())
            capacities += " ";
        capacities += std::to_string(cap);
    }
    std::cout << std::format("Queues: simple spin wait | items={} repeats={} default-capacity={} standard-capacities={}\n",
                             item_count, repeat_count, default_capacity, capacities);

    std::vector<Aggregate> aggregates;
    run_for_queue<simple_spsc_queue>(QueueKind::simple, cases, aggregates);
    run_for_queue<atomic_spin_spsc_queue>(QueueKind::spin, cases, aggregates);
    run_for_queue<atomic_wait_spsc_queue>(QueueKind::wait, cases, aggregates);

    print_table(aggregates);
    return 0;
}
