cmake_minimum_required(VERSION 3.12)

project(SputterOS VERSION 0.5.0 LANGUAGES CXX)

set(LIB_NAME SputterOS)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Freestanding Build Mode ───────────────────────────────────────────────────
# Enable for bare-metal / embedded targets that lack a hosted C++ runtime.
# Adds -ffreestanding -fno-exceptions -fno-rtti and defines
# SPUTTEROS_FREESTANDING=1 so library headers can detect the mode.
option(SPUTTEROS_FREESTANDING "Build for a freestanding (bare-metal) environment" OFF)

if(SPUTTEROS_FREESTANDING)
    message(STATUS "SputterOS: Freestanding build enabled (-ffreestanding -fno-exceptions -fno-rtti)")
    add_compile_options(-ffreestanding -fno-exceptions -fno-rtti)
    add_compile_definitions(SPUTTEROS_FREESTANDING=1)
endif()

# ── Top-Level vs Subproject Detection ─────────────────────────────────────────
# When SputterOS is the top-level project: BUILD TESTS
# When SputterOS is included via add_subdirectory() in a parent project: SKIP TESTS
#
# This allows:
#   - Standalone: cmake -B build -S . && cmake --build build (runs tests)
#   - Subproject: parent CMakeLists.txt includes SputterOS (library only, no tests)
#
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(SPUTTEROS_IS_TOP_LEVEL TRUE)
    message(STATUS "SputterOS: Top-level project detected. Building tests.")
    option(ENABLE_TESTS "Build and enable test suites (top-level only)" ON)
else()
    set(SPUTTEROS_IS_TOP_LEVEL FALSE)
    message(STATUS "SputterOS: Subproject detected. Tests disabled (use parent project to enable).")
    option(ENABLE_TESTS "Build and enable test suites (top-level only)" OFF)
endif()

if(ENABLE_TESTS)
    enable_testing()
    message(STATUS "Test suites enabled (ENABLE_TESTS=ON)")
else()
    message(STATUS "Test suites disabled. Building library only.")
endif()

# Collect source files
file(GLOB_RECURSE SPUTTEROS_SOURCES "src/*.cpp")

# Create a library for SputterOS. Header-only with optional implementation stubs.
if(SPUTTEROS_SOURCES)
    add_library(${LIB_NAME} ${SPUTTEROS_SOURCES})
    target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)
else()
    add_library(${LIB_NAME} INTERFACE)
endif()

# Add include directories for the library
target_include_directories(${LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Export the library for use as a submodule in other projects
set_target_properties(${LIB_NAME} PROPERTIES EXPORT_NAME SputterOS)

# ── Test Suites (optional) ────────────────────────────────────────────────────
# Tests are built via the unified tests/CMakeLists.txt which handles GoogleTest,
# coverage flags, and both unit + system test suites in a single build tree.
#
# Standalone (recommended):
#   cmake -B tests/build -S tests -G Ninja && ninja -C tests/build
#   ctest --test-dir tests/build --output-on-failure
#
# Note: tests/buildTest/ has been removed — bare-metal compilation
# verification will be replaced by a future mechanism.
#
# When SputterOS is the top-level project, the test suite is available via
# add_subdirectory(tests) below. When SputterOS is a subproject, tests are
# skipped automatically.
if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()

# Documentation on how to use this library as a submodule:
# In the parent project:
# add_subdirectory(lib/SputterOS)
# target_link_libraries(MyProject PRIVATE SputterOS)
