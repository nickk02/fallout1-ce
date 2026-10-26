#include "test_harness.h"

#include <string>
#include <vector>

#include "sim/world_sim.h"

using namespace fallout;

namespace {

// Stands in for the engine: records what the service asks of it.
struct FakeEngine {
    unsigned int now = 0;
    int pumpAdds = 0;
    int pumpRemoves = 0;
    WorldSimPumpFunction* pump = nullptr;
    WorldSimFocusFunction* focus = nullptr;
    int animCalls = 0;
    bool animPaused = false;
    std::vector<unsigned int> shifts;
    std::vector<unsigned int> shiftNows;
    int originResets = 0;
    std::vector<std::string> logs;
    std::vector<int> previousFocusCalls;
};

FakeEngine fake;

unsigned int fakeNow() { return fake.now; }

void fakeAddPump(WorldSimPumpFunction* function)
{
    fake.pumpAdds++;
    fake.pump = function;
}

void fakeRemovePump(WorldSimPumpFunction* function)
{
    fake.pumpRemoves++;
    if (fake.pump == function) {
        fake.pump = nullptr;
    }
}

WorldSimFocusFunction* fakeGetFocus() { return fake.focus; }
void fakeSetFocus(WorldSimFocusFunction* function) { fake.focus = function; }

void fakeSetWorldPaused(bool paused)
{
    fake.animCalls++;
    fake.animPaused = paused;
}

void fakeShiftTimers(unsigned int elapsed, unsigned int now)
{
    fake.shifts.push_back(elapsed);
    fake.shiftNows.push_back(now);
}

void fakePauseOriginReset() { fake.originResets++; }
void fakeLog(const char* text) { fake.logs.push_back(text); }

void previousFocus(int gained) { fake.previousFocusCalls.push_back(gained); }

WorldSimPlatform makePlatform()
{
    WorldSimPlatform platform = {};
    platform.now = fakeNow;
    platform.addPump = fakeAddPump;
    platform.removePump = fakeRemovePump;
    platform.getFocus = fakeGetFocus;
    platform.setFocus = fakeSetFocus;
    platform.setWorldPaused = fakeSetWorldPaused;
    platform.shiftTimers = fakeShiftTimers;
    platform.pauseOriginReset = fakePauseOriginReset;
    platform.log = fakeLog;
    return platform;
}

// Starts each test from a clean engine and stops the service when it ends, even
// if a check fails.
class Scope {
public:
    Scope()
    {
        world_sim_exit();
        fake = FakeEngine();
        fake.focus = previousFocus;
    }

    ~Scope() { world_sim_exit(); }
};

// Calls the pump every `step` ms until `duration` ms have passed.
void run(unsigned int duration, unsigned int step)
{
    unsigned int end = fake.now + duration;
    while (fake.now < end) {
        fake.now = fake.now + step < end ? fake.now + step : end;
        fake.pump();
    }
}

} // namespace

TEST(world_sim_mode_0_registers_nothing_and_tracks_nothing)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_EXISTING, makePlatform()));

    CHECK_EQ(fake.pumpAdds, 0);
    CHECK(fake.focus == previousFocus);
    CHECK_EQ(world_sim_mode(), 0);
    CHECK(!world_sim_owns_pauses());

    PauseToken token = world_sim_pause();
    CHECK(!token.active());
    CHECK(!world_sim_world_paused());

    world_sim_reset();
    world_sim_note_legacy_calendar_tick();
    CHECK_EQ(world_sim_tick_count(), 0u);

    world_sim_exit();
    CHECK_EQ(fake.pumpRemoves, 0);
    CHECK(fake.focus == previousFocus);
}

TEST(world_sim_mode_1_counts_ticks_and_touches_no_game_state)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    CHECK_EQ(fake.pumpAdds, 1);
    CHECK(!world_sim_owns_pauses());

    fake.now = 5000;
    fake.pump();
    run(60000, 7);

    // 60 s of unpaused time with no frame over the clamp.
    CHECK_EQ(world_sim_tick_count(), 3600u);
    CHECK_EQ(fake.logs.size(), 6u);

    world_sim_note_legacy_calendar_tick();
    world_sim_note_legacy_calendar_tick();
    CHECK_EQ(world_sim_legacy_calendar_ticks(), 2u);

    // Pauses are tracked, but nothing is frozen or shifted and the legacy code
    // is not told the world is paused.
    PauseToken token = world_sim_pause();
    CHECK(token.active());
    CHECK(!world_sim_world_paused());
    fake.now += 3000;
    token.release();

    CHECK_EQ(fake.animCalls, 0);
    CHECK_EQ(fake.shifts.size(), 0u);
}

TEST(world_sim_mode_1_does_not_count_paused_time)
{
    Scope scope;
    world_sim_init(WORLD_SIM_SHADOW, makePlatform());

    fake.now = 0;
    fake.pump();
    run(30000, 7);

    // A menu for 10 s of wall time, with the pump running throughout.
    PauseToken token = world_sim_pause();
    run(10000, 7);
    token.release();

    run(30000, 7);

    // Only the 60 s outside the menu counts, less the one frame voided on
    // resume. It never reaches 3600 with the menu time included (4200).
    CHECK(world_sim_tick_count() <= 3600u);
    CHECK(world_sim_tick_count() >= 3598u);
}

TEST(world_sim_mode_2_nested_pause_and_inventory_thaw_once)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));
    CHECK(world_sim_owns_pauses());

    // The combat pause opens.
    fake.now = 1000;
    PauseToken combatPause = world_sim_pause();
    CHECK_EQ(fake.animCalls, 1);
    CHECK(fake.animPaused);
    CHECK(world_sim_world_paused());

    // The inventory opens inside it and closes again.
    fake.now = 1500;
    PauseToken inventory = world_sim_pause();
    CHECK_EQ(fake.animCalls, 1);
    fake.now = 1700;
    inventory.release();

    // Still frozen, and nothing shifted yet.
    CHECK(fake.animPaused);
    CHECK_EQ(fake.animCalls, 1);
    CHECK_EQ(fake.shifts.size(), 0u);
    CHECK(world_sim_world_paused());

    // The outer pause ends: one thaw, one shift of the whole length.
    fake.now = 4000;
    combatPause.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.animCalls, 2);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 3000u);
    CHECK(!world_sim_world_paused());
}

TEST(world_sim_mode_2_release_order_does_not_matter)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    PauseToken outer = world_sim_pause();
    fake.now = 10;
    PauseToken inner = world_sim_pause();

    // The outer one goes first.
    fake.now = 50;
    outer.release();
    CHECK(fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 0u);

    fake.now = 80;
    inner.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 80u);
}

TEST(world_sim_mode_2_two_separate_pauses_shift_separately)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    world_sim_pause().release();
    fake.now = 100;
    PauseToken second = world_sim_pause();
    fake.now = 160;
    second.release();

    CHECK_EQ(fake.shifts.size(), 2u);
    CHECK_EQ(fake.shifts[0], 0u);
    CHECK_EQ(fake.shifts[1], 60u);
}

TEST(world_sim_mode_2_shift_is_correct_across_the_timer_wrap)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0xFFFFFFF0u;
    PauseToken token = world_sim_pause();
    fake.now = 0x20u;
    token.release();

    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 0x30u);
}

TEST(world_sim_focus_loss_pauses_and_nests_with_a_modal)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    // A modal is open, then the window loses focus and regains it.
    fake.now = 100;
    PauseToken modal = world_sim_pause();
    fake.now = 200;
    fake.focus(0);
    CHECK_EQ(fake.animCalls, 1);
    fake.now = 900;
    fake.focus(1);

    // Back in focus, but the modal still holds the pause.
    CHECK(fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 0u);

    fake.now = 1000;
    modal.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 900u);
}

TEST(world_sim_focus_loss_alone_pauses_and_resumes_once)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    fake.focus(0);
    CHECK(fake.animPaused);
    CHECK(world_sim_world_paused());

    fake.now = 5000;
    fake.focus(1);
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 5000u);
}

TEST(world_sim_repeated_focus_events_keep_one_claim)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.focus(0);
    fake.focus(0);
    fake.focus(1);
    CHECK(!world_sim_world_paused());

    // A regain with no loss does nothing.
    fake.focus(1);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.animCalls, 2);
}

TEST(world_sim_focus_events_reach_the_previous_callback)
{
    Scope scope;
    world_sim_init(WORLD_SIM_SHADOW, makePlatform());

    fake.focus(0);
    fake.focus(1);
    CHECK_EQ(fake.previousFocusCalls.size(), 2u);
    CHECK_EQ(fake.previousFocusCalls[0], 0);
    CHECK_EQ(fake.previousFocusCalls[1], 1);
}

TEST(world_sim_mode_2_blocks_the_clock_while_paused)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    fake.pump();
    run(1000, 7);
    uint64_t before = world_sim_tick_count();
    CHECK(before > 0);

    fake.focus(0);
    run(5000, 7);
    CHECK_EQ(world_sim_tick_count(), before);

    // The first pump after the release counts nothing either.
    fake.focus(1);
    fake.now += 7;
    fake.pump();
    CHECK_EQ(world_sim_tick_count(), before);
}

TEST(world_sim_reset_keeps_live_tokens_and_rebases_the_pause_start)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    // A load happens inside a modal that holds the pause.
    fake.now = 0;
    PauseToken modal = world_sim_pause();
    fake.now = 5000;
    world_sim_reset();

    CHECK(world_sim_world_paused());
    CHECK(modal.active());

    fake.now = 5100;
    modal.release();

    // Only the time since the reset, so new timers are not pushed by the time
    // before the load.
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 100u);
}

TEST(world_sim_reset_forgets_the_clock_time)
{
    Scope scope;
    world_sim_init(WORLD_SIM_SHADOW, makePlatform());

    fake.now = 0;
    fake.pump();
    run(1000, 7);
    uint64_t before = world_sim_tick_count();

    world_sim_reset();
    fake.now += 600000;
    fake.pump();
    CHECK_EQ(world_sim_tick_count(), before);
}

TEST(world_sim_second_init_is_refused_and_registers_once)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    CHECK(!world_sim_init(WORLD_SIM_OWNED, makePlatform()));
    CHECK_EQ(fake.pumpAdds, 1);
    CHECK_EQ(world_sim_mode(), 1);
}

TEST(world_sim_exit_restores_the_focus_callback_and_removes_the_pump)
{
    Scope scope;
    world_sim_init(WORLD_SIM_SHADOW, makePlatform());
    CHECK(fake.focus != previousFocus);

    world_sim_exit();
    CHECK(fake.focus == previousFocus);
    CHECK_EQ(fake.pumpRemoves, 1);
    CHECK(fake.pump == nullptr);
    CHECK_EQ(world_sim_mode(), 0);

    // A second exit changes nothing.
    world_sim_exit();
    CHECK_EQ(fake.pumpRemoves, 1);
}

TEST(world_sim_can_start_again_after_exit)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    world_sim_exit();
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));
    CHECK_EQ(fake.pumpAdds, 2);
    CHECK_EQ(world_sim_mode(), 2);
    CHECK(fake.focus != previousFocus);
}

TEST(world_sim_exit_leaves_a_replaced_focus_callback_alone)
{
    Scope scope;
    world_sim_init(WORLD_SIM_SHADOW, makePlatform());
    WorldSimFocusFunction* ours = fake.focus;

    // Something else took over the callback after us.
    static void (*other)(int) = [](int) {};
    fake.focus = other;

    world_sim_exit();
    CHECK(fake.focus == other);

    // Our callback may still be called through the chain. It must not crash and
    // must reach the previous one.
    ours(0);
    CHECK_EQ(fake.previousFocusCalls.size(), 1u);
}

TEST(world_sim_init_failure_registers_nothing)
{
    Scope scope;

    WorldSimPlatform noPump = makePlatform();
    noPump.addPump = nullptr;
    CHECK(!world_sim_init(WORLD_SIM_SHADOW, noPump));

    WorldSimPlatform noThaw = makePlatform();
    noThaw.setWorldPaused = nullptr;
    CHECK(!world_sim_init(WORLD_SIM_OWNED, noThaw));

    CHECK(!world_sim_init(7, makePlatform()));
    CHECK(!world_sim_init(-1, makePlatform()));

    CHECK_EQ(fake.pumpAdds, 0);
    CHECK(fake.focus == previousFocus);
    CHECK_EQ(world_sim_mode(), 0);

    // A failed start does not block a good one.
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
}

TEST(world_sim_tokens_outlive_exit_safely)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    PauseToken token = world_sim_pause();
    CHECK(fake.animPaused);

    // Exit while a pause is held thaws the animations once, with no shift.
    world_sim_exit();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 0u);

    CHECK(!token.active());
    token.release();
    CHECK_EQ(fake.shifts.size(), 0u);
    CHECK_EQ(fake.animCalls, 2);
}

TEST(world_sim_shift_gets_the_current_time_not_an_old_reading)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    // The window loses focus. The last background pass ran at 1000.
    fake.now = 1000;
    fake.pump();
    fake.now = 1010;
    fake.focus(0);

    // Thirty seconds later focus returns, before any background pass runs.
    fake.now = 31010;
    fake.focus(1);

    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 30000u);
    CHECK_EQ(fake.shiftNows[0], 31010u);
}

TEST(world_sim_reset_tells_the_engine_only_while_a_pause_is_held)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    world_sim_reset();
    CHECK_EQ(fake.originResets, 0);

    PauseToken token = world_sim_pause();
    world_sim_reset();
    CHECK_EQ(fake.originResets, 1);

    token.release();
    world_sim_reset();
    CHECK_EQ(fake.originResets, 1);
}

TEST(world_sim_mode_1_reset_never_tells_the_engine)
{
    Scope scope;
    world_sim_init(WORLD_SIM_SHADOW, makePlatform());

    PauseToken token = world_sim_pause();
    world_sim_reset();
    CHECK_EQ(fake.originResets, 0);
}

TEST(world_sim_world_pause_hook_is_called_once_per_outer_pause)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    // The engine suspends its interpreter on true and resumes on false, as a
    // counted pair, so the calls must balance however the pauses nest.
    PauseToken a = world_sim_pause();
    PauseToken b = world_sim_pause();
    PauseToken c = world_sim_pause();
    b.release();
    a.release();
    c.release();
    CHECK_EQ(fake.animCalls, 2);

    PauseToken d = world_sim_pause();
    world_sim_exit();
    CHECK_EQ(fake.animCalls, 4);
    CHECK(!fake.animPaused);
}
