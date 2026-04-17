# CrunchLoop example project — OS-native dual-core CRUNCH build
#
# Produces one executable target:
#   crunchloop_osNative — runs a dual-core kernel demonstrating
#                         ICrunchTask on Core 1 in CRUNCH dispatch
#                         mode with AtomicDoubleBuffer cross-core
#                         data sharing.
#
# Demonstrates:
#   - ICrunchTask exclusive-core tight-loop scheduling (~6 kHz)
#   - CoreDispatchMode::CRUNCH on Core 1
#   - AtomicDoubleBuffer for wait-free cross-core data sharing
#   - CrunchDispatcher overrun detection and safety abort bridge
#   - Dual-core AMP scheduling (FLAT_LOOP on Core 0, CRUNCH on Core 1)
#
# Registered as a CTest test labeled "example_projects".  Exit code 0 = PASSED.
#
# @author Ryan Massie (rmassie)
# @date 4/14/2026

add_executable(crunchloop_osNative
    src/main_osNative.cpp
)

target_compile_features(crunchloop_osNative PRIVATE cxx_std_17)

target_include_directories(crunchloop_osNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config      # CrunchLoopConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl    # CrunchLoopHAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/osal_impl   # CrunchLoopOSAL.h, TimedMutexAdapter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include     # CrunchLoopApp.h, SimulatedServoTask.h
    ${CMAKE_CURRENT_SOURCE_DIR}/..           # common/hal/, common/osal/
)

target_link_libraries(crunchloop_osNative PRIVATE
    SputterOS        # SputterOS header-only + compiled utils
    Threads::Threads # std::thread, std::this_thread::sleep_for
)

if(MSVC)
    target_compile_options(crunchloop_osNative PRIVATE /W4)
else()
    target_compile_options(crunchloop_osNative PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# ── CTest registration ────────────────────────────────────────────────────────
add_test(
    NAME    crunchloop_osNative
    COMMAND crunchloop_osNative
)
set_tests_properties(crunchloop_osNative PROPERTIES
    LABEL   "example_projects"
    TIMEOUT 30
)
