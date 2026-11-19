#ifndef FALLOUT_SIM_CALENDAR_PULL_H_
#define FALLOUT_SIM_CALENDAR_PULL_H_

#include <stdint.h>

#include <functional>

#include "sim/campaign_advance.h"
#include "sim/sim_log.h"
#include "sim/tick_accumulator.h"

namespace fallout {

// What the calendar needs from the game on top of CampaignQueue. The engine's
// game time is a signed number and only the non-negative part is usable.
class CalendarQueue : public CampaignQueue {
public:
    // `false` when the game time cannot be used (it is negative). gameTime() and
    // nextEventTime() mean nothing then, and nothing may be written.
    virtual bool timeValid() const = 0;

    // Writes the game time. Returns false, and writes nothing, if the engine
    // cannot hold `time`.
    virtual bool trySetGameTime(uint32_t time) = 0;
};

struct CalendarPullConfig {
    // Simulation ticks in one game tick: 60 per second against 10 game ticks per
    // second.
    uint32_t simTicksPerGameTick = 6;

    // Most unpaid game ticks one pass can owe. Anything above is dropped and
    // counted. It bounds what builds up while nobody pulls.
    uint32_t maxCatchUpTicks = 30;

    // Events one pass may run, over the first drain and every step together.
    uint32_t maxEventsPerPass = 256;

    // The game time never goes past this. The engine's time is a signed int.
    uint32_t timeLimit = 0x7FFFFFFF;
};

struct CalendarPullEnv {
    // Identity of the current lifecycle (world_sim_epoch()). Required.
    std::function<uint64_t()> epoch;

    // The simulation tick count (world_sim_tick_count()). Required.
    std::function<uint64_t()> simTicks;

    // Terminal: the game is ending. Unpaid ticks are dropped. Optional.
    std::function<bool()> quit;

    // Transient: something has to be handled before the calendar goes on, such
    // as a script request or a map transition, or scripts are off. Unpaid ticks
    // are kept, within the cap. Optional.
    std::function<bool()> deferred;
};

enum class CalendarPullStatus {
    // Every owed tick was paid and nothing is due.
    Done,

    // A pass was already running. Nothing was changed.
    Reentered,

    // First pull, or the lifecycle changed since the last one. Nothing was paid
    // and nothing was run.
    Rearmed,

    // A handler returned nonzero. The unpaid ticks are kept.
    Interrupted,

    // The event budget ran out. The unpaid ticks and the due events are kept.
    EventBudget,

    // The deferred request was set. Nothing more was written. The unpaid ticks
    // are kept.
    Deferred,

    // The quit request was set. The unpaid ticks are dropped.
    Quit,

    // The lifecycle changed during the pass. The unpaid ticks are dropped.
    EpochChanged,

    // The next tick would pass the limit. The unpaid ticks are dropped.
    TimeLimit,

    // The game time is not usable, or the engine refused a write. Nothing was
    // written, the unpaid ticks are dropped.
    InvalidTime,
};

struct CalendarPullResult {
    CalendarPullStatus status = CalendarPullStatus::Done;

    // Game ticks written in this pass.
    uint32_t gameTicks = 0;

    uint32_t events = 0;

    // Ticks over the cap that were dropped at the start of this pass.
    uint32_t droppedTicks = 0;
};

// Moves the game calendar forward by pulling from the simulation tick count, one
// game tick at a time. Each tick sets the game time and then runs the events
// that are due at it, as the legacy second tick does. Already-due events are run
// first on every pass, also when no tick is owed.
//
// Nothing here runs from a tick callback: the owner calls pull() from the main
// loop. A handler may call pull() again (through a nested loop); that returns
// Reentered and changes nothing.
class CalendarPull {
public:
    CalendarPull();
    explicit CalendarPull(const CalendarPullConfig& config);

    CalendarPullResult pull(CalendarQueue& queue, const CalendarPullEnv& env);

    // Forgets everything. The next pull() only arms.
    void disarm();

    bool armed() const { return armed_; }
    uint32_t pendingTicks() const { return pending_; }
    uint64_t droppedTicks() const { return dropped_; }

    // The status of the last pull() that was not Reentered. Done before any pull.
    CalendarPullStatus lastStatus() const { return lastStatus_; }

private:
    CalendarPullResult pass(CalendarQueue& queue, const CalendarPullEnv& env);
    void rearm(const CalendarPullEnv& env, uint64_t epoch);

    CalendarPullConfig config_;
    TickAccumulator accumulator_;
    bool armed_;
    bool inPass_;
    uint64_t epoch_;
    uint32_t pending_;
    uint64_t dropped_;
    CalendarPullStatus lastStatus_;
};

// A short lowercase name for diagnostics, "unknown" for a value out of range.
const char* calendar_pull_status_name(CalendarPullStatus status);

// Decides whether the status of one pull earns a diagnostic line. EventBudget,
// TimeLimit and InvalidTime go through the gate (`suppressed` is filled when it
// returns true). Reentered returns false and leaves the gate alone: a nested
// pull says nothing about the outer one. Any other status clears the gate and
// returns false, so the next bad status logs at once.
bool calendar_pull_status_should_log(StatusLogGate& gate, CalendarPullStatus status, uint64_t nowTicks, uint32_t* suppressed);

} // namespace fallout

#endif /* FALLOUT_SIM_CALENDAR_PULL_H_ */
