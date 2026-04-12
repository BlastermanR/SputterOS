# MultiRate example project — OS-native build
#
# Produces one executable target:
#   multirate_osNative — runs a single-core kernel with two scheduled tasks
#                        at different frequencies (100 Hz and 2 Hz) plus a
#                        background task that fills idle gap time.
#
# Demonstrates:
#   - Cruncher multi-rate dispatch with RMS priority ordering
#   - SystemScheduler background gap utilisation
#   - Data flow from fast sampler to slow reporter
#
# Registered as a CTest test labeled "example_projects".  Exit code 0 = PASSED.
#
# @author Ryan Massie (rmassie)
# @date 4/11/2026

add_executable(multirate_osNative
    src/main_osNative.cpp
)

target_compile_features(multirate_osNative PRIVATE cxx_std_17)

# Include paths — searched in declaration order so project headers take
# priority over any identically named SputterOS internal headers.
target_include_directories(multirate_osNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config      # MultirateConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl    # MultirateHAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/osal_impl   # MultirateOSAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include     # MultirateStateMachine.h, tasks
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/hal  # TcpStreamServer.h
)

target_link_libraries(multirate_osNative PRIVATE
    SputterOS        # SputterOS header-only + compiled utils
    Threads::Threads # std::this_thread::sleep_for
)

if(WIN32)
    target_link_libraries(multirate_osNative PRIVATE ws2_32)
endif()

# Strict warnings — match the quality bar used across other test targets.
if(MSVC)
    target_compile_options(multirate_osNative PRIVATE /W4)
else()
    target_compile_options(multirate_osNative PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# ── CTest registration ────────────────────────────────────────────────────────
add_test(
    NAME    multirate_osNative
    COMMAND multirate_osNative
)
set_tests_properties(multirate_osNative PROPERTIES
    LABEL   "example_projects"
    TIMEOUT 30
)
