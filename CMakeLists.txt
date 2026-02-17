cmake_minimum_required(VERSION 3.20)
project(spsc_queue_cpp LANGUAGES CXX)

# Linux-only project
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(test main.cpp)

# Match your manual build: g++ -std=c++20 -O3 -pthread main.cpp -o test
target_compile_options(test PRIVATE
    -O3
)

# pthreads
find_package(Threads REQUIRED)
target_link_libraries(test PRIVATE Threads::Threads)