#include "test_harness.h"

#include <SDL_gamecontroller.h>
#include <SDL_scancode.h>

#include <math.h>

#include "plib/gnw/controller.h"
#include "plib/gnw/game_commands.h"

using namespace fallout;

static const CommandSource kKeyboard = CommandSource::Keyboard;
static const CommandSource kController = CommandSource::Controller;

TEST(edge_for_key_down_repeat_and_up)
{
    CHECK(command_edge_for_key(true, false) == CommandEdge::Pressed);
    CHECK(command_edge_for_key(true, true) == CommandEdge::None);
    CHECK(command_edge_for_key(false, false) == CommandEdge::Released);
    CHECK(command_edge_for_key(false, true) == CommandEdge::Released);
}

TEST(scancodes_map_to_the_five_commands)
{
    Command command;
    CHECK(command_for_scancode(SDL_SCANCODE_E, &command) && command == Command::Interact);
    CHECK(command_for_scancode(SDL_SCANCODE_F, &command) && command == Command::Attack);
    CHECK(command_for_scancode(SDL_SCANCODE_Q, &command) && command == Command::Inspect);
    CHECK(command_for_scancode(SDL_SCANCODE_LEFTBRACKET, &command) && command == Command::PreviousTarget);
    CHECK(command_for_scancode(SDL_SCANCODE_RIGHTBRACKET, &command) && command == Command::NextTarget);

    CHECK(!command_for_scancode(SDL_SCANCODE_K, &command));
    CHECK(!command_for_scancode(SDL_SCANCODE_W, &command));
    CHECK(!command_for_scancode(SDL_SCANCODE_S, &command));
    CHECK(!command_for_scancode(SDL_SCANCODE_RETURN, &command));
    CHECK(!command_for_scancode(SDL_SCANCODE_SPACE, &command));
    CHECK(!command_for_scancode(SDL_SCANCODE_V, &command));
}

TEST(buttons_map_only_a_and_y)
{
    Command command;
    CHECK(command_for_button(SDL_CONTROLLER_BUTTON_A, &command) && command == Command::Interact);
    CHECK(command_for_button(SDL_CONTROLLER_BUTTON_Y, &command) && command == Command::Inspect);
    CHECK(!command_for_button(SDL_CONTROLLER_BUTTON_B, &command));
    CHECK(!command_for_button(SDL_CONTROLLER_BUTTON_X, &command));
    CHECK(!command_for_button(SDL_CONTROLLER_BUTTON_START, &command));
    CHECK(!command_for_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, &command));
}

TEST(pressed_gives_one_pending_and_take_consumes)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(router.pending(Command::Interact));
    CHECK(router.held(Command::Interact));
    CHECK(!router.pending(Command::Attack));
    CHECK(router.take(Command::Interact));
    CHECK(!router.take(Command::Interact));
    CHECK(!router.pending(Command::Interact));
}

TEST(second_press_while_held_adds_no_pending)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(router.take(Command::Interact));
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(!router.take(Command::Interact));
}

TEST(release_before_take_still_delivers)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Attack, CommandEdge::Pressed);
    router.post(kKeyboard, Command::Attack, CommandEdge::Released);
    CHECK(!router.held(Command::Attack));
    CHECK(router.take(Command::Attack));
    CHECK(!router.take(Command::Attack));
}

TEST(none_edge_does_nothing)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Attack, CommandEdge::None);
    CHECK(!router.pending(Command::Attack));
    CHECK(!router.held(Command::Attack));
}

TEST(key_repeat_posts_nothing_and_does_not_release)
{
    CommandRouter router;
    CHECK(command_process_key(router, SDL_SCANCODE_F, true, false, true));
    CHECK(router.take(Command::Attack));
    CHECK(command_process_key(router, SDL_SCANCODE_F, true, true, true));
    CHECK(!router.pending(Command::Attack));
    CHECK(router.held(Command::Attack));
    CHECK(command_process_key(router, SDL_SCANCODE_F, true, false, true) == true);
    CHECK(!router.pending(Command::Attack));
}

TEST(key_up_clears_held_in_world_and_in_other)
{
    CommandRouter router;
    command_process_key(router, SDL_SCANCODE_E, true, false, true);
    CHECK(router.held(Command::Interact));
    command_process_key(router, SDL_SCANCODE_E, false, false, true);
    CHECK(!router.held(Command::Interact));

    command_process_key(router, SDL_SCANCODE_E, true, false, true);
    router.setContext(CommandContext::Other);
    command_process_key(router, SDL_SCANCODE_E, false, false, false);
    CHECK(!router.latched(kKeyboard, Command::Interact));
    CHECK(!router.held(Command::Interact));
}

TEST(key_swallow_results)
{
    CommandRouter router;
    const int swallowed[] = {
        SDL_SCANCODE_E,
        SDL_SCANCODE_F,
        SDL_SCANCODE_Q,
        SDL_SCANCODE_LEFTBRACKET,
        SDL_SCANCODE_RIGHTBRACKET,
        SDL_SCANCODE_K,
    };
    for (int scancode : swallowed) {
        CHECK(command_process_key(router, scancode, true, false, true));
        CHECK(command_process_key(router, scancode, false, false, true));
        CHECK(!command_process_key(router, scancode, true, false, false));
        CHECK(!command_process_key(router, scancode, false, false, false));
        CHECK(command_key_swallowed(scancode, true));
        CHECK(!command_key_swallowed(scancode, false));
    }

    CHECK(!command_process_key(router, SDL_SCANCODE_W, true, false, true));
    CHECK(!command_process_key(router, SDL_SCANCODE_RETURN, true, false, true));
    CHECK(!command_key_swallowed(SDL_SCANCODE_W, true));
}

TEST(k_posts_no_command)
{
    CommandRouter router;
    command_process_key(router, SDL_SCANCODE_K, true, false, true);
    for (int command = 0; command < static_cast<int>(Command::Count); command++) {
        CHECK(!router.pending(static_cast<Command>(command)));
    }
}

TEST(pressed_in_other_context_is_ignored)
{
    CommandRouter router;
    router.setContext(CommandContext::Other);
    CHECK(router.context() == CommandContext::Other);
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(!router.pending(Command::Interact));
    CHECK(!router.held(Command::Interact));
    CHECK(!command_process_key(router, SDL_SCANCODE_E, true, false, false));
    CHECK(!router.pending(Command::Interact));
}

TEST(context_change_clears_pending_and_held)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    router.post(kController, Command::Inspect, CommandEdge::Pressed);
    router.setContext(CommandContext::Other);
    CHECK(!router.pending(Command::Interact));
    CHECK(!router.held(Command::Interact));
    CHECK(!router.pending(Command::Inspect));
    router.setContext(CommandContext::World);
    CHECK(!router.take(Command::Interact));
    CHECK(!router.take(Command::Inspect));
}

TEST(same_context_is_a_no_op)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    router.setContext(CommandContext::World);
    CHECK(router.pending(Command::Interact));
    CHECK(router.held(Command::Interact));
    CHECK(!router.latched(kKeyboard, Command::Interact));
}

TEST(held_key_stays_latched_across_context_change)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    router.setContext(CommandContext::Other);
    router.setContext(CommandContext::World);
    CHECK(router.latched(kKeyboard, Command::Interact));
    CHECK(!router.held(Command::Interact));

    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(!router.pending(Command::Interact));

    router.post(kKeyboard, Command::Interact, CommandEdge::Released);
    CHECK(!router.latched(kKeyboard, Command::Interact));
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(router.take(Command::Interact));
}

TEST(release_posted_while_other_clears_latch)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Inspect, CommandEdge::Pressed);
    router.setContext(CommandContext::Other);
    CHECK(router.latched(kKeyboard, Command::Inspect));
    router.post(kKeyboard, Command::Inspect, CommandEdge::Released);
    CHECK(!router.latched(kKeyboard, Command::Inspect));
    router.setContext(CommandContext::World);
    router.post(kKeyboard, Command::Inspect, CommandEdge::Pressed);
    CHECK(router.take(Command::Inspect));
}

TEST(controller_trigger_held_across_context_change_does_not_refire)
{
    CommandRouter router;
    router.post(kController, Command::Attack, CommandEdge::Pressed);
    CHECK(router.take(Command::Attack));
    router.setContext(CommandContext::Other);
    router.setContext(CommandContext::World);

    router.post(kController, Command::Attack, CommandEdge::Pressed);
    CHECK(!router.pending(Command::Attack));
    CHECK(!router.held(Command::Attack));

    router.post(kController, Command::Attack, CommandEdge::Released);
    router.post(kController, Command::Attack, CommandEdge::Pressed);
    CHECK(router.take(Command::Attack));
}

TEST(source_loss_keeps_the_other_sources_pending)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Attack, CommandEdge::Pressed);
    router.post(kController, Command::Attack, CommandEdge::Pressed);
    router.sourceLost(kController);
    CHECK(router.take(Command::Attack));
}

TEST(keyboard_loss_clears_only_keyboard)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Attack, CommandEdge::Pressed);
    router.sourceLost(kKeyboard);
    CHECK(!router.pending(Command::Attack));
    CHECK(!router.held(Command::Attack));

    router.post(kController, Command::Attack, CommandEdge::Pressed);
    router.sourceLost(kKeyboard);
    CHECK(router.pending(Command::Attack));
    CHECK(router.held(Command::Attack));
}

TEST(source_loss_clears_that_sources_latch)
{
    CommandRouter router;
    router.post(kController, Command::Interact, CommandEdge::Pressed);
    router.setContext(CommandContext::Other);
    router.setContext(CommandContext::World);
    CHECK(router.latched(kController, Command::Interact));
    router.sourceLost(kController);
    CHECK(!router.latched(kController, Command::Interact));
    router.post(kController, Command::Interact, CommandEdge::Pressed);
    CHECK(router.take(Command::Interact));
}

TEST(source_loss_does_not_touch_the_other_sources_held_entry)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Inspect, CommandEdge::Pressed);
    router.post(kController, Command::Inspect, CommandEdge::Pressed);
    router.take(Command::Inspect);
    router.sourceLost(kController);
    CHECK(router.held(Command::Inspect));

    // The keyboard still holds it, so another keyboard press adds nothing.
    router.post(kKeyboard, Command::Inspect, CommandEdge::Pressed);
    CHECK(!router.pending(Command::Inspect));

    // A latch on the keyboard survives losing the controller.
    router.setContext(CommandContext::Other);
    router.setContext(CommandContext::World);
    router.post(kController, Command::Inspect, CommandEdge::Pressed);
    router.sourceLost(kController);
    CHECK(router.latched(kKeyboard, Command::Inspect));
}

TEST(both_sources_pressing_one_command_take_once)
{
    CommandRouter router;
    router.post(kKeyboard, Command::NextTarget, CommandEdge::Pressed);
    router.post(kController, Command::NextTarget, CommandEdge::Pressed);
    CHECK(router.take(Command::NextTarget));
    CHECK(!router.take(Command::NextTarget));
}

TEST(reset_clears_everything_and_returns_to_world)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    router.setContext(CommandContext::Other);
    router.reset();
    CHECK(router.context() == CommandContext::World);
    CHECK(!router.latched(kKeyboard, Command::Interact));
    CHECK(!router.pending(Command::Interact));
    router.post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(router.take(Command::Interact));
}

TEST(controller_action_values_equal_command_values)
{
    CHECK_EQ(static_cast<int>(Command::Interact), static_cast<int>(CONTROLLER_ACTION_INTERACT));
    CHECK_EQ(static_cast<int>(Command::Attack), static_cast<int>(CONTROLLER_ACTION_ATTACK));
    CHECK_EQ(static_cast<int>(Command::Inspect), static_cast<int>(CONTROLLER_ACTION_INSPECT));
    CHECK_EQ(static_cast<int>(Command::NextTarget), static_cast<int>(CONTROLLER_ACTION_NEXT_TARGET));
    CHECK_EQ(static_cast<int>(Command::PreviousTarget), static_cast<int>(CONTROLLER_ACTION_PREVIOUS_TARGET));
    CHECK_EQ(static_cast<int>(Command::Count), static_cast<int>(CONTROLLER_ACTION_COUNT));
}

TEST(process_wide_router_exists)
{
    game_commands().reset();
    game_commands().post(kKeyboard, Command::Interact, CommandEdge::Pressed);
    CHECK(game_commands().take(Command::Interact));
    game_commands().reset();
}

TEST(heartbeat_window_edges)
{
    WorldHeartbeat heartbeat;
    CHECK(!heartbeat.active(0));
    CHECK(!heartbeat.active(1000));
    heartbeat.tick(1000);
    CHECK(heartbeat.active(1000));
    CHECK(heartbeat.active(1149));
    CHECK(!heartbeat.active(1150));
    CHECK(!heartbeat.active(5000));
}

TEST(heartbeat_is_wrap_safe)
{
    WorldHeartbeat heartbeat;
    heartbeat.tick(0xFFFFFFF0u);
    CHECK(heartbeat.active(0xFFFFFFF0u));
    CHECK(heartbeat.active(0xFFFFFFFFu));
    CHECK(heartbeat.active(100u));
    CHECK(!heartbeat.active(0xFFFFFFF0u + 150u));
    CHECK(!heartbeat.active(1000u));
}

static int no_body()
{
    return 0;
}

TEST(default_pump_context_is_other)
{
    static_assert(kDefaultPumpContext == CommandContext::Other, "a pump is Other unless a world loop asks");
    CHECK(kDefaultPumpContext == CommandContext::Other);
}

TEST(pump_sets_the_context_for_the_body_and_returns_its_value)
{
    CommandRouter router;
    CommandContext seen = CommandContext::Other;

    int result = command_pump(router, CommandContext::World, [&] {
        seen = router.context();
        return 42;
    });
    CHECK_EQ(result, 42);
    CHECK(seen == CommandContext::World);

    result = command_pump(router, kDefaultPumpContext, [&] {
        seen = router.context();
        return 7;
    });
    CHECK_EQ(result, 7);
    CHECK(seen == CommandContext::Other);
}

TEST(nested_default_pump_clears_pending_and_stays_other)
{
    CommandRouter router;
    command_pump(router, CommandContext::World, [&] {
        command_process_key(router, SDL_SCANCODE_E, true, false, true);
        router.post(kController, Command::Attack, CommandEdge::Pressed);
        CHECK(router.pending(Command::Interact));
        CHECK(router.pending(Command::Attack));

        command_pump(router, kDefaultPumpContext, no_body);
        CHECK(!router.pending(Command::Interact));
        CHECK(!router.pending(Command::Attack));
        CHECK(!router.held(Command::Interact));
        CHECK(router.latched(kKeyboard, Command::Interact));
        CHECK(router.latched(kController, Command::Attack));

        // The context is not restored after the nested pump.
        CHECK(router.context() == CommandContext::Other);
        router.post(kKeyboard, Command::Inspect, CommandEdge::Pressed);
        CHECK(!router.pending(Command::Inspect));
        return 0;
    });
    CHECK(!router.take(Command::Interact));
    CHECK(!router.take(Command::Attack));
}

TEST(held_key_needs_a_release_after_a_nested_pump)
{
    WorldHeartbeat heartbeat;
    CommandRouter router;
    command_pump(router, CommandContext::World, [&] {
        command_process_key(router, SDL_SCANCODE_E, true, false, true);
        command_pump(router, kDefaultPumpContext, no_body);
        return 0;
    });

    // The key is still held when the next world loop starts.
    command_enter_world(router, heartbeat, 100);
    command_process_key(router, SDL_SCANCODE_E, true, false, true);
    CHECK(!router.take(Command::Interact));

    command_process_key(router, SDL_SCANCODE_E, false, false, true);
    command_process_key(router, SDL_SCANCODE_E, true, false, true);
    CHECK(router.take(Command::Interact));
}

TEST(key_up_during_the_nested_pump_clears_the_latch)
{
    WorldHeartbeat heartbeat;
    CommandRouter router;
    command_pump(router, CommandContext::World, [&] {
        command_process_key(router, SDL_SCANCODE_E, true, false, true);
        command_pump(router, kDefaultPumpContext, [&] {
            command_process_key(router, SDL_SCANCODE_E, false, false, false);
            return 0;
        });
        return 0;
    });
    CHECK(!router.latched(kKeyboard, Command::Interact));

    command_enter_world(router, heartbeat, 100);
    command_process_key(router, SDL_SCANCODE_E, true, false, true);
    CHECK(router.take(Command::Interact));
}

TEST(press_in_a_world_pump_survives_the_tick_that_follows)
{
    // The V pause loop pumps first and ticks second.
    WorldHeartbeat heartbeat;
    CommandRouter router;
    command_pump(router, CommandContext::World, [&] {
        command_process_key(router, SDL_SCANCODE_E, true, false, true);
        return 0;
    });
    command_enter_world(router, heartbeat, 100);
    CHECK(router.pending(Command::Interact));
    CHECK(router.take(Command::Interact));
}

TEST(released_in_any_context_and_pressed_only_in_world)
{
    CommandRouter router;
    router.post(kKeyboard, Command::Attack, CommandEdge::Pressed);
    command_pump(router, kDefaultPumpContext, no_body);
    CHECK(router.latched(kKeyboard, Command::Attack));
    router.post(kKeyboard, Command::Attack, CommandEdge::Released);
    CHECK(!router.latched(kKeyboard, Command::Attack));
    CHECK(!router.held(Command::Attack));

    router.post(kKeyboard, Command::Attack, CommandEdge::Pressed);
    CHECK(!router.pending(Command::Attack));
    CHECK(!router.held(Command::Attack));
}

TEST(trigger_poll_needs_command_enter_world_first)
{
    WorldHeartbeat heartbeat;
    CommandRouter router;
    AxisEvents axes;
    command_pump(router, kDefaultPumpContext, no_body);

    // Without the enter the router is still Other and ignores the press.
    axes.pollTrigger(router, 20000);
    CHECK(!router.pending(Command::Attack));
    axes.pollTrigger(router, 0);

    command_enter_world(router, heartbeat, 100);
    axes.pollTrigger(router, 20000);
    CHECK(router.take(Command::Attack));
}

TEST(attack_never_maps_to_confirm_pause_or_aimed_keys)
{
    Command command;
    const int notAttack[] = {
        SDL_SCANCODE_RETURN,
        SDL_SCANCODE_KP_ENTER,
        SDL_SCANCODE_SPACE,
        SDL_SCANCODE_V,
        SDL_SCANCODE_ESCAPE,
    };
    for (int scancode : notAttack) {
        CHECK(!command_for_scancode(scancode, &command));
    }

    int attackKeys = 0;
    for (int scancode = 0; scancode < SDL_NUM_SCANCODES; scancode++) {
        if (command_for_scancode(scancode, &command) && command == Command::Attack) {
            attackKeys++;
            CHECK_EQ(scancode, static_cast<int>(SDL_SCANCODE_F));
        }
    }
    CHECK_EQ(attackKeys, 1);

    int mapped = 0;
    for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
        if (command_for_button(button, &command)) {
            mapped++;
            CHECK(command == Command::Interact || command == Command::Inspect);
            CHECK(button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_Y);
        }
    }
    CHECK_EQ(mapped, 2);
}

TEST(swallow_follows_the_router_context)
{
    CommandRouter router;
    command_pump(router, CommandContext::World, [&] {
        bool worldActive = router.context() == CommandContext::World;
        CHECK(command_process_key(router, SDL_SCANCODE_E, true, false, worldActive));
        CHECK(command_process_key(router, SDL_SCANCODE_K, true, false, worldActive));
        CHECK(!command_process_key(router, SDL_SCANCODE_W, true, false, worldActive));
        return 0;
    });
    command_pump(router, kDefaultPumpContext, [&] {
        bool worldActive = router.context() == CommandContext::World;
        CHECK(!command_process_key(router, SDL_SCANCODE_E, true, false, worldActive));
        CHECK(!command_process_key(router, SDL_SCANCODE_K, true, false, worldActive));
        return 0;
    });
}

// A fake clock and the production adapters. Nothing here mirrors the
// controller: the events go through command_handle_axis_event, the pump ends
// with AxisEvents::flush, and the world tick is command_enter_world followed by
// the polls.
namespace {

const float kDeadZone = 0.2f;
const int kTriggerRight = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
const int kRightX = SDL_CONTROLLER_AXIS_RIGHTX;
const int kRightY = SDL_CONTROLLER_AXIS_RIGHTY;

struct Rig {
    CommandRouter router;
    WorldHeartbeat heartbeat;
    AxisEvents axes;

    // The events of a pump. The pump has set the context already.
    void event(int axis, int value, uint32_t timestamp = 0)
    {
        command_handle_axis_event(router, axes, axis, static_cast<int16_t>(value), timestamp, kDeadZone);
    }

    // A default pump, such as a modal's, begins.
    void other()
    {
        command_pump(router, kDefaultPumpContext, no_body);
    }

    void endPump()
    {
        axes.flush(router, kDeadZone);
    }

    void tick(uint32_t now, int trigger, int rawX, int rawY)
    {
        command_enter_world(router, heartbeat, now);
        axes.pollTrigger(router, static_cast<int16_t>(trigger));
        axes.pollStick(router, static_cast<int16_t>(rawX), static_cast<int16_t>(rawY), kDeadZone);
    }
};

} // namespace

TEST(trigger_press_and_release_in_one_pump_still_fires)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);

    rig.event(kTriggerRight, 20000);
    rig.event(kTriggerRight, 0);
    rig.endPump();
    rig.tick(66, 0, 0, 0);

    CHECK(rig.router.take(Command::Attack));
    CHECK(!rig.router.latched(kController, Command::Attack));
    CHECK(!rig.router.held(Command::Attack));

    rig.event(kTriggerRight, 20000);
    CHECK(rig.router.take(Command::Attack));
}

TEST(trigger_press_and_release_in_other_creates_nothing)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);

    // A modal runs its own pump.
    rig.other();
    rig.event(kTriggerRight, 20000);
    rig.event(kTriggerRight, 0);
    rig.endPump();
    CHECK(!rig.router.pending(Command::Attack));
    CHECK(!rig.router.latched(kController, Command::Attack));
    CHECK(!rig.router.held(Command::Attack));
}

TEST(trigger_held_across_a_lapse_fires_only_after_release_and_press)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kTriggerRight, 20000);
    CHECK(rig.router.take(Command::Attack));

    rig.other();
    rig.event(kTriggerRight, 0);
    CHECK(!rig.router.latched(kController, Command::Attack));
    rig.event(kTriggerRight, 20000);
    CHECK(!rig.router.pending(Command::Attack));

    rig.tick(1200, 20000, 0, 0);
    CHECK(!rig.router.take(Command::Attack));
    rig.tick(1216, 20000, 0, 0);
    CHECK(!rig.router.take(Command::Attack));

    rig.tick(1232, 0, 0, 0);
    rig.tick(1248, 20000, 0, 0);
    CHECK(rig.router.take(Command::Attack));
}

TEST(trigger_held_across_a_nested_pump_with_no_events_does_not_refire)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.tick(50, 20000, 0, 0);
    CHECK(rig.router.take(Command::Attack));

    rig.other();
    rig.tick(2000, 20000, 0, 0);
    CHECK(!rig.router.take(Command::Attack));
    CHECK(rig.router.latched(kController, Command::Attack));

    rig.tick(2016, 0, 0, 0);
    rig.tick(2032, 20000, 0, 0);
    CHECK(rig.router.take(Command::Attack));
}

TEST(stick_neutral_then_flick_in_one_pump_posts_both_in_order)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.tick(16, 0, -30000, 0);
    CHECK(rig.router.take(Command::PreviousTarget));
    CHECK(rig.router.held(Command::PreviousTarget));

    rig.event(kRightX, 0, 1);
    rig.event(kRightX, 30000, 2);
    rig.endPump();
    CHECK(!rig.router.held(Command::PreviousTarget));
    CHECK(!rig.router.latched(kController, Command::PreviousTarget));
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(stick_pair_with_one_timestamp_is_evaluated_once)
{
    // X = 25000 alone is a flick. With Y = 30000 the pair is diagonal and is
    // not one. Both events of a physical sample share a timestamp, so the
    // intermediate X-only pair is never evaluated.
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kRightX, 25000, 5);
    rig.event(kRightY, 30000, 5);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));
    CHECK(!rig.router.held(Command::NextTarget));
}

TEST(stick_pair_with_different_timestamps_sees_the_intermediate_pair)
{
    // Different timestamps are two samples, so the X-only pair is evaluated
    // and flicks.
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kRightX, 25000, 5);
    rig.event(kRightY, 30000, 6);
    rig.endPump();
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(stick_axis_order_does_not_matter_inside_a_group)
{
    Rig xy;
    xy.tick(0, 0, 0, 0);
    xy.event(kRightX, 25000, 5);
    xy.event(kRightY, 30000, 5);
    xy.endPump();

    Rig yx;
    yx.tick(0, 0, 0, 0);
    yx.event(kRightY, 30000, 5);
    yx.event(kRightX, 25000, 5);
    yx.endPump();

    CHECK_EQ(xy.router.pending(Command::NextTarget), yx.router.pending(Command::NextTarget));
    CHECK_EQ(xy.router.held(Command::NextTarget), yx.router.held(Command::NextTarget));
    CHECK(!yx.router.pending(Command::NextTarget));

    Rig flickXy;
    flickXy.tick(0, 0, 0, 0);
    flickXy.event(kRightX, 30000, 7);
    flickXy.event(kRightY, 1000, 7);
    flickXy.endPump();
    Rig flickYx;
    flickYx.tick(0, 0, 0, 0);
    flickYx.event(kRightY, 1000, 7);
    flickYx.event(kRightX, 30000, 7);
    flickYx.endPump();
    CHECK(flickXy.router.take(Command::NextTarget));
    CHECK(flickYx.router.take(Command::NextTarget));
}

TEST(stick_flick_in_other_creates_nothing_and_does_not_fire_on_return)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);

    rig.other();
    rig.event(kRightX, 30000, 1);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));

    // Still deflected when the world returns.
    rig.tick(1200, 0, 30000, 0);
    CHECK(!rig.router.take(Command::NextTarget));
    rig.tick(1216, 0, 30000, 0);
    CHECK(!rig.router.take(Command::NextTarget));

    rig.tick(1232, 0, 0, 0);
    rig.tick(1248, 0, 30000, 0);
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(stick_neutral_in_other_posts_released)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.tick(16, 0, 30000, 0);
    CHECK(rig.router.take(Command::NextTarget));

    rig.other();
    rig.event(kRightX, 0, 1);
    rig.endPump();
    CHECK(!rig.router.latched(kController, Command::NextTarget));
    CHECK(!rig.router.held(Command::NextTarget));

    rig.event(kRightX, 30000, 2);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));

    rig.tick(1200, 0, 0, 0);
    rig.tick(1216, 0, 30000, 0);
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(stick_cache_keeps_the_polled_pair)
{
    // The poll leaves X = 0, Y = 30000. An X event alone then makes a diagonal
    // pair with the polled Y, so it is not a flick.
    Rig rig;
    rig.tick(0, 0, 0, 30000);
    rig.event(kRightX, 25000, 3);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));

    // With the polled Y small, the same X event is a flick.
    Rig other;
    other.tick(0, 0, 0, 1000);
    other.event(kRightX, 25000, 3);
    other.endPump();
    CHECK(other.router.take(Command::NextTarget));
}

TEST(reset_clears_trigger_flick_cache_and_group)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kTriggerRight, 20000);
    rig.event(kRightY, 30000, 1);
    rig.event(kRightX, 30000, 1);
    rig.axes.reset();
    rig.router.reset();
    rig.endPump();

    // The trigger is up again: a press fires.
    rig.event(kTriggerRight, 20000);
    CHECK(rig.router.take(Command::Attack));

    // The cached Y is gone and no group is open.
    rig.event(kRightX, 25000, 2);
    rig.endPump();
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(polling_the_state_an_event_produced_makes_no_second_edge)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kTriggerRight, 20000);
    rig.event(kRightX, 30000, 1);
    rig.endPump();
    CHECK(rig.router.take(Command::Attack));
    CHECK(rig.router.take(Command::NextTarget));

    rig.tick(20, 20000, 30000, 0);
    CHECK(!rig.router.pending(Command::Attack));
    CHECK(!rig.router.pending(Command::NextTarget));

    // The poll of a state that the events left open does not post twice either.
    rig.event(kRightX, 0, 2);
    rig.tick(36, 20000, 0, 0);
    CHECK(!rig.router.pending(Command::NextTarget));
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));
}

TEST(other_axes_do_nothing)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(SDL_CONTROLLER_AXIS_LEFTX, 32000, 1);
    rig.event(SDL_CONTROLLER_AXIS_LEFTY, 32000, 1);
    rig.event(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32000, 1);
    rig.endPump();
    for (int command = 0; command < static_cast<int>(Command::Count); command++) {
        CHECK(!rig.router.pending(static_cast<Command>(command)));
        CHECK(!rig.router.held(static_cast<Command>(command)));
    }

    // A left stick event does not open a group for the right stick.
    rig.event(kRightX, 30000, 2);
    rig.event(SDL_CONTROLLER_AXIS_LEFTX, 0, 3);
    rig.endPump();
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(stick_normalize_matches_the_dead_zone_math)
{
    float x;
    float y;
    command_stick_normalize(0.1f, 0.1f, 0.2f, &x, &y);
    CHECK(x == 0.0f && y == 0.0f);

    command_stick_normalize(0.0f, 0.0f, 0.2f, &x, &y);
    CHECK(x == 0.0f && y == 0.0f);

    // Along an axis: (0.6 - 0.2) / 0.8 = 0.5.
    command_stick_normalize(0.6f, 0.0f, 0.2f, &x, &y);
    CHECK(fabsf(x - 0.5f) < 0.0001f && y == 0.0f);

    command_stick_normalize(-1.0f, 0.0f, 0.2f, &x, &y);
    CHECK(fabsf(x + 1.0f) < 0.0001f);

    // Past full deflection the length is capped at 1.
    command_stick_normalize(1.0f, 1.0f, 0.2f, &x, &y);
    CHECK(fabsf(sqrtf(x * x + y * y) - 1.0f) < 0.0001f);

    // The same inputs through the old inline formula.
    float rawX = 0.3f;
    float rawY = -0.4f;
    float raw = sqrtf(rawX * rawX + rawY * rawY);
    float scale = (raw - 0.2f) / (1.0f - 0.2f) / raw;
    command_stick_normalize(rawX, rawY, 0.2f, &x, &y);
    CHECK(x == rawX * scale && y == rawY * scale);
}

TEST(stick_flick_rules_match_the_thresholds)
{
    CommandRouter router;
    FlickDetector flick;

    flick.sample(router, 0.5f, 0.0f);
    CHECK(!router.pending(Command::NextTarget));
    flick.sample(router, 0.7f, 0.9f);
    CHECK(!router.pending(Command::NextTarget));
    flick.sample(router, 0.7f, 0.1f);
    CHECK(router.take(Command::NextTarget));

    // Stays down until |x| < 0.3.
    flick.sample(router, 0.4f, 0.0f);
    flick.sample(router, 0.9f, 0.0f);
    CHECK(!router.pending(Command::NextTarget));
    flick.sample(router, 0.29f, 0.0f);
    flick.sample(router, -0.9f, 0.0f);
    CHECK(router.take(Command::PreviousTarget));
}

TEST(poll_stick_evaluates_the_open_group_before_overwriting_the_cache)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.tick(16, 0, 30000, 0);
    CHECK(rig.router.take(Command::NextTarget));
    CHECK(rig.router.held(Command::NextTarget));

    // The neutral event opens a group. No flush: the poll must evaluate it.
    rig.event(kRightX, 0, 10);
    rig.axes.pollStick(rig.router, 30000, 0, kDeadZone);

    // The neutral was seen (Released), then the poll flicked again.
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(stick_same_axis_twice_in_one_timestamp_keeps_both_edges)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kRightX, 30000, 5);
    rig.event(kRightX, 0, 5);
    rig.endPump();

    CHECK(rig.router.take(Command::NextTarget));
    CHECK(!rig.router.held(Command::NextTarget));
    CHECK(!rig.router.latched(kController, Command::NextTarget));
}

TEST(stick_neutral_and_flick_in_one_timestamp_post_released_then_pressed)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.tick(16, 0, 30000, 0);
    CHECK(rig.router.take(Command::NextTarget));
    CHECK(rig.router.held(Command::NextTarget));

    rig.event(kRightX, 0, 7);
    rig.event(kRightX, 30000, 7);
    rig.endPump();
    CHECK(rig.router.held(Command::NextTarget));
    CHECK(rig.router.take(Command::NextTarget));
}

TEST(stick_groups_with_a_repeated_axis_are_evaluated_in_queue_order)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);

    rig.event(kRightX, 30000, 1);
    rig.event(kRightX, 0, 2);
    // The first group was evaluated when the second arrived.
    CHECK(rig.router.held(Command::NextTarget));
    CHECK(rig.router.take(Command::NextTarget));

    rig.event(kRightX, -30000, 2);
    // The repeated axis closed the neutral group.
    CHECK(!rig.router.held(Command::NextTarget));
    CHECK(!rig.router.pending(Command::PreviousTarget));

    rig.event(kRightX, 0, 3);
    // Now the left flick was evaluated.
    CHECK(rig.router.held(Command::PreviousTarget));
    rig.endPump();
    CHECK(!rig.router.held(Command::PreviousTarget));
    CHECK(rig.router.take(Command::PreviousTarget));
    CHECK(!rig.router.latched(kController, Command::PreviousTarget));
}

TEST(stick_x_then_y_at_one_timestamp_is_still_one_pair)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kRightX, 25000, 9);
    rig.event(kRightY, 30000, 9);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));
    CHECK(!rig.router.held(Command::NextTarget));
}

TEST(stick_x_y_x_at_one_timestamp_splits_at_the_repeated_axis)
{
    // (X1, Y) is evaluated first. The second X starts a new group with the
    // cached Y.
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kRightX, 30000, 9);
    rig.event(kRightY, 1000, 9);
    rig.event(kRightX, 0, 9);
    rig.endPump();
    CHECK(rig.router.take(Command::NextTarget));
    CHECK(!rig.router.held(Command::NextTarget));

    // A diagonal first pair does not flick, and the cached Y keeps the
    // second group diagonal too, where X alone would flick.
    Rig diagonal;
    diagonal.tick(0, 0, 0, 0);
    diagonal.event(kRightX, 0, 9);
    diagonal.event(kRightY, 30000, 9);
    diagonal.event(kRightX, 25000, 9);
    diagonal.endPump();
    CHECK(!diagonal.router.pending(Command::NextTarget));
    CHECK(!diagonal.router.held(Command::NextTarget));
}

TEST(stick_same_axis_rule_ignores_different_timestamps)
{
    // Two different timestamps with different axes: X then Y. The Y event
    // evaluates the X group once, and flush evaluates the Y group once. There
    // is no third evaluation, so one Pressed and one pending only.
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.event(kRightX, 30000, 1);
    rig.event(kRightY, 0, 2);
    rig.endPump();
    CHECK(rig.router.take(Command::NextTarget));
    CHECK(!rig.router.take(Command::NextTarget));
    CHECK(rig.router.held(Command::NextTarget));
}

TEST(stick_flags_are_cleared_when_a_group_closes)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);

    // Pair 1 closes when pair 2 starts. A stale seen flag would make the Y of
    // pair 2 look like a repeat and evaluate the X-only pair (25000, 0), which
    // flicks.
    rig.event(kRightX, 0, 1);
    rig.event(kRightY, 0, 1);
    rig.event(kRightX, 25000, 2);
    rig.event(kRightY, 30000, 2);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));
    CHECK(!rig.router.held(Command::NextTarget));
}

TEST(stick_flags_are_cleared_by_a_flush_and_a_poll)
{
    // The polled Y is 30000, so (25000, 30000) is diagonal and does not flick.
    Rig rig;
    rig.tick(0, 0, 0, 30000);
    rig.event(kRightX, 25000, 5);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));

    // The flush closed the group. Y opens a new one and the second X joins it
    // because X was not updated in the current group. A stale X flag would
    // evaluate the old X with the new Y, (25000, 1000), which flicks.
    rig.event(kRightY, 1000, 5);
    rig.event(kRightX, 0, 5);
    rig.endPump();
    CHECK(!rig.router.pending(Command::NextTarget));
    CHECK(!rig.router.held(Command::NextTarget));

    // The same after a poll.
    Rig polled;
    polled.tick(0, 0, 0, 30000);
    polled.event(kRightX, 25000, 5);
    polled.axes.pollStick(polled.router, 25000, 30000, kDeadZone);
    polled.event(kRightY, 1000, 5);
    polled.event(kRightX, 0, 5);
    polled.endPump();
    CHECK(!polled.router.pending(Command::NextTarget));
    CHECK(!polled.router.held(Command::NextTarget));
}

TEST(axis_events_in_a_nested_pump_create_nothing_and_releases_are_seen)
{
    Rig rig;
    rig.tick(0, 0, 0, 0);
    rig.tick(16, 20000, 30000, 0);
    CHECK(rig.router.take(Command::Attack));
    CHECK(rig.router.take(Command::NextTarget));

    rig.other();
    rig.event(kTriggerRight, 0);
    rig.event(kRightX, 0, 1);
    rig.endPump();
    CHECK(!rig.router.latched(kController, Command::Attack));
    CHECK(!rig.router.latched(kController, Command::NextTarget));

    rig.event(kTriggerRight, 20000);
    rig.event(kRightX, -30000, 2);
    rig.endPump();
    for (int command = 0; command < static_cast<int>(Command::Count); command++) {
        CHECK(!rig.router.pending(static_cast<Command>(command)));
    }
}

namespace {
struct RecoveryRig {
    CommandRouter router;
    AxisEvents axes;
    bool left = true;
    ControllerRecovery recovery;
    ControllerRecoveryContext context { router, axes, left };
    ControllerPhysicalState physical;
    static constexpr float deadZone = 0.2f;
};
}

TEST(controller_recovery_requires_every_button_and_axis_neutral)
{
    for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
        RecoveryRig rig;
        rig.physical.buttons[button] = true;
        rig.recovery.suppress(rig.context, rig.physical);
        CHECK(rig.recovery.suppressed());
        CHECK(!rig.recovery.button(rig.context, button, true, rig.deadZone));
        CHECK(rig.recovery.suppressed());
        CHECK(!rig.recovery.button(rig.context, button, false, rig.deadZone));
        CHECK(!rig.recovery.suppressed());
        CHECK(!rig.left);
    }
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; axis++) {
        RecoveryRig rig;
        rig.physical.axes[axis] = 25000;
        rig.recovery.suppress(rig.context, rig.physical);
        rig.recovery.poll(rig.context, rig.physical, rig.deadZone);
        CHECK(rig.recovery.suppressed());
        CHECK(!rig.recovery.axis(rig.context, axis, 0, rig.deadZone));
        CHECK(!rig.recovery.suppressed());
    }
}

TEST(controller_recovery_seed_final_state_does_not_bypass_queued_release)
{
    for (auto context : { CommandContext::World, CommandContext::Other }) {
        RecoveryRig rig;
        rig.router.setContext(context);
        // SDL's poll already sees the final press. Events still contain release/press.
        rig.physical.buttons[SDL_CONTROLLER_BUTTON_A] = true;
        rig.recovery.suppress(rig.context, rig.physical);
        rig.recovery.beginPump();
        rig.recovery.poll(rig.context, rig.physical, rig.deadZone);
        CHECK(rig.recovery.suppressed());
        CHECK(!rig.recovery.button(rig.context, SDL_CONTROLLER_BUTTON_A, false, rig.deadZone));
        CHECK(!rig.recovery.suppressed());
        CHECK(rig.recovery.button(rig.context, SDL_CONTROLLER_BUTTON_A, true, rig.deadZone));
        rig.recovery.endPump(rig.context, rig.physical, rig.deadZone);
    }
}

TEST(controller_recovery_final_neutral_poll_waits_until_pump_drains)
{
    RecoveryRig rig;
    rig.recovery.suppress(rig.context, rig.physical);
    rig.recovery.beginPump();
    rig.recovery.poll(rig.context, rig.physical, rig.deadZone);
    CHECK(rig.recovery.suppressed());
    CHECK(!rig.recovery.button(rig.context, SDL_CONTROLLER_BUTTON_A, true, rig.deadZone));
    CHECK(rig.recovery.suppressed());
    CHECK(!rig.recovery.button(rig.context, SDL_CONTROLLER_BUTTON_A, false, rig.deadZone));
    CHECK(!rig.recovery.suppressed());
    rig.recovery.endPump(rig.context, rig.physical, rig.deadZone);
}

TEST(controller_recovery_focus_blocks_dispatch_until_gain_and_release)
{
    RecoveryRig rig;
    rig.physical.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 25000;
    rig.recovery.focusLost(rig.context, rig.physical);
    CHECK(!rig.recovery.axis(rig.context, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0, rig.deadZone));
    CHECK(rig.recovery.suppressed());
    rig.recovery.focusGained(rig.context, rig.deadZone);
    CHECK(!rig.recovery.suppressed());
    CHECK(rig.recovery.axis(rig.context, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 25000, rig.deadZone));
    command_handle_axis_event(rig.router, rig.axes, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 25000, 1, rig.deadZone);
    CHECK(rig.router.take(Command::Attack));
}

TEST(controller_recovery_held_trigger_in_other_cannot_arm_world)
{
    RecoveryRig rig;
    rig.router.setContext(CommandContext::Other);
    rig.physical.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 25000;
    rig.recovery.suppress(rig.context, rig.physical);
    rig.recovery.poll(rig.context, rig.physical, rig.deadZone);
    CHECK(rig.recovery.suppressed());
    rig.router.setContext(CommandContext::World);
    CHECK(!rig.recovery.axis(rig.context, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 25000, rig.deadZone));
    CHECK(!rig.router.pending(Command::Attack));
    CHECK(!rig.recovery.axis(rig.context, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0, rig.deadZone));
    CHECK(rig.recovery.axis(rig.context, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 25000, rig.deadZone));
    command_handle_axis_event(rig.router, rig.axes, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 25000, 2, rig.deadZone);
    CHECK(rig.router.take(Command::Attack));
}

TEST(controller_recovery_disconnect_and_recovery_preserve_keyboard)
{
    RecoveryRig rig;
    rig.router.post(kKeyboard, Command::Attack, CommandEdge::Pressed);
    rig.router.post(kController, Command::Interact, CommandEdge::Pressed);
    rig.axes.pollTrigger(rig.router, 25000);
    rig.recovery.disconnect(rig.context);
    CHECK(rig.router.held(Command::Attack));
    CHECK(rig.router.take(Command::Attack));
    CHECK(!rig.router.pending(Command::Interact));
    CHECK(!rig.left);
    rig.recovery.suppress(rig.context, rig.physical);
    rig.recovery.poll(rig.context, rig.physical, rig.deadZone);
    rig.axes.pollTrigger(rig.router, 25000);
    CHECK(rig.router.take(Command::Attack));
}

TEST(controller_recovery_nested_pump_cannot_poll_past_queued_edges)
{
    RecoveryRig rig;
    rig.recovery.suppress(rig.context, rig.physical);
    rig.recovery.beginPump();
    rig.recovery.beginPump();
    rig.recovery.endPump(rig.context, rig.physical, rig.deadZone);
    CHECK(rig.recovery.suppressed());
    rig.recovery.endPump(rig.context, rig.physical, rig.deadZone);
    CHECK(!rig.recovery.suppressed());
}

TEST(controller_button_other_context_preserves_quick_menu_a)
{
    CommandRouter router;
    WorldHeartbeat heartbeat;
    heartbeat.tick(100);
    bool menuPending = false;
    router.setContext(CommandContext::Other);
    CHECK(heartbeat.active(101));
    CHECK(!command_controller_button(router, SDL_CONTROLLER_BUTTON_A, menuPending));
    // The release must not erase a tap queued for the menu poll.
    router.post(kController, Command::Interact, CommandEdge::Released);
    CHECK(menuPending);
    CHECK(!router.pending(Command::Interact));
    router.setContext(CommandContext::World);
    menuPending = false;
    CHECK(command_controller_button(router, SDL_CONTROLLER_BUTTON_A, menuPending));
    CHECK(router.take(Command::Interact));
    CHECK(!menuPending);
}
