#include "test_harness.h"

#include <string>
#include <vector>

#include "sim/modal_pause.h"
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

} // namespace

TEST(modal_pause_mode_0_holds_nothing_and_never_pauses)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_EXISTING, makePlatform()));

    {
        ModalPause modal;
        CHECK(!world_sim_world_paused());
        modal.release();
    }

    CHECK_EQ(fake.animCalls, 0);
    CHECK_EQ(fake.shifts.size(), 0u);
}

TEST(modal_pause_mode_1_never_pauses_the_world)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_SHADOW, makePlatform()));

    fake.now = 100;
    {
        ModalPause modal;
        CHECK(!world_sim_world_paused());
        fake.now = 900;
    }

    CHECK(!world_sim_world_paused());
    CHECK_EQ(fake.animCalls, 0);
    CHECK_EQ(fake.shifts.size(), 0u);
}

TEST(modal_pause_mode_2_pauses_while_alive_and_resumes_on_destruction)
{
    Scope scope;
    CHECK(world_sim_init(WORLD_SIM_OWNED, makePlatform()));

    fake.now = 1000;
    {
        ModalPause modal;
        CHECK(world_sim_world_paused());
        CHECK(fake.animPaused);
        CHECK_EQ(fake.animCalls, 1);
        fake.now = 1800;
    }

    CHECK(!world_sim_world_paused());
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.animCalls, 2);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 800u);
}

TEST(modal_pause_mode_2_nested_modals_pause_once_and_shift_once)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    {
        ModalPause outer;
        fake.now = 100;
        {
            ModalPause inner;
            CHECK_EQ(fake.animCalls, 1);
            fake.now = 300;
        }

        // The inner one is gone, the outer one still holds the world.
        CHECK(fake.animPaused);
        CHECK(world_sim_world_paused());
        CHECK_EQ(fake.shifts.size(), 0u);
        fake.now = 500;
    }

    CHECK(!fake.animPaused);
    CHECK_EQ(fake.animCalls, 2);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 500u);
}

TEST(modal_pause_mode_2_release_twice_is_safe)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    ModalPause first;
    ModalPause second;

    fake.now = 40;
    first.release();
    first.release();

    // The second release did not give back the other modal's claim.
    CHECK(fake.animPaused);
    CHECK(world_sim_world_paused());

    fake.now = 90;
    second.release();
    second.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 90u);
}

TEST(modal_pause_mode_2_release_order_does_not_matter)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    ModalPause outer;
    fake.now = 10;
    ModalPause inner;

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

TEST(modal_pause_mode_2_can_outlive_world_sim_exit)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    {
        ModalPause modal;
        CHECK(fake.animPaused);
        world_sim_exit();
        modal.release();
        modal.release();
    }

    // The destructor of a second one after exit also does nothing.
    world_sim_init(WORLD_SIM_OWNED, makePlatform());
    ModalPause late;
    world_sim_exit();
}

TEST(modal_pause_moved_modal_keeps_the_single_claim)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    ModalPause a;
    ModalPause b(static_cast<ModalPause&&>(a));
    a.release();
    CHECK(fake.animPaused);

    fake.now = 20;
    b.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.shifts.size(), 1u);
}

TEST(modal_pause_mode_2_dialogue_with_nested_barter_thaws_and_shifts_once)
{
    Scope scope;
    world_sim_init(WORLD_SIM_OWNED, makePlatform());

    fake.now = 0;
    fake.pump();
    fake.now = 100;
    fake.pump();
    uint64_t before = world_sim_tick_count();

    // gdialog_enter takes its pause, then the talk script runs and the pump
    // keeps running from the dialogue loop.
    fake.now = 1000;
    ModalPause dialogue;
    CHECK(fake.animPaused);
    CHECK_EQ(fake.animCalls, 1);

    for (unsigned int t = 1007; t <= 2000; t += 7) {
        fake.now = t;
        fake.pump();
    }
    CHECK_EQ(world_sim_tick_count(), before);

    // Barter opens and closes inside the dialogue. No thaw, no shift.
    fake.now = 2100;
    {
        ModalPause barter;
        fake.now = 2600;
    }
    CHECK(fake.animPaused);
    CHECK_EQ(fake.animCalls, 1);
    CHECK_EQ(fake.shifts.size(), 0u);
    CHECK(world_sim_world_paused());

    // More dialogue, then gdialog_enter returns.
    fake.now = 4000;
    dialogue.release();
    CHECK(!fake.animPaused);
    CHECK_EQ(fake.animCalls, 2);
    CHECK_EQ(fake.shifts.size(), 1u);
    CHECK_EQ(fake.shifts[0], 3000u);
    CHECK(!world_sim_world_paused());

    // The first pump after the dialogue counts nothing.
    fake.now = 4007;
    fake.pump();
    CHECK_EQ(world_sim_tick_count(), before);
}
