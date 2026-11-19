# Asset-free tests. They compile engine sources directly and never open the
# game data files or SDL.

add_executable(fallout-runtime-tests
    "test_main.cc"
    "test_harness.h"
    "stubs.cc"
    "test_buffered_attack.cc"
    "test_attack_ammo.cc"
    "test_attack_participants.cc"
    "test_attack_instance.cc"
    "test_animation_lease.cc"
    "test_animation_callbacks.cc"
    "test_attack_callbacks.cc"
    "test_object_drop.cc"
    "test_object_teardown.cc"
    "test_script_binding.cc"
    "test_movement_owner.cc"
    "test_continuous_move.cc"
    "test_route_follower.cc"
    "test_vats_shot.cc"
    "test_campaign_advance.cc"
    "test_game_commands.cc"
    "test_modal_pause.cc"
    "test_object_handle.cc"
    "test_pause_set.cc"
    "test_pointer_registry.cc"
    "test_rect.cc"
    "test_procedure_table.cc"
    "test_program_dispatch.cc"
    "test_sim_clock.cc"
    "test_sim_log.cc"
    "test_tick_accumulator.cc"
    "test_timed_events.cc"
    "test_world_sim.cc"
    "${CMAKE_SOURCE_DIR}/src/plib/gnw/game_commands.cc"
    "${CMAKE_SOURCE_DIR}/src/plib/gnw/rect.cc"
    "${CMAKE_SOURCE_DIR}/src/object_handle.cc"
    "${CMAKE_SOURCE_DIR}/src/pointer_registry.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/buffered_attack.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/attack_ammo.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/attack_participants.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/attack_instance.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/animation_lease.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/animation_callbacks.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/attack_callbacks.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/object_drop.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/object_teardown.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/script_binding.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/movement_owner.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/continuous_move.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/route_follower.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/vats_shot.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/campaign_advance.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/pause_set.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/procedure_table.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/sim_clock.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/sim_log.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/timed_events.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/world_sim.cc"
)

target_include_directories(fallout-runtime-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(TARGET SDL2-static)
    target_include_directories(fallout-runtime-tests PRIVATE
        $<TARGET_PROPERTY:SDL2-static,INTERFACE_INCLUDE_DIRECTORIES>)
else()
    target_include_directories(fallout-runtime-tests PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

target_link_libraries(fallout-runtime-tests fallout-sanitizers)

add_test(NAME runtime COMMAND fallout-runtime-tests)

# The real interpreter (src/int/intrpret.cc) on programs built in memory. Its
# own target so that the stubs it needs stay out of the other tests. See
# interpreter_stubs.cc for what they replace. The SDL include path is for the
# engine headers only; nothing links against SDL.
add_executable(fallout-interpreter-tests
    "test_main.cc"
    "test_harness.h"
    "interpreter_stubs.cc"
    "interpreter_stubs.h"
    "test_interpreter_wait.cc"
    "${CMAKE_SOURCE_DIR}/src/int/intrpret.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/procedure_table.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/script_binding.cc"
)

target_include_directories(fallout-interpreter-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(TARGET SDL2-static)
    target_include_directories(fallout-interpreter-tests PRIVATE
        $<TARGET_PROPERTY:SDL2-static,INTERFACE_INCLUDE_DIRECTORIES>)
else()
    target_include_directories(fallout-interpreter-tests PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

target_link_libraries(fallout-interpreter-tests fallout-sanitizers)

add_test(NAME interpreter COMMAND fallout-interpreter-tests)

# A test that spins on a wait must not hang the build.
set_tests_properties(interpreter PROPERTIES TIMEOUT 30)

# The real event queue (src/game/queue.cc) with its handlers replaced. Own target
# for the same reason as the interpreter's. See queue_stubs.cc.
add_executable(fallout-queue-tests
    "test_main.cc"
    "test_harness.h"
    "queue_stubs.cc"
    "queue_stubs.h"
    "test_queue.cc"
    "test_calendar_pull.cc"
    "${CMAKE_SOURCE_DIR}/src/game/queue.cc"
    "${CMAKE_SOURCE_DIR}/src/game/calendar_queue.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/calendar_pull.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/campaign_advance.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/pause_set.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/sim_clock.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/sim_log.cc"
)

target_include_directories(fallout-queue-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(TARGET SDL2-static)
    target_include_directories(fallout-queue-tests PRIVATE
        $<TARGET_PROPERTY:SDL2-static,INTERFACE_INCLUDE_DIRECTORIES>)
else()
    target_include_directories(fallout-queue-tests PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

target_link_libraries(fallout-queue-tests fallout-sanitizers)

add_test(NAME queue COMMAND fallout-queue-tests)
set_tests_properties(queue PROPERTIES TIMEOUT 30)

# The public animation API with the real descriptor engine and ownership code.
# External game resources are stubs; no game data or SDL runtime is opened.
add_executable(fallout-animation-tests
    "test_main.cc"
    "test_harness.h"
    "test_anim.cc"
    "test_attack_flow.cc"
    "animation_stubs.cc"
    "animation_stubs.h"
    "${CMAKE_SOURCE_DIR}/src/game/anim.cc"
    "${CMAKE_SOURCE_DIR}/src/object_handle.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/animation_callbacks.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/animation_lease.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/route_follower.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/attack_ammo.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/attack_instance.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/attack_participants.cc"
    "${CMAKE_SOURCE_DIR}/src/sim/buffered_attack.cc"
    "${CMAKE_SOURCE_DIR}/src/plib/gnw/rect.cc"
)
target_include_directories(fallout-animation-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET SDL2-static)
    target_include_directories(fallout-animation-tests PRIVATE
        $<TARGET_PROPERTY:SDL2-static,INTERFACE_INCLUDE_DIRECTORIES>)
else()
    target_include_directories(fallout-animation-tests PRIVATE ${SDL2_INCLUDE_DIRS})
endif()
target_link_libraries(fallout-animation-tests fallout-sanitizers)
add_test(NAME animation COMMAND fallout-animation-tests)
set_tests_properties(animation PROPERTIES TIMEOUT 30)
