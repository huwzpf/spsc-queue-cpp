cmake_minimum_required(VERSION 3.20)
project(spsc_queue_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(bench main.cpp)

target_compile_options(bench PRIVATE
    -O3
)

find_package(Threads REQUIRED)
target_link_libraries(bench PRIVATE Threads::Threads)
