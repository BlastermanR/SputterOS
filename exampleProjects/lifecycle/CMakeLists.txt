# Lifecycle example project — OS-native dual-core build
#
# Produces one executable target:
#   lifecycle_osNative — runs a dual-core kernel demonstrating kernel
#                        state observation, AMP scheduling with per-core
#                        Cruncher instances, and multi-core barriers.
#
# Demonstrates:
#   - Kernel state machine observation via System<Cfg>::kernelState()
#   - Dual-core AMP scheduling (independent Crunchers per core)
#   - Multi-core startup/shutdown barriers (MultiCoreSync<2>)
#   - Task lifecycle hooks (onSuspend/onResume)
#   - Multi-rate tasks across cores (5 Hz on Core 0, 2 Hz on Core 1)
#
# Registered as a CTest test labeled "example_projects".  Exit code 0 = PASSED.
#
# @author Ryan Massie (rmassie)
# @date 4/11/2026

add_executable(lifecycle_osNative
    src/main_osNative.cpp
)

target_compile_features(lifecycle_osNative PRIVATE cxx_std_17)

target_include_directories(lifecycle_osNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config      # LifecycleConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl    # LifecycleHAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/osal_impl   # LifecycleOSAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include     # LifecycleStateMachine.h, tasks
    ${CMAKE_CURRENT_SOURCE_DIR}/..           # common/hal/, common/osal/
)

target_link_libraries(lifecycle_osNative PRIVATE
    SputterOS        # SputterOS header-only + compiled utils
    Threads::Threads # std::thread, std::this_thread::sleep_for
)

if(WIN32)
    target_link_libraries(lifecycle_osNative PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(lifecycle_osNative PRIVATE /W4)
else()
    target_compile_options(lifecycle_osNative PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# ── CTest registration ────────────────────────────────────────────────────────
add_test(
    NAME    lifecycle_osNative
    COMMAND lifecycle_osNative
)
set_tests_properties(lifecycle_osNative PROPERTIES
    LABEL   "example_projects"
    TIMEOUT 30
)
