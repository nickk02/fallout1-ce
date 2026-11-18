// The calendar coordinator (src/sim/calendar_pull.cc) on the game's real event
// queue (src/game/queue.cc) through the real adapter (src/game/calendar_queue.cc).
// The queue's handler table is pointed at handlers that write to a list, the
// way test_queue.cc does. The simulation ticks come from the test, or from the
// real SimClock and PauseSet where a frame rate or a pause is the point.

#include "test_harness.h"

#include "queue_stubs.h"

#include <functional>
#include <limits.h>
#include <string.h>
#include <string>
#include <vector>

#include "game/calendar_queue.h"
#include "game/queue.h"
#include "game/scripts.h"
#include "sim/calendar_pull.h"
#include "sim/pause_set.h"
#include "sim/sim_clock.h"

using namespace fallout;
using namespace fallout_test;

namespace {

// The game time seen by each handler call, in order, and the tags.
std::vector<int> seenTimes;
std::vector<int> seenTags;

// Run by the handler of the event with this tag.
std::function<void(int tag)> onHandle;

// The result a handler returns for a tag.
std::vector<int> results;

int tags[64];

int* tag(int value)
{
    tags[value] = value;
    return &tags[value];
}

int handler(Object*, void* data)
{
    int value = *static_cast<int*>(data);
    seenTimes.push_back(game_time());
    seenTags.push_back(value);
    if (onHandle) {
        onHandle(value);
    }
    return value >= 0 && value < static_cast<int>(results.size()) ? results[value] : 0;
}

EventTypeDescription savedTable[EVENT_TYPE_COUNT];

class Scope {
public:
    Scope()
    {
        for (int i = 0; i < EVENT_TYPE_COUNT; i++) {
            savedTable[i] = q_func[i];
        }
        queue_stub_reset();
        queue_init();
        seenTimes.clear();
        seenTags.clear();
        onHandle = nullptr;
        results.clear();
        q_func[EVENT_TYPE_SCRIPT].handlerProc = handler;
        q_func[EVENT_TYPE_SCRIPT].freeProc = nullptr;
    }

    ~Scope()
    {
        queue_clear();
        for (int i = 0; i < EVENT_TYPE_COUNT; i++) {
            q_func[i] = savedTable[i];
        }
        onHandle = nullptr;
        queue_stub_reset();
    }
};

// A queue event `delay` after the game time now, with this tag.
void add(int delay, int value)
{
    queue_add(delay, nullptr, tag(value), EVENT_TYPE_SCRIPT);
}

// The coordinator with the environment the owner will give it.
struct Rig {
    CalendarPull pull;
    GameCalendarQueue queue;
    uint64_t epoch = 1;
    uint64_t ticks = 0;
    bool quit = false;
    bool deferred = false;
    CalendarPullEnv env;

    Rig() { build(); }

    explicit Rig(const CalendarPullConfig& config)
        : pull(config)
    {
        build();
    }

    void build()
    {
        env.epoch = [this] { return epoch; };
        env.simTicks = [this] { return ticks; };
        env.quit = [this] { return quit; };
        env.deferred = [this] { return deferred; };
    }

    CalendarPullResult run() { return pull.pull(queue, env); }

    // Arms the coordinator (the first pull only arms it).
    void arm()
    {
        CHECK(run().status == CalendarPullStatus::Rearmed);
    }
};

} // namespace

TEST(calendar_pull_first_pull_only_arms_and_pays_nothing)
{
    Scope scope;
    queueStub.now = 100;
    Rig rig;
    rig.ticks = 5000;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Rearmed);
    CHECK_EQ(result.gameTicks, 0u);
    CHECK_EQ(queueStub.now, 100);
    CHECK(rig.pull.armed());

    // The count before the arm is not owed.
    CHECK(rig.run().status == CalendarPullStatus::Done);
    CHECK_EQ(queueStub.now, 100);
}

TEST(calendar_pull_six_sim_ticks_make_one_game_tick_and_events_run_at_their_own_tick)
{
    Scope scope;
    queueStub.now = 1000;
    add(1, 1);
    add(2, 2);
    add(50, 3);
    add(100, 4);
    add(101, 5);

    Rig rig;
    rig.arm();

    // 600 sim ticks, one second at 60 Hz times ten: 100 game ticks.
    rig.ticks = 600;
    CalendarPullResult result = rig.run();

    // The default cap is 30 game ticks, so one pass pays 30 and drops the rest.
    CHECK(result.status == CalendarPullStatus::Done);
    CHECK_EQ(result.gameTicks, 30u);
    CHECK_EQ(result.droppedTicks, 70u);
    CHECK_EQ(queueStub.now, 1030);

    // Events 1 and 2 ran at exactly their time.
    const std::vector<int> times = { 1001, 1002 };
    const std::vector<int> tagsSeen = { 1, 2 };
    CHECK(seenTimes == times);
    CHECK(seenTags == tagsSeen);
}

TEST(calendar_pull_pays_100_game_ticks_for_600_sim_ticks_when_pulled_often_enough)
{
    Scope scope;
    queueStub.now = 1000;
    add(50, 3);
    add(100, 4);
    add(101, 5);

    Rig rig;
    rig.arm();

    // Pulled every 10 sim ticks (a sixth of a second).
    for (int step = 0; step < 60; step++) {
        rig.ticks += 10;
        CHECK(rig.run().status == CalendarPullStatus::Done);
    }

    CHECK_EQ(queueStub.now, 1100);
    const std::vector<int> times = { 1050, 1100 };
    const std::vector<int> tagsSeen = { 3, 4 };
    CHECK(seenTimes == times);
    CHECK(seenTags == tagsSeen);
}

TEST(calendar_pull_events_a_handler_adds_run_at_the_same_tick_or_the_next_one)
{
    Scope scope;
    queueStub.now = 10;
    add(1, 1);
    onHandle = [](int tag) {
        if (tag == 1) {
            add(0, 2);
            add(1, 3);
        }
    };

    Rig rig;
    rig.arm();
    rig.ticks = 18;
    CHECK(rig.run().status == CalendarPullStatus::Done);

    const std::vector<int> times = { 11, 11, 12 };
    const std::vector<int> tagsSeen = { 1, 2, 3 };
    CHECK(seenTimes == times);
    CHECK(seenTags == tagsSeen);
}

TEST(calendar_pull_runs_events_already_due_when_no_new_tick_is_owed)
{
    Scope scope;
    queueStub.now = 100;
    add(1, 1);
    add(2, 2);
    add(500, 3);

    Rig rig;
    rig.arm();

    // Something else moved the game time (a rest, a script).
    queueStub.now = 300;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Done);
    CHECK_EQ(result.gameTicks, 0u);
    CHECK_EQ(result.events, 2u);
    CHECK_EQ(queueStub.now, 300);

    // Run at the time they were found, the later event stays queued.
    const std::vector<int> times = { 300, 300 };
    CHECK(seenTimes == times);
    int next = 0;
    CHECK(queue_peek_time(&next));
    CHECK_EQ(next, 600);
}

TEST(calendar_pull_a_nonzero_handler_result_keeps_the_unpaid_ticks_for_the_next_pass)
{
    Scope scope;
    queueStub.now = 0;
    results.assign(8, 0);
    results[1] = 1;
    add(3, 1);
    add(5, 2);

    Rig rig;
    rig.arm();
    rig.ticks = 60;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Interrupted);
    CHECK_EQ(result.gameTicks, 3u);
    CHECK_EQ(queueStub.now, 3);
    CHECK_EQ(rig.pull.pendingTicks(), 7u);

    // No new sim ticks. The next pass pays the rest.
    result = rig.run();
    CHECK(result.status == CalendarPullStatus::Done);
    CHECK_EQ(result.gameTicks, 7u);
    CHECK_EQ(queueStub.now, 10);
    const std::vector<int> times = { 3, 5 };
    CHECK(seenTimes == times);
}

TEST(calendar_pull_the_event_budget_covers_the_whole_pass_and_leaves_the_rest_queued)
{
    Scope scope;
    queueStub.now = 0;

    // 20 events due at each of the next ten ticks.
    for (int tick = 1; tick <= 10; tick++) {
        for (int i = 0; i < 20; i++) {
            add(tick, 1);
        }
    }

    CalendarPullConfig config;
    config.maxEventsPerPass = 50;
    Rig rig(config);
    rig.arm();
    rig.ticks = 60;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::EventBudget);
    CHECK_EQ(result.events, 50u);
    CHECK_EQ(seenTags.size(), 50u);

    // 50 events fit in two full ticks and half of the third.
    CHECK_EQ(queueStub.now, 3);
    CHECK(rig.pull.pendingTicks() > 0u);

    // The budget is per pass: the next pass goes on, and everything is paid.
    int passes = 1;
    while (rig.run().status == CalendarPullStatus::EventBudget && passes < 20) {
        passes++;
    }
    CHECK_EQ(seenTags.size(), 200u);
    CHECK_EQ(queueStub.now, 10);
}

TEST(calendar_pull_events_that_keep_scheduling_for_now_end_at_the_budget_without_a_hang)
{
    Scope scope;
    queueStub.now = 0;
    add(1, 1);
    onHandle = [](int) { add(0, 1); };

    CalendarPullConfig config;
    config.maxEventsPerPass = 100;
    Rig rig(config);
    rig.arm();
    rig.ticks = 6;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::EventBudget);
    CHECK_EQ(result.events, 100u);
    CHECK_EQ(queueStub.now, 1);
}

TEST(calendar_pull_a_deferred_request_keeps_the_ticks_and_writes_nothing)
{
    Scope scope;
    queueStub.now = 50;
    add(1, 1);
    add(0, 2);

    Rig rig;
    rig.arm();
    rig.ticks = 24;
    rig.deferred = true;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Deferred);
    CHECK_EQ(result.gameTicks, 0u);
    CHECK_EQ(result.events, 0u);
    CHECK_EQ(queueStub.now, 50);
    CHECK_EQ(rig.pull.pendingTicks(), 4u);

    // Cleared: the kept ticks are paid, and the drain runs the due event.
    rig.deferred = false;
    result = rig.run();
    CHECK(result.status == CalendarPullStatus::Done);
    CHECK_EQ(result.gameTicks, 4u);
    CHECK_EQ(queueStub.now, 54);
}

TEST(calendar_pull_a_request_raised_by_a_handler_stops_before_the_next_time_write)
{
    Scope scope;
    queueStub.now = 0;
    add(2, 1);

    Rig rig;
    rig.arm();
    rig.ticks = 60;
    onHandle = [&rig](int) { rig.deferred = true; };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Deferred);
    CHECK_EQ(queueStub.now, 2);
    CHECK_EQ(result.gameTicks, 2u);
    CHECK_EQ(rig.pull.pendingTicks(), 8u);
}

TEST(calendar_pull_a_deferred_backlog_is_capped_and_the_excess_counted)
{
    Scope scope;
    queueStub.now = 0;
    Rig rig;
    rig.arm();
    rig.deferred = true;

    // Ten seconds of sim ticks pile up while deferred.
    rig.ticks = 600;
    rig.run();
    CHECK_EQ(rig.pull.pendingTicks(), 30u);
    CHECK_EQ(rig.pull.droppedTicks(), 70u);

    // And they do not grow past the cap on later pulls.
    rig.ticks = 1200;
    rig.run();
    CHECK_EQ(rig.pull.pendingTicks(), 30u);
}

TEST(calendar_pull_quit_drops_the_ticks_and_the_next_pull_arms_again)
{
    Scope scope;
    queueStub.now = 0;
    add(2, 1);

    Rig rig;
    rig.arm();
    rig.ticks = 60;
    onHandle = [&rig](int) { rig.quit = true; };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Quit);
    CHECK_EQ(queueStub.now, 2);
    CHECK_EQ(rig.pull.pendingTicks(), 0u);
    CHECK(!rig.pull.armed());

    rig.quit = false;
    CHECK(rig.run().status == CalendarPullStatus::Rearmed);
    CHECK_EQ(queueStub.now, 2);
}

TEST(calendar_pull_a_lifecycle_change_in_a_handler_stops_before_another_write)
{
    Scope scope;
    queueStub.now = 0;
    add(3, 1);

    Rig rig;
    rig.arm();
    rig.ticks = 60;
    onHandle = [&rig](int) { rig.epoch++; };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::EpochChanged);
    CHECK_EQ(queueStub.now, 3);
    CHECK_EQ(rig.pull.pendingTicks(), 0u);
    CHECK(!rig.pull.armed());
}

TEST(calendar_pull_a_new_lifecycle_with_the_same_tick_count_pays_nothing_for_the_old_one)
{
    Scope scope;
    queueStub.now = 0;

    Rig rig;
    rig.arm();
    rig.ticks = 18;
    CHECK(rig.run().status == CalendarPullStatus::Done);
    CHECK_EQ(queueStub.now, 3);

    // A reset: the epoch changes, and the tick count is exactly what it was.
    rig.epoch++;
    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Rearmed);
    CHECK_EQ(result.gameTicks, 0u);
    CHECK_EQ(queueStub.now, 3);

    // Progress starts from the new arm.
    rig.ticks = 18 + 12;
    CHECK(rig.run().status == CalendarPullStatus::Done);
    CHECK_EQ(queueStub.now, 5);
}

TEST(calendar_pull_a_lifecycle_change_with_the_count_reused_after_a_restart)
{
    Scope scope;
    queueStub.now = 0;

    Rig rig;
    rig.ticks = 500;
    rig.arm();
    rig.ticks = 530;
    rig.run();
    CHECK_EQ(queueStub.now, 5);

    // The service restarted: the count starts again at zero and reaches a value
    // seen before. The new epoch keeps that from counting.
    rig.epoch++;
    rig.ticks = 0;
    CHECK(rig.run().status == CalendarPullStatus::Rearmed);
    rig.ticks = 500;
    CalendarPullResult result = rig.run();
    CHECK_EQ(queueStub.now, 5 + 30);
    CHECK_EQ(result.gameTicks, 30u);
}

TEST(calendar_pull_a_pull_from_inside_a_handler_changes_nothing)
{
    Scope scope;
    queueStub.now = 0;
    add(2, 1);

    Rig rig;
    rig.arm();
    rig.ticks = 30;

    CalendarPullResult inner;
    int innerTime = -1;
    onHandle = [&](int) {
        rig.ticks += 600;
        inner = rig.run();
        innerTime = queueStub.now;
    };

    CalendarPullResult outer = rig.run();
    CHECK(inner.status == CalendarPullStatus::Reentered);
    CHECK_EQ(inner.gameTicks, 0u);
    CHECK_EQ(innerTime, 2);

    // The outer pass paid its own five ticks (and the burst the handler added is
    // owed to the next pull, within the cap).
    CHECK(outer.status == CalendarPullStatus::Done);
    CHECK_EQ(outer.gameTicks, 5u);
    CHECK_EQ(queueStub.now, 5);

    // The guard is released: the next pull works.
    CHECK(rig.run().status == CalendarPullStatus::Done);
    CHECK_EQ(queueStub.now, 5 + 30);
}

TEST(calendar_pull_a_handler_that_moves_the_game_time_forward_is_followed)
{
    Scope scope;
    queueStub.now = 100;
    add(2, 1);
    add(60, 2);

    Rig rig;
    rig.arm();
    rig.ticks = 60;

    // The script jumps the game time by 100 inside the first event.
    onHandle = [](int tag) {
        if (tag == 1) {
            queueStub.now += 100;
        }
    };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Done);

    // Event 2 was due at 160, inside the jump: it ran in this pass, at the time
    // the drain found. The remaining ticks went on from the new time.
    CHECK_EQ(seenTags.size(), 2u);
    CHECK_EQ(seenTimes[0], 102);
    CHECK_EQ(seenTimes[1], 202);
    CHECK_EQ(queueStub.now, 202 + 8);
    CHECK_EQ(result.gameTicks, 10u);
}

TEST(calendar_pull_stops_at_the_time_limit_and_never_writes_past_it)
{
    Scope scope;
    queueStub.now = INT_MAX - 3;
    add(2, 1);
    add(3, 2);

    Rig rig;
    rig.arm();
    rig.ticks = 60;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::TimeLimit);
    CHECK_EQ(result.gameTicks, 3u);
    CHECK_EQ(queueStub.now, INT_MAX);
    CHECK_EQ(rig.pull.pendingTicks(), 0u);

    // The events at and before the limit ran.
    CHECK_EQ(seenTags.size(), 2u);

    // Later pulls cannot move it either.
    rig.ticks = 120;
    CHECK(rig.run().status == CalendarPullStatus::TimeLimit);
    CHECK_EQ(queueStub.now, INT_MAX);
}

TEST(calendar_pull_does_not_write_or_run_anything_at_a_negative_game_time)
{
    Scope scope;
    queueStub.now = 5;
    add(1, 1);

    Rig rig;
    rig.arm();
    rig.ticks = 60;

    queueStub.now = -4;
    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::InvalidTime);
    CHECK_EQ(queueStub.now, -4);
    CHECK_EQ(result.events, 0u);
    CHECK_EQ(result.gameTicks, 0u);
    CHECK_EQ(rig.pull.pendingTicks(), 0u);
    CHECK_EQ(seenTags.size(), 0u);
}

TEST(calendar_pull_a_game_time_that_turns_negative_in_a_handler_stops_the_pass)
{
    Scope scope;
    queueStub.now = 0;
    add(1, 1);

    Rig rig;
    rig.arm();
    rig.ticks = 60;
    onHandle = [](int) { queueStub.now = -1; };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::InvalidTime);
    CHECK_EQ(queueStub.now, -1);
    CHECK_EQ(result.gameTicks, 1u);
}

TEST(calendar_pull_a_handler_that_makes_the_time_negative_stops_the_drain_with_nothing_owed)
{
    Scope scope;
    queueStub.now = 3;
    add(-10, 1);
    add(-10, 2);

    Rig rig;
    rig.arm();
    onHandle = [](int value) {
        if (value == 1) {
            queueStub.now = -1;
        }
    };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::InvalidTime);
    CHECK_EQ(result.events, 1u);
    const std::vector<int> tagsSeen = { 1 };
    CHECK(seenTags == tagsSeen);
    CHECK_EQ(queueStub.now, -1);
    CHECK_EQ(rig.pull.pendingTicks(), 0u);

    int deadline = 0;
    CHECK(queue_peek_time(&deadline));
}

TEST(calendar_pull_an_invalid_time_does_not_burn_the_event_budget_on_later_negative_deadlines)
{
    Scope scope;
    queueStub.now = 3;
    add(-10, 1);
    add(-5, 2);

    Rig rig;
    rig.arm();
    onHandle = [](int value) {
        if (value == 1) {
            queueStub.now = -5;
        }
    };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::InvalidTime);
    CHECK_EQ(result.events, 1u);
    const std::vector<int> tagsSeen = { 1 };
    CHECK(seenTags == tagsSeen);

    int deadline = 0;
    CHECK(queue_peek_time(&deadline));
}

TEST(calendar_pull_a_handler_that_makes_the_time_negative_drops_owed_ticks)
{
    Scope scope;
    queueStub.now = 3;
    add(-10, 1);
    add(-10, 2);

    Rig rig;
    rig.arm();
    rig.ticks += 30;
    onHandle = [](int value) {
        if (value == 1) {
            queueStub.now = -1;
        }
    };

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::InvalidTime);
    CHECK_EQ(result.events, 1u);
    CHECK_EQ(result.gameTicks, 0u);
    CHECK_EQ(rig.pull.pendingTicks(), 0u);
    const std::vector<int> tagsSeen = { 1 };
    CHECK(seenTags == tagsSeen);
}

TEST(calendar_pull_an_overdue_negative_deadline_runs_like_the_engine_does)
{
    Scope scope;

    // Game time 3, an event 10 before it: a negative deadline, due at once.
    queueStub.now = 3;
    add(-10, 1);
    add(1, 2);

    int deadline = 0;
    CHECK(queue_peek_time(&deadline));
    CHECK_EQ(deadline, -7);

    Rig rig;
    rig.arm();
    rig.ticks = 6;

    CalendarPullResult result = rig.run();
    CHECK(result.status == CalendarPullStatus::Done);

    // The overdue event runs first, in the drain, at the time where it was
    // found. The other runs at its own tick.
    const std::vector<int> tagsSeen = { 1, 2 };
    const std::vector<int> times = { 3, 4 };
    CHECK(seenTags == tagsSeen);
    CHECK(seenTimes == times);
}

TEST(game_calendar_queue_refuses_writes_the_engine_cannot_hold)
{
    Scope scope;
    queueStub.now = 77;
    GameCalendarQueue queue;

    CHECK(!queue.trySetGameTime(0));
    CHECK(!queue.trySetGameTime(static_cast<uint32_t>(INT_MAX) + 1u));
    CHECK(!queue.trySetGameTime(0xFFFFFFFFu));
    CHECK_EQ(queueStub.now, 77);

    queue.setGameTime(0x80000000u);
    CHECK_EQ(queueStub.now, 77);

    CHECK(queue.trySetGameTime(INT_MAX));
    CHECK_EQ(queueStub.now, INT_MAX);
    CHECK(queue.timeValid());

    queueStub.now = -1;
    CHECK(!queue.timeValid());
    CHECK_EQ(queue.gameTime(), 0u);
}

TEST(game_calendar_queue_steps_exactly_one_event_and_reports_emptiness)
{
    Scope scope;
    queueStub.now = 10;
    GameCalendarQueue queue;

    uint32_t next = 99;
    CHECK(!queue.nextEventTime(&next));
    CHECK_EQ(next, 99u);

    add(0, 1);
    add(0, 2);
    CHECK(queue.nextEventTime(&next));
    CHECK_EQ(next, 10u);

    CHECK_EQ(queue.processNext(), 0);
    CHECK_EQ(seenTags.size(), 1u);
    CHECK(queue.nextEventTime(&next));
}

TEST(calendar_pull_a_pause_gives_no_game_ticks_and_the_partial_progress_survives)
{
    Scope scope;
    queueStub.now = 0;
    SimClock clock;
    PauseSet pauses;

    Rig rig;
    uint32_t now = 0;
    auto frame = [&](uint32_t ms) {
        now += ms;
        clock.advance(now, &pauses, [&rig] { rig.ticks++; });
        rig.run();
    };

    // The first frame arms the coordinator.
    frame(0);
    CHECK(rig.pull.armed());

    // 100 ms is 6 sim ticks, one game tick. Run 50 ms first: 3 sim ticks.
    frame(50);
    CHECK_EQ(queueStub.now, 0);

    {
        PauseToken token = pauses.acquire();
        for (int i = 0; i < 600; i++) {
            frame(16);
        }
    }
    frame(16);
    CHECK_EQ(queueStub.now, 0);

    // The 3 sim ticks from before the pause and 50 more ms make one game tick.
    frame(50);
    CHECK(queueStub.now >= 1);
    CHECK(queueStub.now <= 2);
}

TEST(calendar_pull_gives_exactly_600_game_ticks_in_60_seconds_at_17_30_and_144_hz)
{
    const uint32_t rates[] = { 17, 30, 144 };
    for (uint32_t hz : rates) {
        Scope scope;
        queueStub.now = 1000;
        SimClock clock;
        PauseSet pauses;
        Rig rig;
        uint32_t now = 0;

        clock.advance(now, &pauses, [&rig] { rig.ticks++; });
        rig.run();

        uint32_t frames = 60 * hz;
        for (uint32_t k = 1; k <= frames; k++) {
            now = static_cast<uint32_t>(static_cast<uint64_t>(k) * 1000 / hz);
            clock.advance(now, &pauses, [&rig] { rig.ticks++; });
            rig.run();
        }

        CHECK_EQ(now, 60000u);
        CHECK_EQ(rig.ticks, 3600u);
        CHECK_EQ(queueStub.now, 1000 + 600);
    }
}

TEST(calendar_pull_gives_exactly_600_game_ticks_in_60_seconds_with_jittery_frames)
{
    Scope scope;
    queueStub.now = 0;
    SimClock clock;
    PauseSet pauses;
    Rig rig;
    uint32_t now = 0;

    clock.advance(now, &pauses, [&rig] { rig.ticks++; });
    rig.run();

    uint32_t seed = 987654321;
    uint32_t done = 0;
    while (done < 60000) {
        seed = seed * 1103515245u + 12345u;
        uint32_t ms = 5 + (seed >> 16) % 36;
        if (done + ms > 60000) {
            ms = 60000 - done;
        }
        done += ms;
        now += ms;
        clock.advance(now, &pauses, [&rig] { rig.ticks++; });
        rig.run();
    }

    CHECK_EQ(rig.ticks, 3600u);
    CHECK_EQ(queueStub.now, 600);
}

TEST(calendar_pull_last_status_follows_each_pull_and_ignores_a_reentered_one)
{
    Scope scope;
    queueStub.now = 0;
    add(2, 1);

    Rig rig;
    CHECK(rig.pull.lastStatus() == CalendarPullStatus::Done);

    rig.arm();
    CHECK(rig.pull.lastStatus() == CalendarPullStatus::Rearmed);

    // Nothing owed and nothing due.
    rig.ticks = 0;
    CHECK(rig.run().status == CalendarPullStatus::Done);
    CHECK(rig.pull.lastStatus() == CalendarPullStatus::Done);

    // A pull from inside a handler is Reentered and leaves the stored status
    // alone, both while the outer pass runs and after it.
    rig.ticks = 30;
    CalendarPullStatus insideStatus = CalendarPullStatus::Quit;
    CalendarPullStatus innerResult = CalendarPullStatus::Quit;
    onHandle = [&](int) {
        innerResult = rig.run().status;
        insideStatus = rig.pull.lastStatus();
    };
    CHECK(rig.run().status == CalendarPullStatus::Done);
    CHECK(innerResult == CalendarPullStatus::Reentered);
    CHECK(insideStatus == CalendarPullStatus::Done);
    CHECK(rig.pull.lastStatus() == CalendarPullStatus::Done);

    // A non-Done outcome is stored too.
    rig.quit = true;
    rig.ticks += 60;
    CHECK(rig.run().status == CalendarPullStatus::Quit);
    CHECK(rig.pull.lastStatus() == CalendarPullStatus::Quit);
}

TEST(calendar_pull_last_status_is_rearmed_after_a_lifecycle_change)
{
    Scope scope;
    queueStub.now = 0;

    Rig rig;
    rig.arm();
    rig.epoch++;
    CHECK(rig.run().status == CalendarPullStatus::Rearmed);
    CHECK(rig.pull.lastStatus() == CalendarPullStatus::Rearmed);
}

TEST(calendar_pull_status_names_are_short_and_distinct)
{
    const CalendarPullStatus all[] = {
        CalendarPullStatus::Done,
        CalendarPullStatus::Reentered,
        CalendarPullStatus::Rearmed,
        CalendarPullStatus::Interrupted,
        CalendarPullStatus::EventBudget,
        CalendarPullStatus::Deferred,
        CalendarPullStatus::Quit,
        CalendarPullStatus::EpochChanged,
        CalendarPullStatus::TimeLimit,
        CalendarPullStatus::InvalidTime,
    };
    const size_t count = sizeof(all) / sizeof(all[0]);

    for (size_t i = 0; i < count; i++) {
        const char* name = calendar_pull_status_name(all[i]);
        CHECK(name != nullptr && name[0] != '\0');
        CHECK(strcmp(name, "unknown") != 0);

        for (size_t j = i + 1; j < count; j++) {
            CHECK(strcmp(name, calendar_pull_status_name(all[j])) != 0);
        }
    }

    CHECK_EQ(std::string(calendar_pull_status_name(CalendarPullStatus::EventBudget)), std::string("event_budget"));
    CHECK_EQ(std::string(calendar_pull_status_name(CalendarPullStatus::InvalidTime)), std::string("invalid_time"));
    CHECK_EQ(std::string(calendar_pull_status_name(static_cast<CalendarPullStatus>(99))), std::string("unknown"));
}

TEST(calendar_pull_status_log_a_reentered_pull_does_not_reset_the_gate)
{
    Scope scope;
    queueStub.now = 0;
    add(1, 1);

    // Each run reschedules itself for now and pulls again. The nested pull is
    // Reentered, and the outer pass ends in EventBudget.
    std::vector<CalendarPullStatus> statuses;
    CalendarPullConfig config;
    config.maxEventsPerPass = 5;
    Rig rig(config);
    rig.arm();
    rig.ticks = 6;

    onHandle = [&](int) {
        add(0, 1);
        statuses.push_back(rig.run().status);
    };

    StatusLogGate gate;
    int logged = 0;
    int reentered = 0;
    int budget = 0;
    for (int pass = 0; pass < 2; pass++) {
        size_t before = statuses.size();
        statuses.push_back(rig.run().status);
        // The outer result was pushed after the nested ones of its pass.
        for (size_t i = before; i < statuses.size(); i++) {
            reentered += statuses[i] == CalendarPullStatus::Reentered;
            budget += statuses[i] == CalendarPullStatus::EventBudget;
            if (calendar_pull_status_should_log(gate, statuses[i], 100, nullptr)) {
                logged++;
            }
        }
    }

    CHECK(reentered >= 4);
    CHECK_EQ(budget, 2);
    CHECK_EQ(logged, 1);
}

TEST(calendar_pull_status_log_a_done_pass_clears_the_gate)
{
    StatusLogGate gate;
    uint32_t suppressed = 99;
    CHECK(calendar_pull_status_should_log(gate, CalendarPullStatus::EventBudget, 100, &suppressed));
    CHECK_EQ(suppressed, 0u);
    CHECK(!calendar_pull_status_should_log(gate, CalendarPullStatus::EventBudget, 100, &suppressed));

    CHECK(!calendar_pull_status_should_log(gate, CalendarPullStatus::Done, 100, &suppressed));
    CHECK(calendar_pull_status_should_log(gate, CalendarPullStatus::EventBudget, 100, &suppressed));
}

TEST(calendar_pull_status_log_reentered_between_two_budgets_does_not_make_the_second_log)
{
    StatusLogGate gate;
    uint32_t suppressed = 0;
    CHECK(calendar_pull_status_should_log(gate, CalendarPullStatus::EventBudget, 100, &suppressed));
    CHECK(!calendar_pull_status_should_log(gate, CalendarPullStatus::Reentered, 100, &suppressed));
    CHECK(!calendar_pull_status_should_log(gate, CalendarPullStatus::EventBudget, 100, &suppressed));

    // The held-back one is counted for the next line.
    CHECK(calendar_pull_status_should_log(gate, CalendarPullStatus::EventBudget, 100 + 600, &suppressed));
    CHECK_EQ(suppressed, 1u);
}

TEST(calendar_pull_status_log_a_different_bad_status_logs_at_once)
{
    StatusLogGate gate;
    uint32_t suppressed = 0;
    CHECK(calendar_pull_status_should_log(gate, CalendarPullStatus::EventBudget, 100, &suppressed));
    CHECK(calendar_pull_status_should_log(gate, CalendarPullStatus::TimeLimit, 101, &suppressed));
    CHECK(calendar_pull_status_should_log(gate, CalendarPullStatus::InvalidTime, 102, &suppressed));
    CHECK(!calendar_pull_status_should_log(gate, CalendarPullStatus::InvalidTime, 103, &suppressed));

    // Statuses that are not bad never log.
    CHECK(!calendar_pull_status_should_log(gate, CalendarPullStatus::Quit, 104, &suppressed));
    CHECK(!calendar_pull_status_should_log(gate, CalendarPullStatus::Interrupted, 104, &suppressed));
}
