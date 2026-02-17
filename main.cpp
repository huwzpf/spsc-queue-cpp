#include "simple_spsc_queue.hpp"
#include "atomic_spin_spsc_queue.hpp"
#include "atomic_wait_spsc_queue.hpp"

#include <cassert>
#include <iostream>
#include <thread>
#include <type_traits>

template <class Queue>
void run_test(const char* name)
{
    constexpr int num_items = 1'000'000;
    Queue q(100);

    std::jthread producer([&] {
        for (int i = 0; i < num_items; ++i) {
            const bool ok = q.push(i);
            // For these tests we expect no close(), so push must succeed.
            assert(ok);
        }
        q.close(); // allow consumer to exit if it uses close semantics
    });

    std::jthread consumer([&] {
        for (int i = 0; i < num_items; ++i) {
            auto value = q.pop();
            assert(value.has_value());
            assert(*value == i);
        }
    });

    std::cout << "[OK] " << name << "\n";
}

int main()
{
    run_test<simple_spsc_queue<int>>("simple_spsc_queue");
    run_test<atomic_spin_spsc_queue<int>>("atomic_spin_spsc_queue");
    run_test<atomic_wait_spsc_queue<int>>("atomic_wait_spsc_queue");
    return 0;
}