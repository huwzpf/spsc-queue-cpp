cmake_minimum_required(VERSION 3.20)
project(spsc_queue_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_COVERAGE "Enable Linux GCC gcov coverage instrumentation for unit tests" OFF)

add_executable(bench src/main.cpp)

target_include_directories(
    bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(bench PRIVATE
    -O3
)

find_package(Threads REQUIRED)
target_link_libraries(bench PRIVATE Threads::Threads)


include(FetchContent)
FetchContent_Declare(
googletest
URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
queue_tests
tests/queue_tests.cpp
)

target_include_directories(
    queue_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(queue_tests PRIVATE
    Threads::Threads
    GTest::gtest_main
)

if(ENABLE_COVERAGE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "ENABLE_COVERAGE is supported only on Linux with GCC")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC (CMAKE_CXX_COMPILER_ID='${CMAKE_CXX_COMPILER_ID}')")
    endif()

    target_compile_options(queue_tests PRIVATE -O0 -g --coverage)
    target_link_options(queue_tests PRIVATE --coverage)
    message(STATUS "Coverage instrumentation enabled for queue_tests (Linux/GCC)")
endif()

include(GoogleTest)
gtest_discover_tests(queue_tests)
    
