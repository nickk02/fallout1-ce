#include "test_harness.h"

#include <stdint.h>

#include "sim/pause_set.h"
#include "sim/sim_clock.h"
#include "sim/tick_accumulator.h"

using namespace fallout;

namespace {

const uint32_t kApTicks = 24;

// The real clock and pause set, and the accumulator reading the clock's count,
// the way the action point consumer reads the simulation's.
struct Rig {
    SimClock clock;
    PauseSet pauses;
    TickAccumulator accumulator { kApTicks };
    uint64_t ticks = 0;
    uint32_t now = 0;
    uint64_t windows = 0;

    // Moves time on by `ms` and reads the accumulator.
    void step(uint32_t ms)
    {
        now += ms;
        clock.advance(now, &pauses, [this] { ticks++; });
        windows += accumulator.take(ticks);
    }

    // Only moves time, the way a frame without a consumer read does.
    void stepNoRead(uint32_t ms)
    {
        now += ms;
        clock.advance(now, &pauses, [this] { ticks++; });
    }
};

// Runs `total` ms in steps of `ms`, with the last step cut to fit.
void run(Rig& rig, uint32_t total, uint32_t ms)
{
    uint32_t done = 0;
    while (done < total) {
        uint32_t step = total - done < ms ? total - done : ms;
        rig.step(step);
        done += step;
    }
}

} // namespace

TEST(tick_accumulator_gives_exactly_150_windows_in_60_seconds_at_frame_steps_of_7_16_17_30_ms)
{
    const uint32_t steps[] = { 17, 30, 7, 16 };
    for (uint32_t ms : steps) {
        Rig rig;
        rig.step(0);
        run(rig, 60000, ms);
        CHECK_EQ(rig.windows, 150u);
    }
}

TEST(tick_accumulator_gives_exactly_150_windows_with_jittery_frames)
{
    Rig rig;
    rig.step(0);

    // A fixed pseudo-random frame length between 5 and 40 ms.
    uint32_t seed = 12345;
    uint32_t done = 0;
    while (done < 60000) {
        seed = seed * 1103515245u + 12345u;
        uint32_t ms = 5 + (seed >> 16) % 36;
        if (done + ms > 60000) {
            ms = 60000 - done;
        }
        rig.step(ms);
        done += ms;
    }

    CHECK_EQ(rig.windows, 150u);
}

TEST(tick_accumulator_first_take_arms_and_returns_zero_whatever_the_count)
{
    TickAccumulator accumulator(kApTicks);
    CHECK(!accumulator.armed());
    CHECK_EQ(accumulator.take(100000), 0u);
    CHECK(accumulator.armed());

    // Nothing was owed for the 100000 ticks before the arm.
    CHECK_EQ(accumulator.take(100023), 0u);
    CHECK_EQ(accumulator.take(100024), 1u);
}

TEST(tick_accumulator_reading_twice_at_the_same_tick_gives_nothing_the_second_time)
{
    TickAccumulator accumulator(kApTicks);
    accumulator.take(0);

    CHECK_EQ(accumulator.take(48), 2u);
    CHECK_EQ(accumulator.take(48), 0u);
    CHECK_EQ(accumulator.take(48), 0u);
}

TEST(tick_accumulator_keeps_the_remainder_between_reads)
{
    TickAccumulator accumulator(kApTicks);
    accumulator.take(0);

    CHECK_EQ(accumulator.take(23), 0u);
    CHECK_EQ(accumulator.take(25), 1u);

    // One tick of the next period is already in.
    CHECK_EQ(accumulator.take(47), 0u);
    CHECK_EQ(accumulator.take(48), 1u);
}

TEST(tick_accumulator_disarm_drops_progress_and_the_ticks_while_away)
{
    TickAccumulator accumulator(kApTicks);
    accumulator.take(0);
    CHECK_EQ(accumulator.take(20), 0u);

    // A menu, a load, or no actor: the owner disarms, ticks go by unread.
    accumulator.disarm();
    CHECK(!accumulator.armed());
    CHECK_EQ(accumulator.take(5000), 0u);

    // Progress started again from the arm: 20 earlier ticks do not count.
    CHECK_EQ(accumulator.take(5023), 0u);
    CHECK_EQ(accumulator.take(5024), 1u);
}

TEST(tick_accumulator_rebase_arms_at_the_given_count_with_no_progress)
{
    TickAccumulator accumulator(kApTicks);
    accumulator.take(0);
    CHECK_EQ(accumulator.take(20), 0u);

    // A fight starts at tick 30: the 20 ticks of progress are dropped and the
    // accumulator is armed, so the next read does not arm again.
    accumulator.rebase(30);
    CHECK(accumulator.armed());
    CHECK_EQ(accumulator.take(53), 0u);
    CHECK_EQ(accumulator.take(54), 1u);

    // Rebase also arms a disarmed accumulator.
    TickAccumulator other(kApTicks);
    other.rebase(100);
    CHECK_EQ(other.take(124), 1u);
}

TEST(tick_accumulator_disarm_and_rebase_differ_on_the_next_read)
{
    TickAccumulator disarmed(kApTicks);
    TickAccumulator rebased(kApTicks);
    disarmed.take(0);
    rebased.take(0);

    disarmed.disarm();
    rebased.rebase(1000);

    // The same count: one pays nothing and arms, the other pays its windows.
    CHECK_EQ(disarmed.take(1048), 0u);
    CHECK_EQ(rebased.take(1048), 2u);
}

TEST(tick_accumulator_resyncs_when_the_tick_counter_restarts)
{
    TickAccumulator accumulator(kApTicks);
    accumulator.take(0);
    CHECK_EQ(accumulator.take(500), 20u);

    // The counter went back to zero (the service was restarted).
    CHECK_EQ(accumulator.take(3), 0u);
    CHECK_EQ(accumulator.take(26), 0u);
    CHECK_EQ(accumulator.take(27), 1u);
}

TEST(tick_accumulator_zero_period_is_one)
{
    TickAccumulator accumulator(0);
    accumulator.take(0);
    CHECK_EQ(accumulator.take(5), 5u);
}

TEST(tick_accumulator_a_pause_gives_no_windows_and_keeps_the_partial_progress)
{
    Rig rig;
    rig.step(0);

    // 300 ms is 18 ticks of the 24 for a window.
    run(rig, 300, 10);
    CHECK_EQ(rig.windows, 0u);

    // Ten seconds paused, with frames still running.
    {
        PauseToken token = rig.pauses.acquire();
        run(rig, 10000, 16);
    }
    rig.step(16);
    CHECK_EQ(rig.windows, 0u);

    // 100 more ms: the 18 ticks from before the pause and these make a window.
    run(rig, 100, 10);
    CHECK_EQ(rig.windows, 1u);
}

TEST(tick_accumulator_a_pause_between_two_reads_pays_nothing_for_its_length)
{
    Rig rig;
    rig.step(0);
    run(rig, 200, 10);

    // The pause starts and ends between two frames and holds no frame at all.
    {
        PauseToken token = rig.pauses.acquire();
        rig.now += 5000;
    }
    rig.step(10);

    uint64_t before = rig.windows;
    CHECK(before <= 1u);
    run(rig, 60000, 17);
    CHECK_EQ(rig.windows - before, 150u);
}

TEST(tick_accumulator_nested_pauses_and_focus_style_overlap_pay_nothing_inside)
{
    Rig rig;
    rig.step(0);
    run(rig, 1000, 10);
    uint64_t before = rig.windows;

    PauseToken modal = rig.pauses.acquire();
    run(rig, 2000, 16);

    // Focus is lost while the modal is open, and the modal closes first.
    PauseToken focus = rig.pauses.acquire();
    run(rig, 2000, 16);
    modal.release();
    run(rig, 3000, 16);
    CHECK_EQ(rig.windows, before);

    focus.release();
    rig.step(16);
    CHECK_EQ(rig.windows, before);

    // Time counts again after the last release.
    run(rig, 10000, 16);
    CHECK(rig.windows - before >= 24u);
    CHECK(rig.windows - before <= 25u);
}

TEST(tick_accumulator_ticks_during_a_menu_are_not_paid_after_a_disarm)
{
    Rig rig;
    rig.step(0);
    run(rig, 500, 10);

    // The owner leaves the game for a menu and disarms. The clock keeps
    // counting; nobody reads.
    rig.accumulator.disarm();
    for (int i = 0; i < 300; i++) {
        rig.stepNoRead(17);
    }

    uint64_t before = rig.windows;
    rig.step(17);
    CHECK_EQ(rig.windows, before);

    // Reading continues from the arm.
    run(rig, 10000, 17);
    CHECK(rig.windows - before >= 24u);
    CHECK(rig.windows - before <= 25u);
}

TEST(ap_regen_grant_gives_one_point_per_window_up_to_the_maximum)
{
    CHECK_EQ(ap_regen_grant(0, 3, 10), 0);
    CHECK_EQ(ap_regen_grant(1, 3, 10), 1);
    CHECK_EQ(ap_regen_grant(4, 3, 10), 4);

    // Capped at the room left.
    CHECK_EQ(ap_regen_grant(20, 8, 10), 2);
    CHECK_EQ(ap_regen_grant(1, 9, 10), 1);
}

TEST(ap_regen_grant_spends_windows_at_full_points_without_effect)
{
    CHECK_EQ(ap_regen_grant(5, 10, 10), 0);

    // A maximum lowered below the points held (a lost perk) grants nothing.
    CHECK_EQ(ap_regen_grant(5, 12, 10), 0);
    CHECK_EQ(ap_regen_grant(5, 0, 0), 0);
}

TEST(ap_regen_grant_handles_a_huge_window_count)
{
    CHECK_EQ(ap_regen_grant(UINT64_MAX, 0, 10), 10);
    CHECK_EQ(ap_regen_grant(UINT64_MAX, 0, 2147483647), 2147483647);
}

TEST(tick_accumulator_full_points_spend_the_window_and_the_next_one_starts_on_time)
{
    // The consumer's whole step for a stretch at full points, then one spent.
    TickAccumulator accumulator(kApTicks);
    accumulator.take(0);
    int ap = 10;
    const int maxAp = 10;

    // 100 ticks at full points: four windows are spent, nothing granted.
    int granted = ap_regen_grant(accumulator.take(100), ap, maxAp);
    CHECK_EQ(granted, 0);

    // Points are spent at tick 100. The window that was running (4 ticks in)
    // ends at 120 and gives the point, not a fresh 24 after the spend.
    ap = 6;
    CHECK_EQ(ap_regen_grant(accumulator.take(119), ap, maxAp), 0);
    CHECK_EQ(ap_regen_grant(accumulator.take(120), ap, maxAp), 1);
}

TEST(tick_accumulator_gives_exactly_150_windows_in_60_seconds_at_17_30_and_144_hz_timestamps)
{
    const uint32_t rates[] = { 17, 30, 144 };
    for (uint32_t hz : rates) {
        Rig rig;
        rig.step(0);

        // Frame k is stamped floor(k * 1000 / hz) ms, so the frame lengths are
        // uneven the way a real rate is (6 or 7 ms at 144 Hz), and the last
        // frame of the minute lands on 60000 ms.
        uint32_t frames = 60 * hz;
        for (uint32_t k = 1; k <= frames; k++) {
            uint32_t stamp = static_cast<uint32_t>(static_cast<uint64_t>(k) * 1000 / hz);
            rig.step(stamp - rig.now);
        }

        CHECK_EQ(rig.now, 60000u);
        CHECK_EQ(rig.ticks, 3600u);
        CHECK_EQ(rig.windows, 150u);
    }
}
