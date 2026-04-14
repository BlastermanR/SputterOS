cmake_minimum_required(VERSION 3.12)

project(SputterOS VERSION 0.5.0 LANGUAGES CXX)

# ── Initialize Git Submodules ────────────────────────────────────────────────
# Ensure all submodules are initialized and updated before building
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    message(STATUS "Initializing and updating Git submodules...")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_SUBMODULE_RESULT
    )
    if(NOT GIT_SUBMODULE_RESULT EQUAL 0)
        message(WARNING "Failed to update submodules (exit code: ${GIT_SUBMODULE_RESULT})")
    endif()
    
    # Verify that lib/units exists
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/units/CMakeLists.txt")
        message(FATAL_ERROR "Submodule lib/units not found. Please ensure the repository has been properly cloned with --recursive flag.")
    endif()
else()
    message(WARNING "Git not found or .git directory missing. Submodules will not be automatically initialized.")
endif()

set(LIB_NAME SputterOS)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# ── Units library (header-only, for physical quantity management) ──────────────
# Configuration for embedded environments
set(DISABLE_IOSTREAM ON CACHE BOOL "Disable iostream in units library for embedded" FORCE)
# Enable all predefined units for maximum flexibility
set(DISABLE_PREDEFINED_UNITS OFF CACHE BOOL "Enable all predefined units" FORCE)

add_subdirectory(lib/units)
target_link_libraries(${LIB_NAME} PUBLIC units)

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
