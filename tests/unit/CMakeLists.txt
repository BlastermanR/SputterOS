# Unit Test Suite for SputterOS
#
# Host-native build — no embedded toolchain, no Pico SDK.
# Each class under test has its own CTest executable for isolation.
#
# This file is included via add_subdirectory() from the parent
# tests/CMakeLists.txt which provides: GoogleTest, SputterOS library,
# SPUTTEROS_TEST_CONFIG_DIR, SPUTTEROS_TEST_MOCKS_DIR, and coverage flags.
#
# @author Ryan Massie (rmassie)
# @date 4/5/2026

# ── Test suites ───────────────────────────────────────────────────────────────
add_subdirectory(logic)
add_subdirectory(tasks)
add_subdirectory(utils)
add_subdirectory(osal)
add_subdirectory(comms)
