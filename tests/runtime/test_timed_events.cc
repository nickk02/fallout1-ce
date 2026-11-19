#include "test_harness.h"

#include "sim/timed_events.h"

using namespace fallout;

TEST(timed_events_second_tick_every_100_ms)
{
    TimedEventClock clock;
    clock.rebase(1000);

    CHECK(!clock.secondTickDue(1099));
    CHECK(clock.secondTickDue(1100));

    // The interval restarts at the tick.
    CHECK(!clock.secondTickDue(1199));
    CHECK(clock.secondTickDue(1210));
}

TEST(timed_events_light_update_every_30_seconds)
{
    TimedEventClock clock;
    clock.rebase(0);

    CHECK(!clock.lightUpdateDue(29999));
    CHECK(clock.lightUpdateDue(30000));
    CHECK(!clock.lightUpdateDue(59999));
    CHECK(clock.lightUpdateDue(60000));
}

TEST(timed_events_stale_rebase_fires_everything_after_a_long_pause)
{
    // This is the failure the rebase has to avoid: restarted from the clock
    // reading of the last background pass (just before a 30 s pause), the
    // intervals are already due when the pass runs again.
    TimedEventClock stale;
    unsigned int lastPassBeforePause = 1000;
    stale.rebase(lastPassBeforePause);

    unsigned int firstPassAfterPause = 31020;
    CHECK(stale.lightUpdateDue(firstPassAfterPause));
    CHECK(stale.secondTickDue(firstPassAfterPause));
}

TEST(timed_events_rebase_from_the_current_time_fires_nothing_early)
{
    TimedEventClock clock;

    // Focus returns at 31010 and the pause is released with that reading.
    clock.rebase(31010);

    unsigned int firstPassAfterPause = 31020;
    CHECK(!clock.lightUpdateDue(firstPassAfterPause));
    CHECK(!clock.secondTickDue(firstPassAfterPause));

    // Then the normal rhythm resumes.
    CHECK(clock.secondTickDue(31110));
    CHECK(clock.lightUpdateDue(61010));
}

TEST(timed_events_a_start_in_the_future_counts_as_due)
{
    // The same rule as elapsed_tocks(): used by the legacy code, kept as is.
    TimedEventClock clock;
    clock.rebase(5000);
    CHECK(clock.secondTickDue(4000));
}

TEST(timed_events_wrapped_counter_still_measures_forward)
{
    TimedEventClock clock;
    clock.rebase(0xFFFFFF00u);

    // After the wrap the new reading is smaller than the start. The legacy rule
    // calls that due, so the tick fires once and the interval restarts.
    CHECK(clock.secondTickDue(0x20u));
    CHECK(!clock.secondTickDue(0x30u));
}
