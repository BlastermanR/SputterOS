# Example Projects for SputterOS
#
# Standalone executable examples that exercise the full SputterOS runtime.
# Each example constructs a complete kernel from real library components
# and a thin platform layer (osal_impl/, hal_impl/).
#
# Usage (standalone):
#   cmake -B exampleProjects/build -S exampleProjects -G Ninja
#   ninja -C exampleProjects/build
#   ctest --test-dir exampleProjects/build --output-on-failure
#
# @author Ryan Massie (rmassie)
# @date 4/9/2026

cmake_minimum_required(VERSION 3.14)

project(SputterOS_ExampleProjects LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# pthreads / Win32 threads — required for std::this_thread::sleep_for.
find_package(Threads REQUIRED)

# ── SputterOS library ─────────────────────────────────────────────────────────
add_subdirectory(.. ${CMAKE_CURRENT_BINARY_DIR}/SputterOS_build)

enable_testing()

# ── Example projects ──────────────────────────────────────────────────────────
add_subdirectory(heartbeat)
add_subdirectory(pingpong)
add_subdirectory(multirate)
add_subdirectory(sensorpoll)
add_subdirectory(lifecycle)
add_subdirectory(crunchloop)
