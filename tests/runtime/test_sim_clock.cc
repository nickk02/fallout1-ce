#include "test_harness.h"

#include "sim/pause_set.h"
#include "sim/sim_clock.h"

using namespace fallout;

namespace {

// Deterministic jitter source.
uint32_t nextRandom(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state >> 16;
}

// Feeds `seconds` of frames at `frameRate` into a clock, with frame times off by
// up to `jitter` ms, and checks the tick count against the exact value after
// every frame. Returns the final tick count.
uint64_t runFrames(SimClock& clock, uint32_t start, uint32_t frameRate, uint32_t seconds, uint32_t jitter, const PauseSet* pauses = nullptr)
{
    uint32_t random = 12345;
    uint32_t frames = frameRate * seconds;
    uint32_t previous = start;

    clock.advance(start, pauses, nullptr);

    for (uint32_t frame = 1; frame <= frames; frame++) {
        uint32_t elapsed = static_cast<uint32_t>(static_cast<uint64_t>(frame) * 1000 / frameRate);
        if (frame != frames && jitter != 0) {
            elapsed = elapsed + nextRandom(random) % (2 * jitter + 1) - jitter;
        }

        uint32_t now = start + elapsed;
        if (static_cast<int32_t>(now - previous) <= 0) {
            now = previous + 1;
        }
        previous = now;

        clock.advance(now, pauses, nullptr);

        // Exact at every frame, not only at the end.
        CHECK_EQ(clock.tickCount(), static_cast<uint64_t>(now - start) * 60 / 1000);
    }

    return clock.tickCount();
}

} // namespace

TEST(sim_clock_sixty_seconds_is_3600_ticks_at_30_hz)
{
    SimClock clock;
    CHECK_EQ(runFrames(clock, 1000, 30, 60, 0), 3600u);
    CHECK_EQ(clock.droppedUnits(), 0u);
}

TEST(sim_clock_sixty_seconds_is_3600_ticks_at_60_hz)
{
    SimClock clock;
    CHECK_EQ(runFrames(clock, 1000, 60, 60, 0), 3600u);
}

TEST(sim_clock_sixty_seconds_is_3600_ticks_at_144_hz)
{
    SimClock clock;
    CHECK_EQ(runFrames(clock, 1000, 144, 60, 0), 3600u);
}

TEST(sim_clock_sixty_seconds_with_jitter_is_3600_ticks)
{
    uint32_t rates[] = { 30, 60, 144 };
    for (uint32_t rate : rates) {
        SimClock clock;
        CHECK_EQ(runFrames(clock, 5, rate, 60, 2), 3600u);
        CHECK_EQ(clock.droppedUnits(), 0u);
    }
}

TEST(sim_clock_counts_across_the_32_bit_wrap)
{
    SimClock clock;
    CHECK_EQ(runFrames(clock, 0xFFFFFF00u, 60, 60, 1), 3600u);
}

TEST(sim_clock_first_call_only_records_the_time)
{
    SimClock clock;
    int ticks = 0;
    CHECK_EQ(clock.advance(123456, nullptr, [&ticks]() { ticks++; }), 0u);
    CHECK_EQ(ticks, 0);
    CHECK_EQ(clock.tickCount(), 0u);
}

TEST(sim_clock_runs_the_tick_function_once_per_tick)
{
    SimClock clock;
    clock.advance(0, nullptr, nullptr);

    int ticks = 0;
    CHECK_EQ(clock.advance(100, nullptr, [&ticks]() { ticks++; }), 6u);
    CHECK_EQ(ticks, 6);
}

TEST(sim_clock_long_frame_is_clamped_and_counted)
{
    SimClock clock;
    clock.advance(0, nullptr, nullptr);

    // 250 ms is the default limit: 15 ticks.
    CHECK_EQ(clock.advance(1000, nullptr, nullptr), 15u);
    CHECK_EQ(clock.droppedUnits(), 750u);
}

TEST(sim_clock_clamp_can_be_turned_off)
{
    SimClock::Config config;
    config.maxFrameDelta = 0;
    SimClock clock(config);
    clock.advance(0, nullptr, nullptr);
    CHECK_EQ(clock.advance(1000, nullptr, nullptr), 60u);
    CHECK_EQ(clock.droppedUnits(), 0u);
}

TEST(sim_clock_zero_config_values_become_one)
{
    SimClock::Config config;
    config.tickRate = 0;
    config.unitsPerSecond = 0;
    SimClock clock(config);
    clock.advance(0, nullptr, nullptr);
    CHECK_EQ(clock.advance(3, nullptr, nullptr), 3u);
}

TEST(sim_clock_paused_time_is_discarded)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);
    CHECK_EQ(clock.advance(16, &pauses, nullptr), 0u);

    PauseToken token = pauses.acquire();
    CHECK_EQ(clock.advance(5000, &pauses, nullptr), 0u);
    CHECK_EQ(clock.droppedUnits(), 0u);

    token.release();

    // One frame after the release counts only that frame.
    uint64_t before = clock.tickCount();
    clock.advance(5016, &pauses, nullptr);
    CHECK(clock.tickCount() - before <= 1);
}

TEST(sim_clock_pause_clears_the_remainder)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    // 10 ms leaves 600 of the 1000 needed for the next tick.
    CHECK_EQ(clock.advance(10, &pauses, nullptr), 0u);

    PauseToken token = pauses.acquire();
    clock.advance(20, &pauses, nullptr);
    token.release();

    // Without clearing, 7 more ms would complete a tick.
    CHECK_EQ(clock.advance(27, &pauses, nullptr), 0u);
}

TEST(sim_clock_pause_inside_a_tick_stops_the_batch)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    PauseToken token;
    int ticks = 0;
    uint32_t ran = clock.advance(250, &pauses, [&]() {
        ticks++;
        if (ticks == 3) {
            token = pauses.acquire();
        }
    });
    CHECK_EQ(ran, 3u);
    CHECK_EQ(ticks, 3);

    // The rest of the batch is gone, not replayed after the release.
    token.release();
    CHECK_EQ(clock.advance(267, &pauses, nullptr), 0u);
    CHECK_EQ(clock.advance(284, &pauses, nullptr), 1u);
}

TEST(sim_clock_nested_advance_does_nothing)
{
    SimClock clock;
    clock.advance(0, nullptr, nullptr);

    int nestedTotal = 0;
    int ticks = 0;
    clock.advance(100, nullptr, [&]() {
        ticks++;
        nestedTotal += clock.advance(100, nullptr, nullptr);
        nestedTotal += clock.advance(160, nullptr, nullptr);
    });
    CHECK_EQ(ticks, 6);
    CHECK_EQ(nestedTotal, 0);
    CHECK_EQ(clock.tickCount(), 6u);

    // The nested times were ignored whole, so the next call measures from 100.
    // 17 ms is one tick.
    CHECK_EQ(clock.advance(117, nullptr, nullptr), 1u);
}

TEST(sim_clock_reset_forgets_the_last_time)
{
    SimClock clock;
    clock.advance(0, nullptr, nullptr);
    clock.advance(100, nullptr, nullptr);
    clock.reset();
    CHECK_EQ(clock.advance(900000, nullptr, nullptr), 0u);
    CHECK_EQ(clock.droppedUnits(), 0u);
}

TEST(sim_clock_pause_that_began_and_ended_between_calls_voids_the_interval)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    // A modal screen paused and resumed without a call in between.
    pauses.acquire().release();

    CHECK_EQ(clock.advance(10000, &pauses, nullptr), 0u);
    CHECK_EQ(clock.droppedUnits(), 0u);

    // The interval after that counts again, from the discarded call.
    CHECK_EQ(clock.advance(10017, &pauses, nullptr), 1u);
}

TEST(sim_clock_several_pauses_between_calls_void_one_interval)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    for (int i = 0; i < 5; i++) {
        pauses.acquire().release();
    }

    CHECK_EQ(clock.advance(200, &pauses, nullptr), 0u);
    CHECK_EQ(clock.advance(217, &pauses, nullptr), 1u);
}

TEST(sim_clock_pause_and_release_inside_a_tick_drops_the_batch)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    int ticks = 0;
    uint32_t ran = clock.advance(250, &pauses, [&]() {
        ticks++;
        if (ticks == 3) {
            pauses.acquire().release();
        }
    });
    CHECK_EQ(ran, 3u);
    CHECK(!pauses.isPaused());

    CHECK_EQ(clock.advance(267, &pauses, nullptr), 1u);
}

TEST(sim_clock_pause_before_the_first_call_is_not_a_change)
{
    PauseSet pauses;
    pauses.acquire().release();

    SimClock clock;
    clock.advance(0, &pauses, nullptr);
    CHECK_EQ(clock.advance(17, &pauses, nullptr), 1u);
}

TEST(sim_clock_switching_pause_sets_does_not_void_the_interval)
{
    PauseSet first;
    PauseSet second;
    second.acquire().release();

    SimClock clock;
    clock.advance(0, &first, nullptr);
    CHECK_EQ(clock.advance(17, &second, nullptr), 1u);
    CHECK_EQ(clock.advance(34, nullptr, nullptr), 1u);
}

TEST(sim_clock_backward_sample_is_ignored)
{
    SimClock clock;
    clock.advance(1000, nullptr, nullptr);
    CHECK_EQ(clock.advance(999, nullptr, nullptr), 0u);
    CHECK_EQ(clock.backwardSamples(), 1u);
    CHECK_EQ(clock.droppedUnits(), 0u);

    // The next normal sample measures from the last accepted time.
    CHECK_EQ(clock.advance(1017, nullptr, nullptr), 1u);
    CHECK_EQ(clock.tickCount(), 1u);
}

TEST(sim_clock_backward_sample_while_paused_is_ignored)
{
    SimClock clock;
    PauseSet pauses;
    PauseToken token = pauses.acquire();
    clock.advance(1000, &pauses, nullptr);
    CHECK_EQ(clock.advance(500, &pauses, nullptr), 0u);
    CHECK_EQ(clock.backwardSamples(), 1u);
}

TEST(sim_clock_forward_step_across_the_wrap_is_not_backward)
{
    SimClock clock;
    clock.advance(0xFFFFFFF0u, nullptr, nullptr);

    // 32 ms across the wrap.
    CHECK_EQ(clock.advance(0x10u, nullptr, nullptr), 1u);
    CHECK_EQ(clock.backwardSamples(), 0u);
}

TEST(sim_clock_reset_inside_a_tick_ends_the_batch)
{
    SimClock clock;
    clock.advance(0, nullptr, nullptr);

    int ticks = 0;
    uint32_t ran = clock.advance(250, nullptr, [&]() {
        ticks++;
        if (ticks == 3) {
            clock.reset();
        }
    });
    CHECK_EQ(ran, 3u);

    // The next call only records the time.
    CHECK_EQ(clock.advance(100000, nullptr, nullptr), 0u);
    CHECK_EQ(clock.advance(100017, nullptr, nullptr), 1u);
}

TEST(sim_clock_throwing_tick_leaves_the_clock_usable)
{
    SimClock clock;
    clock.advance(0, nullptr, nullptr);

    bool caught = false;
    try {
        clock.advance(100, nullptr, []() { throw 1; });
    } catch (int) {
        caught = true;
    }
    CHECK(caught);

    // The re-entrancy flag was cleared.
    CHECK(clock.advance(200, nullptr, nullptr) > 0);
}

TEST(sim_clock_first_pump_after_a_late_release_counts_nothing)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    PauseToken token = pauses.acquire();

    // Several pumps while the pause is held.
    CHECK_EQ(clock.advance(1000, &pauses, nullptr), 0u);
    CHECK_EQ(clock.advance(3000, &pauses, nullptr), 0u);
    CHECK_EQ(clock.advance(5000, &pauses, nullptr), 0u);

    // The release comes after the last paused pump.
    token.release();

    CHECK_EQ(clock.advance(10000, &pauses, nullptr), 0u);
    CHECK_EQ(clock.droppedUnits(), 0u);
    CHECK_EQ(clock.advance(10017, &pauses, nullptr), 1u);
}

TEST(sim_clock_nested_pauses_resume_only_after_the_last_release)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    PauseToken a = pauses.acquire();
    PauseToken b = pauses.acquire();
    clock.advance(1000, &pauses, nullptr);

    // One holder left: still paused, nothing runs.
    a.release();
    CHECK_EQ(clock.advance(2000, &pauses, nullptr), 0u);

    b.release();
    CHECK_EQ(clock.advance(3000, &pauses, nullptr), 0u);
    CHECK_EQ(clock.advance(3017, &pauses, nullptr), 1u);
}

TEST(sim_clock_release_in_the_middle_of_nesting_does_not_void_a_running_clock)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    // Both holders are taken and one is released between pumps, then the other
    // is held across a pump: the set was paused all along, so this is one
    // voided interval and then the clock resumes normally.
    PauseToken a = pauses.acquire();
    PauseToken b = pauses.acquire();
    a.release();
    CHECK_EQ(clock.advance(1000, &pauses, nullptr), 0u);
    b.release();
    CHECK_EQ(clock.advance(1010, &pauses, nullptr), 0u);
    CHECK_EQ(clock.advance(1027, &pauses, nullptr), 1u);
}

TEST(sim_clock_runs_normally_after_the_resume_pump)
{
    SimClock clock;
    PauseSet pauses;
    clock.advance(0, &pauses, nullptr);

    pauses.acquire();
    clock.advance(500, &pauses, nullptr);
    pauses.acquire().release();
    // The first token above was a temporary and is already released.
    CHECK_EQ(clock.advance(600, &pauses, nullptr), 0u);

    // 60 ms after the resume pump is 3 ticks plus 0.6 of the next.
    CHECK_EQ(clock.advance(660, &pauses, nullptr), 3u);
}

TEST(sim_clock_reset_and_destroy_the_pause_set_inside_a_tick)
{
    SimClock clock;
    PauseSet* pauses = new PauseSet();
    clock.advance(0, pauses, nullptr);

    int ticks = 0;
    uint32_t ran = clock.advance(250, pauses, [&]() {
        ticks++;
        // World teardown: forget the clock, then free the set.
        clock.reset();
        delete pauses;
        pauses = nullptr;
    });
    CHECK_EQ(ran, 1u);
    CHECK_EQ(ticks, 1);

    // A new set afterwards: the first call only records the time.
    PauseSet next;
    CHECK_EQ(clock.advance(1000, &next, nullptr), 0u);
    CHECK_EQ(clock.advance(1017, &next, nullptr), 1u);
}
