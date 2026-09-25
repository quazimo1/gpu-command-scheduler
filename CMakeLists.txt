cmake_minimum_required(VERSION 3.20)
project(gpu_command_scheduler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(scheduler src/scheduler.cpp)
target_include_directories(scheduler PUBLIC include)

if(MSVC)
  target_compile_options(scheduler PRIVATE /W4 /permissive-)
else()
  target_compile_options(scheduler PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gpu-scheduler src/main.cpp)
target_link_libraries(gpu-scheduler PRIVATE scheduler)

add_executable(policy-benchmark benchmarks/policy_benchmark.cpp)
target_link_libraries(policy-benchmark PRIVATE scheduler)

enable_testing()
add_executable(scheduler-tests tests/scheduler_tests.cpp)
target_link_libraries(scheduler-tests PRIVATE scheduler)
add_test(NAME scheduler-tests COMMAND scheduler-tests)
