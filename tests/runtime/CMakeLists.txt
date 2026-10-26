# Asset-free tests. They compile engine sources directly and never open the
# game data files or SDL.

add_executable(fallout-runtime-tests
    "test_main.cc"
    "test_harness.h"
    "stubs.cc"
    "test_campaign_advance.cc"
    "test_pause_set.cc"
    "test_pointer_registry.cc"
    "test_rect.cc"
    "test_procedure_table.cc"
    "test_sim_clock.cc"
    "test_timed_events.cc"
    "test_world_sim.cc"
    "${CMAKE_SOURCE_DIR}/src/plib/gnw/rect.cc"
    "${CMAKE_SOURCE_DIR}/src/pointer_registry.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/campaign_advance.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/pause_set.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/procedure_table.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/sim_clock.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/timed_events.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/world_sim.cc"
)

target_include_directories(fallout-runtime-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(fallout-runtime-tests fallout-sanitizers)

add_test(NAME runtime COMMAND fallout-runtime-tests)
