#include "atomic_wait_spsc_queue.hpp"
#include <iostream>
#include <thread>
#include <cassert>

int main() {
    spsc_queue<int> q(100);
    const int num_items = 1000000;

    std::jthread producer([&] {
        for (int i = 0; i < num_items; ++i) {
            q.push(i);
        }
    });

    std::jthread consumer([&] {
        for (int i = 0; i < num_items; ++i) {
            auto value = q.pop();
            assert(*value == i);
        }
    });

    return 0;
}