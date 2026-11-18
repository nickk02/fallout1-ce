#include "test_harness.h"

#include <functional>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "sim/tick_accumulator.h"
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
    std::vector<std::string> events;
    int describeCalls = 0;
    int gameplayCalls = 0;
    std::function<void()> gameplay;
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
void fakeEvent(const char* line) { fake.events.push_back(line); }
void fakeGameplayTick()
{
    fake.gameplayCalls++;
    if (fake.gameplay) fake.gameplay();
}

void fakeDescribe(char* out, size_t size)
{
    fake.describeCalls++;
    snprintf(out, size, " game_time=7 extra=ok");
}

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

WorldSimPlatform makeLoggingPlatform(bool withDescribe)
{
    WorldSimPlatform platform = makePlatform();
    platform.event = fakeEvent;
    platform.describe = withDescribe ? fakeDescribe : nullptr;
    return platform;
}

// The line the service writes for these values.
std::string expectPause(bool begin, unsigned int elapsed, uint64_t ticks)
{
    char line[192];
    if (begin) {
        snprintf(line, sizeof(line), "simlog pause begin epoch=%llu ticks=%llu",
            static_cast<unsigned long long>(world_sim_epoch()), static_cast<unsigned long long>(ticks));
    } else {
        snprintf(line, sizeof(line), "simlog pause end epoch=%llu ticks=%llu elapsed_ms=%u",
            static_cast<unsigned long long>(world_sim_epoch()), static_cast<unsigned long long>(ticks), elapsed);
    }
    return line;
}

std::string expectLifecycle(const char* what, uint64_t epoch, uint64_t ticks, int paused)
{
    char line[192];
    snprintf(line, sizeof(line), "simlog %s epoch=%llu ticks=%llu paused=%d", what,
        static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(ticks), paused);
    return line;
}

std::string expectSummary(int mode, uint64_t epoch, uint64_t ticks, uint64_t legacy, const char* suffix)
{
    char line[192];
    snprintf(line, sizeof(line), "simlog summary mode=%d epoch=%llu ticks=%llu legacy_calendar_ticks=%llu%s", mode,
        static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(ticks),
        static_cast<unsigned long long>(legacy), suffix);
    return line;
}

bool startsWith(const std::string& text, const char* prefix)
{
    return text.compare(0, strlen(prefix), prefix) == 0;
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

TEST(world_sim_tick_count_feeds_the_accumulator_across_a_pause_and_a_restart)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    TickAccumulator accumulator(24);
    fake.now = 0;
    fake.pump();
    accumulator.take(world_sim_tick_count());

    // 4.8 s of play is 288 ticks, 12 windows. A held pause adds none.
    run(4800, 8);
    {
        PauseToken token = world_sim_pause();
        run(5000, 8);
    }
    fake.now += 8;
    fake.pump();
    run(8, 8);
    uint64_t windows = accumulator.take(world_sim_tick_count());
    CHECK(windows >= 12u);
    CHECK(windows <= 13u);

    // The service restarts: the count starts again from zero. The accumulator
    // resyncs and pays nothing for it.
    world_sim_exit();
    world_sim_init(WORLD_SIM_OWNED, makePlatform());
    fake.pump();
    CHECK_EQ(accumulator.take(world_sim_tick_count()), 0u);
    run(1000, 8);
    CHECK_EQ(accumulator.take(world_sim_tick_count()), 2u);
}

TEST(world_sim_epoch_changes_on_every_successful_init_in_every_mode)
{
    Scope scope;

    uint64_t last = world_sim_epoch();
    const int modes[] = { WORLD_SIM_EXISTING, WORLD_SIM_SHADOW, WORLD_SIM_OWNED };
    for (int mode : modes) {
        CHECK(world_sim_init(mode, makePlatform()));
        uint64_t now = world_sim_epoch();
        CHECK(now > last);
        last = now;
        world_sim_exit();
        CHECK(world_sim_epoch() > last);
        last = world_sim_epoch();
    }
}

TEST(world_sim_epoch_ignores_a_failed_init_and_a_refused_second_init)
{
    Scope scope;
    uint64_t before = world_sim_epoch();

    WorldSimPlatform noPump = makePlatform();
    noPump.addPump = nullptr;
    CHECK(!world_sim_init(WORLD_SIM_SHADOW, noPump));
    CHECK(!world_sim_init(7, makePlatform()));
    CHECK(!world_sim_init(-1, makePlatform()));
    CHECK_EQ(world_sim_epoch(), before);

    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    uint64_t started = world_sim_epoch();
    CHECK(started > before);

    // The service is already started: this refusal is not a new lifecycle.
    CHECK(!world_sim_init(WORLD_SIM_OWNED, makePlatform()));
    CHECK_EQ(world_sim_epoch(), started);
}

TEST(world_sim_epoch_ignores_a_reset_or_an_exit_when_not_started)
{
    Scope scope;
    uint64_t before = world_sim_epoch();

    world_sim_reset();
    world_sim_exit();
    world_sim_exit();
    CHECK_EQ(world_sim_epoch(), before);
}

TEST(world_sim_epoch_changes_on_each_reset_including_mode_0_and_1)
{
    const int modes[] = { WORLD_SIM_EXISTING, WORLD_SIM_SHADOW, WORLD_SIM_OWNED };
    for (int mode : modes) {
        Scope scope;
        CHECK(world_sim_init(mode, makePlatform()));

        uint64_t first = world_sim_epoch();
        world_sim_reset();
        uint64_t second = world_sim_epoch();
        world_sim_reset();
        uint64_t third = world_sim_epoch();

        // Repeated resets are distinct from each other.
        CHECK(second > first);
        CHECK(third > second);
    }
}

TEST(world_sim_epoch_after_exit_and_init_never_matches_an_earlier_value)
{
    Scope scope;

    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    fake.now = 0;
    fake.pump();
    run(1000, 7);
    CHECK(world_sim_tick_count() > 0u);
    uint64_t stale = world_sim_epoch();

    // The state is deleted and made again: the tick count starts from zero, as
    // it did the first time, but the epoch does not come back.
    world_sim_exit();
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    CHECK_EQ(world_sim_tick_count(), 0u);
    CHECK(world_sim_epoch() != stale);
    CHECK(world_sim_epoch() > stale);

    // And not even after a reset that happens to bring a similar count.
    uint64_t second = world_sim_epoch();
    world_sim_reset();
    CHECK(world_sim_epoch() != stale);
    CHECK(world_sim_epoch() != second);
}

TEST(world_sim_epoch_on_reset_with_held_and_nested_pauses_leaves_the_pauses_alone)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));

    fake.now = 0;
    PauseToken outer = world_sim_pause();
    fake.now = 100;
    PauseToken inner = world_sim_pause();
    uint64_t before = world_sim_epoch();

    fake.now = 5000;
    world_sim_reset();
    uint64_t after = world_sim_epoch();
    CHECK(after != before);

    // Both claims and the freeze are still there, the engine was told once, and
    // nothing was shifted yet.
    CHECK(outer.active());
    CHECK(inner.active());
    CHECK(world_sim_world_paused());
    CHECK(fake.animPaused);
    CHECK_EQ(fake.animCalls, 1);
    CHECK_EQ(fake.originResets, 1);
    CHECK_EQ(fake.shifts.size(), 0u);

    // A second reset inside the same pause is another epoch.
    fake.now = 5200;
    world_sim_reset();
    CHECK(world_sim_epoch() != after);
    CHECK_EQ(fake.originResets, 2);

    // Releases in either order: one thaw, one shift from the last reset.
    fake.now = 5300;
    outer.release();
    CHECK(fake.animPaused);
    inner.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 100u);

    // Releasing does not change the epoch.
    uint64_t settled = world_sim_epoch();
    PauseToken again = world_sim_pause();
    again.release();
    CHECK_EQ(world_sim_epoch(), settled);
}

TEST(world_sim_epoch_does_not_change_the_clock_or_the_tick_count)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));

    fake.now = 0;
    fake.pump();
    run(1000, 7);
    uint64_t ticks = world_sim_tick_count();
    CHECK(ticks > 0u);

    // Reading the epoch is free of effects.
    uint64_t epoch = world_sim_epoch();
    CHECK_EQ(world_sim_epoch(), epoch);
    CHECK_EQ(world_sim_tick_count(), ticks);

    // A reset forgets the clock's time but keeps the count, as before.
    world_sim_reset();
    CHECK_EQ(world_sim_tick_count(), ticks);
    fake.now += 600000;
    fake.pump();
    CHECK_EQ(world_sim_tick_count(), ticks);
    run(1000, 7);
    CHECK(world_sim_tick_count() > ticks);
}

TEST(world_sim_epoch_outlives_tokens_held_across_exit_and_reinit)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));
    PauseToken token = world_sim_pause();
    uint64_t first = world_sim_epoch();

    world_sim_exit();
    CHECK(!token.active());
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));

    // The token of the earlier service is inert in the new one, and the epoch
    // says so too.
    CHECK(!world_sim_world_paused());
    CHECK(world_sim_epoch() != first);
}

TEST(world_sim_invalidate_changes_the_epoch_in_every_mode)
{
    const int modes[] = { WORLD_SIM_EXISTING, WORLD_SIM_SHADOW, WORLD_SIM_OWNED };
    for (int mode : modes) {
        Scope scope;
        CHECK(world_sim_init(mode, makePlatform()));

        uint64_t first = world_sim_epoch();
        world_sim_invalidate();
        uint64_t second = world_sim_epoch();
        world_sim_invalidate();

        CHECK(second > first);
        CHECK(world_sim_epoch() > second);
    }
}

TEST(world_sim_invalidate_does_nothing_when_not_started)
{
    Scope scope;
    uint64_t before = world_sim_epoch();

    world_sim_invalidate();
    CHECK_EQ(world_sim_epoch(), before);

    // Nor after an exit.
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    world_sim_exit();
    uint64_t stopped = world_sim_epoch();
    world_sim_invalidate();
    CHECK_EQ(world_sim_epoch(), stopped);
}

TEST(world_sim_invalidate_leaves_the_clock_and_tick_count_alone)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));

    fake.now = 0;
    fake.pump();
    run(1000, 7);
    uint64_t ticks = world_sim_tick_count();
    CHECK(ticks > 0u);

    // Unlike a reset, the clock keeps its last time: the next 7 ms frame counts
    // exactly what it would have, and a big jump is not forgotten as a reset
    // would forget it (it is cut by the frame limit instead).
    world_sim_invalidate();
    CHECK_EQ(world_sim_tick_count(), ticks);
    run(1000, 7);
    CHECK(world_sim_tick_count() >= ticks + 59u);
    CHECK(world_sim_tick_count() <= ticks + 61u);
}

TEST(world_sim_invalidate_with_held_and_nested_pauses_keeps_the_origin_and_the_shift)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));

    fake.now = 1000;
    PauseToken outer = world_sim_pause();
    fake.now = 1500;
    PauseToken inner = world_sim_pause();
    uint64_t before = world_sim_epoch();

    fake.now = 4000;
    world_sim_invalidate();
    world_sim_invalidate();
    CHECK(world_sim_epoch() > before);

    // Nothing the engine is told about changed: no origin reset, no thaw, no
    // shift, and both claims are live.
    CHECK_EQ(fake.originResets, 0);
    CHECK_EQ(fake.animCalls, 1);
    CHECK(fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 0u);
    CHECK(outer.active());
    CHECK(inner.active());
    CHECK(world_sim_world_paused());

    // The pause is measured from where it began, not from the invalidation.
    fake.now = 6000;
    inner.release();
    CHECK(fake.animPaused);
    outer.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 5000u);
}

TEST(world_sim_invalidate_during_a_pause_discards_the_paused_time_not_the_clock)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));

    fake.now = 0;
    fake.pump();
    run(500, 7);
    uint64_t before = world_sim_tick_count();

    {
        PauseToken token = world_sim_pause();
        run(5000, 7);
        world_sim_invalidate();
        run(5000, 7);
    }

    // The pause gave no ticks, with or without the invalidation inside it.
    fake.now += 7;
    fake.pump();
    CHECK_EQ(world_sim_tick_count(), before);
}

TEST(world_sim_log_one_outer_pause_gives_one_begin_and_one_end_line)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeLoggingPlatform(false)));
    fake.events.clear();

    fake.now = 1000;
    PauseToken first = world_sim_pause();
    PauseToken second = world_sim_pause();
    fake.now = 1100;
    PauseToken third = world_sim_pause();
    fake.now = 1250;
    first.release();
    third.release();
    CHECK_EQ(fake.events.size(), 1u);
    second.release();

    CHECK_EQ(fake.events.size(), 2u);
    CHECK_EQ(fake.events[0], expectPause(true, 0, 0));
    CHECK_EQ(fake.events[1], expectPause(false, 250, 0));
}

TEST(world_sim_log_pause_lines_carry_the_tick_count_and_each_pause_is_logged)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeLoggingPlatform(false)));
    fake.now = 0;
    fake.pump();
    run(1000, 5);
    uint64_t ticksAtFirst = world_sim_tick_count();
    CHECK(ticksAtFirst > 0);
    fake.events.clear();

    PauseToken token = world_sim_pause();
    fake.now += 40;
    token.release();

    fake.now += 40;
    token = world_sim_pause();
    fake.now += 75;
    token.release();

    CHECK_EQ(fake.events.size(), 4u);
    CHECK_EQ(fake.events[0], expectPause(true, 0, ticksAtFirst));
    CHECK_EQ(fake.events[1], expectPause(false, 40, ticksAtFirst));
    CHECK_EQ(fake.events[3], expectPause(false, 75, ticksAtFirst));
}

TEST(world_sim_log_pause_end_elapsed_is_right_across_the_timer_wrap)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeLoggingPlatform(false)));
    fake.events.clear();

    fake.now = 0xFFFFFF00u;
    PauseToken token = world_sim_pause();
    fake.now = 0x00000100u;
    token.release();

    CHECK_EQ(fake.events.size(), 2u);
    CHECK_EQ(fake.events[1], expectPause(false, 0x200u, 0));
}

TEST(world_sim_log_lifecycle_lines_carry_the_epoch_and_the_paused_flag)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeLoggingPlatform(false)));
    uint64_t epoch = world_sim_epoch();

    CHECK_EQ(fake.events.size(), 1u);
    CHECK_EQ(fake.events[0], expectLifecycle("init", epoch, 0, 0));

    fake.events.clear();
    world_sim_reset();
    CHECK_EQ(fake.events.size(), 1u);
    CHECK_EQ(fake.events[0], expectLifecycle("reset", epoch + 1, 0, 0));

    world_sim_invalidate();
    CHECK_EQ(fake.events.size(), 2u);
    CHECK_EQ(fake.events[1], expectLifecycle("invalidate", epoch + 2, 0, 0));

    PauseToken token = world_sim_pause();
    fake.events.clear();
    world_sim_reset();
    world_sim_invalidate();
    CHECK_EQ(fake.events.size(), 2u);
    CHECK_EQ(fake.events[0], expectLifecycle("reset", epoch + 3, 0, 1));
    CHECK_EQ(fake.events[1], expectLifecycle("invalidate", epoch + 4, 0, 1));

    fake.events.clear();
    world_sim_exit();
    CHECK_EQ(world_sim_epoch(), epoch + 5);
    CHECK_EQ(fake.events.size(), 1u);
    CHECK_EQ(fake.events[0], expectLifecycle("exit", epoch + 5, 0, 1));

    // The token outlived the service. Releasing it writes nothing.
    token.release();
    CHECK_EQ(fake.events.size(), 1u);
}

TEST(world_sim_log_exit_while_unpaused_and_nothing_when_not_started)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makeLoggingPlatform(false)));
    uint64_t epoch = world_sim_epoch();
    fake.events.clear();

    world_sim_exit();
    CHECK_EQ(fake.events.size(), 1u);
    CHECK_EQ(fake.events[0], expectLifecycle("exit", epoch + 1, 0, 0));

    // Not started: no state, so no lines, even for the calls that are allowed.
    world_sim_exit();
    world_sim_reset();
    world_sim_invalidate();
    CHECK_EQ(fake.events.size(), 1u);
}

TEST(world_sim_log_summary_appears_once_per_600_ticks_with_the_describe_suffix)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeLoggingPlatform(true)));
    uint64_t epoch = world_sim_epoch();
    fake.now = 0;
    fake.pump();
    fake.events.clear();

    world_sim_note_legacy_calendar_tick();
    run(60000, 7);
    CHECK_EQ(world_sim_tick_count(), 3600u);
    CHECK_EQ(fake.events.size(), 6u);
    CHECK_EQ(fake.describeCalls, 6);

    for (size_t i = 0; i < fake.events.size(); i++) {
        CHECK_EQ(fake.events[i], expectSummary(2, epoch, 600 * (i + 1), 1, " game_time=7 extra=ok"));
    }

    // The existing line is untouched.
    CHECK_EQ(fake.logs.size(), 6u);
}

TEST(world_sim_log_summary_without_describe_has_no_suffix)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeLoggingPlatform(false)));
    uint64_t epoch = world_sim_epoch();
    fake.now = 0;
    fake.pump();
    fake.events.clear();

    run(10000, 7);
    CHECK_EQ(fake.events.size(), 1u);
    CHECK_EQ(fake.events[0], expectSummary(2, epoch, 600, 0, ""));
    CHECK_EQ(fake.describeCalls, 0);
}

TEST(world_sim_log_mode_1_has_lifecycle_and_summary_but_no_pause_lines)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makeLoggingPlatform(true)));
    uint64_t epoch = world_sim_epoch();
    fake.now = 0;
    fake.pump();

    PauseToken token = world_sim_pause();
    fake.now += 500;
    token.release();
    run(12000, 7);
    world_sim_reset();

    for (size_t i = 0; i < fake.events.size(); i++) {
        CHECK(!startsWith(fake.events[i], "simlog pause"));
    }

    CHECK_EQ(fake.events.size(), 3u);
    CHECK_EQ(fake.events[0], expectLifecycle("init", epoch, 0, 0));
    CHECK(startsWith(fake.events[1], "simlog summary mode=1 "));
    CHECK_EQ(fake.events[2], expectLifecycle("reset", epoch + 1, world_sim_tick_count(), 0));
}

TEST(world_sim_log_mode_0_writes_nothing)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_EXISTING, makeLoggingPlatform(true)));
    world_sim_reset();
    world_sim_invalidate();
    world_sim_exit();
    CHECK_EQ(fake.events.size(), 0u);
    CHECK_EQ(fake.describeCalls, 0);
}

TEST(world_sim_log_with_no_event_function_emits_nothing_and_never_describes)
{
    Scope scope;
    WorldSimPlatform platform = makePlatform();
    platform.describe = fakeDescribe;
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    fake.now = 0;
    fake.pump();

    PauseToken token = world_sim_pause();
    fake.now += 100;
    token.release();
    run(10000, 7);
    world_sim_reset();
    world_sim_invalidate();
    world_sim_exit();

    CHECK_EQ(fake.events.size(), 0u);
    CHECK_EQ(fake.describeCalls, 0);
    CHECK_EQ(fake.shifts.size(), 1u);
}

namespace {

// A sink that breaks the rule: on the first line that starts with `prefix` it
// calls every world_sim function that changes the lifecycle.
struct Reentry {
    const char* prefix = nullptr;
    bool fromDescribe = false;
    int fired = 0;
    bool initResult = true;
    bool tokenActive = true;
    int modeInside = -1;
    bool ownsInside = false;
    uint64_t epochBefore = 0;
    uint64_t epochAfter = 0;
    int pumpRemovesBefore = 0;
    int pumpRemovesAfter = 0;
};

Reentry reentry;

void breakTheRules()
{
    reentry.fired++;
    reentry.epochBefore = world_sim_epoch();
    reentry.pumpRemovesBefore = fake.pumpRemoves;

    world_sim_exit();
    world_sim_reset();
    world_sim_invalidate();
    reentry.initResult = world_sim_init(WORLD_SIM_SHADOW, makeLoggingPlatform(false));
    PauseToken token = world_sim_pause();
    reentry.tokenActive = token.active();

    reentry.modeInside = world_sim_mode();
    reentry.ownsInside = world_sim_owns_pauses();
    reentry.epochAfter = world_sim_epoch();
    reentry.pumpRemovesAfter = fake.pumpRemoves;
}

void breakingEvent(const char* line)
{
    fakeEvent(line);
    if (reentry.fired == 0 && !reentry.fromDescribe && reentry.prefix != nullptr && startsWith(line, reentry.prefix)) {
        breakTheRules();
    }
}

void breakingDescribe(char* out, size_t size)
{
    fakeDescribe(out, size);
    if (reentry.fired == 0) {
        breakTheRules();
    }
}

WorldSimPlatform makeBreakingPlatform(const char* prefix, bool fromDescribe)
{
    reentry = Reentry();
    reentry.prefix = prefix;
    reentry.fromDescribe = fromDescribe;
    WorldSimPlatform platform = makePlatform();
    platform.event = breakingEvent;
    platform.describe = fromDescribe ? breakingDescribe : nullptr;
    return platform;
}

// What every refused call must have left behind.
void checkRefused()
{
    CHECK_EQ(reentry.fired, 1);
    CHECK(!reentry.initResult);
    CHECK(!reentry.tokenActive);
    CHECK_EQ(reentry.modeInside, static_cast<int>(WORLD_SIM_OWNED));
    CHECK(reentry.ownsInside);
    CHECK_EQ(reentry.epochAfter, reentry.epochBefore);
    CHECK_EQ(reentry.pumpRemovesAfter, reentry.pumpRemovesBefore);
}

// A normal exit still works and removes the pump once.
void checkNormalExit(int removesBefore)
{
    world_sim_exit();
    CHECK_EQ(world_sim_mode(), static_cast<int>(WORLD_SIM_EXISTING));
    CHECK_EQ(fake.pumpRemoves, removesBefore + 1);
    CHECK(fake.focus == previousFocus);

    // And the service can be started again.
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    world_sim_exit();
}

} // namespace

TEST(world_sim_event_sink_cannot_change_the_lifecycle_on_pause_begin)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeBreakingPlatform("simlog pause begin", false)));
    uint64_t epoch = world_sim_epoch();

    PauseToken token = world_sim_pause();
    checkRefused();
    CHECK(token.active());
    CHECK_EQ(world_sim_epoch(), epoch);
    CHECK_EQ(world_sim_mode(), static_cast<int>(WORLD_SIM_OWNED));
    CHECK_EQ(fake.pumpRemoves, 0);

    // The pause itself completed.
    CHECK_EQ(fake.animCalls, 1);
    CHECK(fake.animPaused);
    CHECK(world_sim_world_paused());

    token.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
    checkNormalExit(0);
}

TEST(world_sim_event_sink_cannot_exit_again_from_the_exit_line)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeBreakingPlatform("simlog exit", false)));
    uint64_t epoch = world_sim_epoch();

    world_sim_exit();
    checkRefused();

    // The real exit finished once.
    CHECK_EQ(fake.pumpRemoves, 1);
    CHECK_EQ(world_sim_mode(), static_cast<int>(WORLD_SIM_EXISTING));
    CHECK_EQ(world_sim_epoch(), epoch + 1);
    CHECK(fake.focus == previousFocus);

    // A second exit is a no-op, and a new start works.
    world_sim_exit();
    CHECK_EQ(fake.pumpRemoves, 1);
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));
    world_sim_exit();
}

TEST(world_sim_event_sink_cannot_change_the_lifecycle_on_the_summary_line)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makeBreakingPlatform("simlog summary", false)));
    uint64_t epoch = world_sim_epoch();
    fake.now = 0;
    fake.pump();
    run(10000, 7);

    checkRefused();
    CHECK_EQ(world_sim_epoch(), epoch);
    CHECK_EQ(fake.pumpRemoves, 0);
    CHECK(world_sim_tick_count() >= 600u);

    // The clock went on after the summary.
    uint64_t ticks = world_sim_tick_count();
    run(100, 7);
    CHECK(world_sim_tick_count() > ticks);
    checkNormalExit(0);
}

TEST(world_sim_describe_cannot_change_the_lifecycle)
{
    Scope scope;
    WorldSimPlatform platform = makeBreakingPlatform(nullptr, true);
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    uint64_t epoch = world_sim_epoch();
    fake.now = 0;
    fake.pump();
    run(10000, 7);

    checkRefused();
    CHECK_EQ(world_sim_epoch(), epoch);
    CHECK_EQ(fake.pumpRemoves, 0);

    // The summary line was still written, with the suffix.
    bool found = false;
    for (size_t i = 0; i < fake.events.size(); i++) {
        if (startsWith(fake.events[i], "simlog summary") && fake.events[i].find(" game_time=7 extra=ok") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
    checkNormalExit(0);
}

TEST(world_sim_gameplay_only_mode2_and_once_per_tick)
{
    for (int mode : { WORLD_SIM_EXISTING, WORLD_SIM_SHADOW, WORLD_SIM_OWNED }) {
        Scope scope;
        auto platform = makePlatform();
        platform.gameplayTick = fakeGameplayTick;
        CHECK(world_sim_init(mode, platform));
        if (fake.pump != nullptr) {
            fake.pump();
            fake.now = 100;
            fake.pump();
        }
        CHECK_EQ(fake.gameplayCalls, mode == WORLD_SIM_OWNED ? 6 : 0);
    }
}

TEST(world_sim_gameplay_lifecycle_stops_batch_and_rebases)
{
    for (int change = 0; change < 4; change++) {
        Scope scope;
        auto platform = makePlatform();
        platform.gameplayTick = fakeGameplayTick;
        CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
        fake.pump();
        fake.gameplay = [&, change] {
            if (fake.gameplayCalls != 1) return;
            if (change == 0) world_sim_reset();
            if (change == 1) world_sim_invalidate();
            if (change >= 2) world_sim_exit();
            if (change == 3) {
                CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
                // A replacement service cannot recursively pump during old delivery.
                fake.pump();
            }
        };
        fake.now = 100;
        auto oldPump = fake.pump;
        oldPump();
        CHECK_EQ(fake.gameplayCalls, 1);
        if (change == 2) {
            CHECK(fake.pump == nullptr);
            oldPump();
            CHECK_EQ(fake.gameplayCalls, 1);
        } else {
            fake.now = 200;
            fake.pump(); // New baseline; no abandoned elapsed ticks replay.
            CHECK_EQ(fake.gameplayCalls, 1);
            fake.now = 250;
            fake.pump();
            CHECK_EQ(fake.gameplayCalls, 4);
        }
    }
}

TEST(world_sim_gameplay_nested_pump_is_not_duplicate_or_new_sample)
{
    Scope scope;
    auto platform = makePlatform();
    platform.gameplayTick = fakeGameplayTick;
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    fake.pump();
    fake.gameplay = [] { fake.pump(); };
    fake.now = 100;
    fake.pump();
    CHECK_EQ(fake.gameplayCalls, 6);
    CHECK_EQ(world_sim_tick_count(), 6u);
}

TEST(world_sim_gameplay_pause_inside_tick_stops_remaining_batch)
{
    for (bool releaseImmediately : { false, true }) {
        Scope scope;
        auto platform = makePlatform();
        platform.gameplayTick = fakeGameplayTick;
        CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
        fake.pump();
        PauseToken held;
        fake.gameplay = [&] {
            if (fake.gameplayCalls == 1) {
                held = world_sim_pause();
                if (releaseImmediately) held.release();
            }
        };
        fake.now = 100;
        fake.pump();
        CHECK_EQ(fake.gameplayCalls, 1);
        fake.now = 200;
        fake.pump();
        CHECK_EQ(fake.gameplayCalls, releaseImmediately ? 7 : 1);
        held.release();
    }
}

TEST(world_sim_gameplay_coexists_with_periodic_diagnostics)
{
    Scope scope;
    auto platform = makeLoggingPlatform(true);
    platform.gameplayTick = fakeGameplayTick;
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    fake.pump();
    run(10000, 10);
    CHECK_EQ(fake.gameplayCalls, 600);
    CHECK_EQ(fake.describeCalls, 1);
    CHECK_EQ(fake.logs.size(), 1u);
    CHECK_EQ(fake.events.size(), 2u); // Init plus summary.
}

TEST(world_sim_gameplay_exit_while_paused_thaws_and_old_token_is_inert)
{
    Scope scope;
    auto platform = makePlatform();
    platform.gameplayTick = fakeGameplayTick;
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    fake.pump();
    PauseToken held;
    fake.gameplay = [&] {
        held = world_sim_pause();
        CHECK(held.active());
        CHECK(fake.animPaused);
        world_sim_exit();
        CHECK(!fake.animPaused);
        CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    };
    fake.now = 100;
    fake.pump();
    CHECK_EQ(fake.gameplayCalls, 1);
    CHECK_EQ(world_sim_tick_count(), 0u);
    CHECK(!world_sim_world_paused());
    auto shifts = fake.shifts.size();
    held.release();
    CHECK_EQ(fake.shifts.size(), shifts);
}

TEST(world_sim_gameplay_epoch_change_at_summary_skips_old_diagnostics)
{
    Scope scope;
    auto platform = makeLoggingPlatform(true);
    platform.gameplayTick = fakeGameplayTick;
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    fake.pump();
    fake.gameplay = [] {
        if (fake.gameplayCalls == 600) world_sim_invalidate();
    };
    run(10000, 10);
    CHECK_EQ(fake.gameplayCalls, 600);
    CHECK_EQ(fake.describeCalls, 0);
    CHECK_EQ(fake.logs.size(), 0u);
    CHECK_EQ(fake.events.size(), 2u); // Init and invalidation, no stale summary.
}

TEST(world_sim_explicit_drain_rejects_reentrant_intent_publication)
{
    Scope scope;
    auto platform = makePlatform();
    platform.gameplayTick = fakeGameplayTick;
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    CHECK(world_sim_drain());
    int freshPublications = 0;
    fake.gameplay = [&] { if (world_sim_drain()) freshPublications++; };
    fake.now = 100;
    CHECK(world_sim_drain());
    CHECK_EQ(fake.gameplayCalls, 6);
    CHECK_EQ(freshPublications, 0);
    world_sim_exit();
    CHECK(!world_sim_drain());
}

TEST(world_sim_explicit_drain_rejects_sample_from_replaced_epoch)
{
    Scope scope;
    auto platform = makePlatform();
    platform.gameplayTick = fakeGameplayTick;
    CHECK(world_sim_init(WORLD_SIM_OWNED, platform));
    CHECK(world_sim_drain());
    fake.gameplay = [] { world_sim_invalidate(); };
    fake.now = 100;
    CHECK(!world_sim_drain());
    CHECK_EQ(fake.gameplayCalls, 1);
    CHECK(world_sim_drain()); // Fresh baseline, no callback.
    CHECK_EQ(fake.gameplayCalls, 1);
}
