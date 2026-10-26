#ifndef FALLOUT_SIM_CAMPAIGN_ADVANCE_H_
#define FALLOUT_SIM_CAMPAIGN_ADVANCE_H_

#include <stdint.h>

#include <functional>

namespace fallout {

// What advancing the campaign calendar needs from the event queue.
//
// The real queue (game/queue.cc) has no single-step entry: queue_process() runs
// every due event, and keeps running events that handlers schedule for the same
// time, in one call that nothing outside can interrupt or bound. An adapter that
// maps processNext() onto queue_process() would therefore lose the bounds
// below. Until the engine gains a single-event step, those bounds hold only for
// queues that implement processNext() for real. That change is not part of this
// module.
class CampaignQueue {
public:
    virtual ~CampaignQueue() = default;

    virtual uint32_t gameTime() const = 0;
    virtual void setGameTime(uint32_t time) = 0;

    // Stores the time of the earliest event in `time` and returns true. Returns
    // false when the queue is empty. queue_next_time() reports an empty queue as
    // 0, which is also a valid time, so an adapter must test for emptiness
    // itself.
    virtual bool nextEventTime(uint32_t* time) const = 0;

    // Removes and runs the earliest event. The caller has checked that it is
    // due at the current game time. A nonzero result asks the caller to stop
    // (queue_process() stops at the first nonzero handler result).
    virtual int processNext() = 0;
};

enum class CampaignAdvanceStatus {
    // The game time is at the target, or was already past it.
    Reached,

    // processNext() returned nonzero. The game time stays where the event ran.
    Interrupted,

    // CampaignAdvanceOptions::shouldStop returned true. It is checked before
    // anything is changed or run, so a request that is already set leaves the
    // queue and the game time as they were.
    Stopped,

    // More than maxEvents events were due. Handlers that keep scheduling work
    // for the same time end here. The remaining events stay queued.
    NoProgress,

    // advanceCampaignBy() would pass 2^32. Nothing was changed.
    TimeOverflow,
};

struct CampaignAdvanceOptions {
    // Limit on events run in one call.
    uint32_t maxEvents = 65536;

    // Checked before every change of the game time and every event.
    std::function<bool()> shouldStop;
};

struct CampaignAdvanceResult {
    CampaignAdvanceStatus status = CampaignAdvanceStatus::Reached;

    // Times the game time was moved to stop at an event.
    uint32_t slices = 0;

    uint32_t events = 0;
};

// Moves the game time forward to `target`, running the events on the way. The
// game time never moves back and never goes past `target`. A target at or
// before the current time changes nothing and runs nothing.
//
// An event due at the current time, or overdue, runs at the current time. For a
// later event the game time is first set to one past the event time (as the
// Pip-Boy rest loop does), limited to `target`. Events that handlers schedule
// for a time not after the target are run too. Each event is one processNext()
// call, so the limits and the stop request apply between events.
CampaignAdvanceResult advanceCampaignTime(CampaignQueue& queue, uint32_t target, const CampaignAdvanceOptions& options = CampaignAdvanceOptions());

// Same, for `delta` ticks from the current time.
CampaignAdvanceResult advanceCampaignBy(CampaignQueue& queue, uint32_t delta, const CampaignAdvanceOptions& options = CampaignAdvanceOptions());

} // namespace fallout

#endif /* FALLOUT_SIM_CAMPAIGN_ADVANCE_H_ */
