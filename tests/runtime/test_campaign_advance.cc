#include "test_harness.h"

#include <algorithm>
#include <functional>
#include <vector>

#include "sim/campaign_advance.h"

using namespace fallout;

namespace {

// A queue that follows queue.cc: events sorted by time. processNext() runs the
// earliest event, as one step of queue_process().
class FakeQueue : public CampaignQueue {
public:
    typedef std::function<int(FakeQueue&)> Handler;

    struct Event {
        uint32_t time;
        Handler handler;
    };

    explicit FakeQueue(uint32_t start)
        : time_(start)
        , maxTime_(start)
        , processCalls_(0)
    {
    }

    void add(uint32_t time, Handler handler)
    {
        Event event = { time, handler };
        auto position = std::upper_bound(events_.begin(), events_.end(), time, [](uint32_t value, const Event& e) { return value < e.time; });
        events_.insert(position, event);
    }

    uint32_t gameTime() const override { return time_; }

    void setGameTime(uint32_t time) override
    {
        // The advance code must never move time back.
        CHECK(time >= time_);
        time_ = time;
        maxTime_ = std::max(maxTime_, time);
    }

    bool nextEventTime(uint32_t* time) const override
    {
        if (events_.empty()) {
            return false;
        }

        *time = events_.front().time;
        return true;
    }

    int processNext() override
    {
        processCalls_++;

        if (events_.empty()) {
            return 0;
        }

        Event event = events_.front();
        events_.erase(events_.begin());
        return event.handler(*this);
    }

    size_t pending() const { return events_.size(); }
    uint32_t maxTime() const { return maxTime_; }
    int processCalls() const { return processCalls_; }

private:
    uint32_t time_;
    uint32_t maxTime_;
    int processCalls_;
    std::vector<Event> events_;
};

} // namespace

TEST(campaign_empty_queue_jumps_to_the_target)
{
    FakeQueue queue(100);
    CampaignAdvanceResult result = advanceCampaignTime(queue, 5000);
    CHECK(result.status == CampaignAdvanceStatus::Reached);
    CHECK_EQ(queue.gameTime(), 5000u);
    CHECK_EQ(queue.processCalls(), 0);
}

TEST(campaign_empty_queue_is_not_an_event_at_time_zero)
{
    // queue_next_time() returns 0 for an empty queue. The interface must not
    // read that as an event, or time would loop or go backwards.
    FakeQueue queue(100);
    CampaignAdvanceResult result = advanceCampaignTime(queue, 200);
    CHECK_EQ(result.slices, 0u);
    CHECK_EQ(result.events, 0u);
    CHECK_EQ(queue.gameTime(), 200u);
}

TEST(campaign_event_at_time_zero_is_a_real_event)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(0, [&ran](FakeQueue&) { ran++; return 0; });
    advanceCampaignTime(queue, 10);
    CHECK_EQ(ran, 1);
    CHECK_EQ(queue.gameTime(), 10u);
}

TEST(campaign_target_at_or_before_now_changes_nothing)
{
    FakeQueue queue(500);
    int ran = 0;
    queue.add(400, [&ran](FakeQueue&) { ran++; return 0; });

    CHECK(advanceCampaignTime(queue, 500).status == CampaignAdvanceStatus::Reached);
    CHECK(advanceCampaignTime(queue, 100).status == CampaignAdvanceStatus::Reached);
    CHECK_EQ(queue.gameTime(), 500u);
    CHECK_EQ(ran, 0);
    CHECK_EQ(queue.processCalls(), 0);
}

TEST(campaign_event_on_the_way_runs_one_past_its_time)
{
    FakeQueue queue(0);
    uint32_t seenTime = 0;
    queue.add(100, [&seenTime](FakeQueue& q) { seenTime = q.gameTime(); return 0; });

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000);
    CHECK(result.status == CampaignAdvanceStatus::Reached);
    CHECK_EQ(seenTime, 101u);
    CHECK_EQ(queue.gameTime(), 1000u);
    CHECK_EQ(queue.pending(), 0u);
}

TEST(campaign_event_at_the_target_runs_and_time_does_not_pass_it)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(1000, [&ran](FakeQueue&) { ran++; return 0; });

    advanceCampaignTime(queue, 1000);
    CHECK_EQ(ran, 1);
    CHECK_EQ(queue.gameTime(), 1000u);
    CHECK_EQ(queue.maxTime(), 1000u);
}

TEST(campaign_event_after_the_target_stays_queued)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(1001, [&ran](FakeQueue&) { ran++; return 0; });

    advanceCampaignTime(queue, 1000);
    CHECK_EQ(ran, 0);
    CHECK_EQ(queue.pending(), 1u);
    CHECK_EQ(queue.gameTime(), 1000u);
}

TEST(campaign_event_at_the_current_time_runs_without_moving_time_back)
{
    FakeQueue queue(500);
    int ran = 0;
    uint32_t seenTime = 0;
    queue.add(500, [&](FakeQueue& q) { ran++; seenTime = q.gameTime(); return 0; });

    advanceCampaignTime(queue, 600);
    CHECK_EQ(ran, 1);
    CHECK_EQ(seenTime, 500u);
    CHECK_EQ(queue.gameTime(), 600u);
}

TEST(campaign_overdue_event_runs_at_the_current_time)
{
    FakeQueue queue(500);
    uint32_t seenTime = 0;
    queue.add(100, [&seenTime](FakeQueue& q) { seenTime = q.gameTime(); return 0; });

    advanceCampaignTime(queue, 600);
    CHECK_EQ(seenTime, 500u);
    CHECK_EQ(queue.gameTime(), 600u);
}

TEST(campaign_events_run_in_time_order)
{
    FakeQueue queue(0);
    std::vector<int> order;
    queue.add(300, [&order](FakeQueue&) { order.push_back(3); return 0; });
    queue.add(100, [&order](FakeQueue&) { order.push_back(1); return 0; });
    queue.add(200, [&order](FakeQueue&) { order.push_back(2); return 0; });

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000);
    CHECK_EQ(order.size(), 3u);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 2);
    CHECK_EQ(order[2], 3);
    CHECK_EQ(result.events, 3u);
}

TEST(campaign_handler_scheduling_same_time_work_is_processed)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(100, [&ran](FakeQueue& q) {
        ran++;
        q.add(q.gameTime(), [&ran](FakeQueue&) { ran++; return 0; });
        return 0;
    });

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000);
    CHECK(result.status == CampaignAdvanceStatus::Reached);
    CHECK_EQ(ran, 2);
    CHECK_EQ(queue.pending(), 0u);
}

TEST(campaign_handler_scheduling_later_work_inside_the_target_is_processed)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(100, [&ran](FakeQueue& q) {
        ran++;
        q.add(500, [&ran](FakeQueue&) { ran++; return 0; });
        return 0;
    });

    advanceCampaignTime(queue, 1000);
    CHECK_EQ(ran, 2);
}

TEST(campaign_handler_scheduling_work_past_the_target_leaves_it_queued)
{
    FakeQueue queue(0);
    queue.add(100, [](FakeQueue& q) {
        q.add(5000, [](FakeQueue&) { return 0; });
        return 0;
    });

    advanceCampaignTime(queue, 1000);
    CHECK_EQ(queue.pending(), 1u);
    CHECK_EQ(queue.gameTime(), 1000u);
}

TEST(campaign_interrupt_stops_at_the_event_time)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(100, [&ran](FakeQueue&) { ran++; return 1; });
    queue.add(200, [&ran](FakeQueue&) { ran++; return 0; });

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000);
    CHECK(result.status == CampaignAdvanceStatus::Interrupted);
    CHECK_EQ(ran, 1);
    CHECK_EQ(queue.gameTime(), 101u);
    CHECK_EQ(queue.pending(), 1u);
}

TEST(campaign_stop_request_is_checked_after_each_process)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(100, [&ran](FakeQueue&) { ran++; return 0; });
    queue.add(200, [&ran](FakeQueue&) { ran++; return 0; });

    CampaignAdvanceOptions options;
    options.shouldStop = [&ran]() { return ran >= 1; };

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000, options);
    CHECK(result.status == CampaignAdvanceStatus::Stopped);
    CHECK_EQ(ran, 1);
    CHECK_EQ(queue.gameTime(), 101u);
}

TEST(campaign_event_that_never_leaves_the_queue_is_bounded)
{
    // A queue whose processNext() does nothing leaves the event due forever.
    class StuckQueue : public FakeQueue {
    public:
        StuckQueue()
            : FakeQueue(0)
            , calls(0)
        {
        }
        int processNext() override
        {
            calls++;
            return 0;
        }
        int calls;
    };

    StuckQueue queue;
    queue.add(100, [](FakeQueue&) { return 0; });

    CampaignAdvanceOptions options;
    options.maxEvents = 5;

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000, options);
    CHECK(result.status == CampaignAdvanceStatus::NoProgress);
    CHECK_EQ(queue.calls, 5);
    CHECK_EQ(result.events, 5u);
    CHECK_EQ(queue.pending(), 1u);
}

TEST(campaign_same_time_rescheduling_chain_is_bounded)
{
    FakeQueue queue(0);
    int ran = 0;
    FakeQueue::Handler forever;
    forever = [&](FakeQueue& q) {
        ran++;
        q.add(q.gameTime(), forever);
        return 0;
    };
    queue.add(100, forever);

    CampaignAdvanceOptions options;
    options.maxEvents = 50;

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000, options);
    CHECK(result.status == CampaignAdvanceStatus::NoProgress);
    CHECK_EQ(ran, 50);
    CHECK_EQ(result.events, 50u);
    CHECK_EQ(queue.pending(), 1u);
    CHECK_EQ(queue.gameTime(), 101u);
}

TEST(campaign_stop_request_cuts_a_rescheduling_chain)
{
    FakeQueue queue(0);
    int ran = 0;
    FakeQueue::Handler forever;
    forever = [&](FakeQueue& q) {
        ran++;
        q.add(q.gameTime(), forever);
        return 0;
    };
    queue.add(100, forever);

    CampaignAdvanceOptions options;
    options.shouldStop = [&ran]() { return ran >= 7; };

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000, options);
    CHECK(result.status == CampaignAdvanceStatus::Stopped);
    CHECK_EQ(ran, 7);
}

TEST(campaign_stop_set_before_the_call_changes_nothing_on_an_empty_queue)
{
    FakeQueue queue(0);
    CampaignAdvanceOptions options;
    options.shouldStop = []() { return true; };

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000, options);
    CHECK(result.status == CampaignAdvanceStatus::Stopped);
    CHECK_EQ(queue.gameTime(), 0u);
    CHECK_EQ(queue.processCalls(), 0);
}

TEST(campaign_stop_set_before_the_call_runs_nothing_on_a_full_queue)
{
    FakeQueue queue(0);
    int ran = 0;
    queue.add(0, [&ran](FakeQueue&) { ran++; return 0; });
    queue.add(100, [&ran](FakeQueue&) { ran++; return 0; });

    CampaignAdvanceOptions options;
    options.shouldStop = []() { return true; };

    CampaignAdvanceResult result = advanceCampaignTime(queue, 1000, options);
    CHECK(result.status == CampaignAdvanceStatus::Stopped);
    CHECK_EQ(ran, 0);
    CHECK_EQ(queue.pending(), 2u);
    CHECK_EQ(queue.gameTime(), 0u);
}

TEST(campaign_event_limit_bounds_a_long_chain)
{
    FakeQueue queue(0);
    for (uint32_t i = 1; i <= 50; i++) {
        queue.add(i * 10, [](FakeQueue&) { return 0; });
    }

    CampaignAdvanceOptions options;
    options.maxEvents = 10;

    CampaignAdvanceResult result = advanceCampaignTime(queue, 100000, options);
    CHECK(result.status == CampaignAdvanceStatus::NoProgress);
    CHECK_EQ(result.events, 10u);
    CHECK_EQ(queue.pending(), 40u);
}

TEST(campaign_event_at_the_largest_time_does_not_overflow)
{
    FakeQueue queue(UINT32_MAX - 10);
    int ran = 0;
    queue.add(UINT32_MAX, [&ran](FakeQueue&) { ran++; return 0; });

    CampaignAdvanceResult result = advanceCampaignTime(queue, UINT32_MAX);
    CHECK(result.status == CampaignAdvanceStatus::Reached);
    CHECK_EQ(ran, 1);
    CHECK_EQ(queue.gameTime(), UINT32_MAX);
}

TEST(campaign_advance_by_adds_to_the_current_time)
{
    FakeQueue queue(1000);
    CampaignAdvanceResult result = advanceCampaignBy(queue, 600);
    CHECK(result.status == CampaignAdvanceStatus::Reached);
    CHECK_EQ(queue.gameTime(), 1600u);
}

TEST(campaign_advance_by_reports_overflow_and_changes_nothing)
{
    FakeQueue queue(UINT32_MAX - 5);
    int ran = 0;
    queue.add(UINT32_MAX - 4, [&ran](FakeQueue&) { ran++; return 0; });

    CampaignAdvanceResult result = advanceCampaignBy(queue, 6);
    CHECK(result.status == CampaignAdvanceStatus::TimeOverflow);
    CHECK_EQ(queue.gameTime(), UINT32_MAX - 5);
    CHECK_EQ(ran, 0);
    CHECK_EQ(queue.processCalls(), 0);

    // The largest legal step is still allowed.
    CHECK(advanceCampaignBy(queue, 5).status == CampaignAdvanceStatus::Reached);
    CHECK_EQ(queue.gameTime(), UINT32_MAX);
}
