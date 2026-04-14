# Unified Test Suite for SputterOS
#
# Single entry point for all host-native tests (unit + system).
# GoogleTest is fetched once and shared across both suites.
# SputterOS library is built once and linked into all test executables.
#
# Usage:
#   cmake -B tests/build -S tests -G Ninja
#   ninja -C tests/build
#   ctest --test-dir tests/build --output-on-failure
#
# Filter by suite:
#   ctest --test-dir tests/build --label-regex system        # system tests only
#   ctest --test-dir tests/build --label-exclude "system|memory"  # unit tests only
#   ctest --test-dir tests/build --label-regex logic         # logic layer only
#
# Coverage:
#   cmake -B tests/build_coverage -S tests -G Ninja \
#         -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
#         -DSPUTTEROS_COVERAGE=ON
#   ninja -C tests/build_coverage
#   ctest --test-dir tests/build_coverage --output-on-failure
#
# @author Ryan Massie (rmassie)
# @date 4/10/2026

cmake_minimum_required(VERSION 3.14)

project(SputterOS_Tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# ── Code Coverage (LLVM source-based, optional) ──────────────────────────────
option(SPUTTEROS_COVERAGE "Build with LLVM source-based coverage instrumentation" OFF)

if(SPUTTEROS_COVERAGE)
    message(STATUS "LLVM source-based coverage enabled (global flags).")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping -fuse-ld=lld)
endif()

# ── Google Test via FetchContent ──────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
enable_testing()

# ── SputterOS library (built once, shared by all test suites) ─────────────────
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/SputterOS_lib)

# Inject the test SystemConfig.h into the SputterOS compilation unit.
# Both unit and system test configs are identical; use unit's (has Doxygen comments).
target_include_directories(SputterOS PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit/config)

# ── Shared paths (propagated to all add_subdirectory() children) ──────────────
set(SPUTTEROS_TEST_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/unit/config)
set(SPUTTEROS_TEST_MOCKS_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/unit/mocks)

# ── Test suites ───────────────────────────────────────────────────────────────
add_subdirectory(unit)
add_subdirectory(systemTests)
