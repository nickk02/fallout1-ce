# Asset-free tests. They compile engine sources directly and never open the
# game data files or SDL.

add_executable(fallout-runtime-tests
    "test_main.cc"
    "test_harness.h"
    "stubs.cc"
    "test_pointer_registry.cc"
    "test_rect.cc"
    "${CMAKE_SOURCE_DIR}/src/plib/gnw/rect.cc"
    "${CMAKE_SOURCE_DIR}/src/pointer_registry.cc"
)

target_include_directories(fallout-runtime-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(fallout-runtime-tests fallout-sanitizers)

add_test(NAME runtime COMMAND fallout-runtime-tests)
