#ifndef FALLOUT_SIM_SIM_CLOCK_H_
#define FALLOUT_SIM_SIM_CLOCK_H_

#include <stdint.h>

#include <functional>

#include "sim/pause_set.h"

namespace fallout {

// Fixed-rate simulation clock. It turns frame-to-frame time deltas into a whole
// number of simulation ticks. Time is counted in integer units (milliseconds by
// default) and the tick rate is a ratio, so no rounding error builds up: after
// N seconds of input exactly N * tickRate ticks have run, whatever the frame
// rate or jitter.
class SimClock {
public:
    struct Config {
        // Simulation ticks per second.
        uint32_t tickRate = 60;

        // Input time units per second. 1000 for milliseconds.
        uint32_t unitsPerSecond = 1000;

        // A single frame delta above this is cut to it. The cut part is counted
        // in droppedUnits() and never replayed. Zero turns the limit off.
        uint32_t maxFrameDelta = 250;
    };

    // A zero tickRate or unitsPerSecond is replaced with 1.
    SimClock();
    explicit SimClock(const Config& config);

    typedef std::function<void()> TickFunction;

    // Feeds the current time and runs the due ticks through `tick`. `now` is a
    // free-running counter that may wrap at 2^32. Returns the number of ticks
    // run.
    //
    // The first call only records the time.
    //
    // A pause discards the elapsed time and the remainder, so releasing it never
    // produces a catch-up burst. This holds when the pause began and ended
    // between two calls (it is seen through PauseSet::transitionCount()) and when it
    // began and ended inside a tick: the rest of that batch is dropped.
    //
    // A `now` that is behind the last time by less than half the 32-bit range
    // is a backward sample. It is ignored whole and counted in
    // backwardSamples(); the next normal sample measures from the last accepted
    // time. A backward step larger than that is indistinguishable from a wrap.
    //
    // A call made from inside `tick` does nothing and returns 0. The outer call
    // already holds the time, so the nested time is neither lost nor counted
    // twice.
    uint32_t advance(uint32_t now, const PauseSet* pauses, const TickFunction& tick);

    uint64_t tickCount() const { return tickCount_; }

    // Time units cut by maxFrameDelta since construction.
    uint64_t droppedUnits() const { return droppedUnits_; }

    // Backward samples ignored since construction.
    uint64_t backwardSamples() const { return backwardSamples_; }

    // Forgets the last time and the remainder. The next advance() only records
    // the time. Use it after a load or a long stall that must not count. A
    // handler may call it: the batch it runs in ends, the clock does not touch
    // the pause set again for that call (the handler may destroy it), and the
    // next call to advance() only records the time. Destroying the set inside a
    // tick without calling reset() is not allowed.
    void reset();

private:
    void syncPauses(const PauseSet* pauses);

    // `true` if a pause began since the last call. Takes a new baseline.
    bool pausesChanged(const PauseSet* pauses);

    Config config_;
    bool started_;
    bool running_;
    uint32_t last_;

    // Remainder in units * tickRate. One tick costs unitsPerSecond.
    uint64_t remainder_;
    uint64_t tickCount_;
    uint64_t droppedUnits_;
    uint64_t backwardSamples_;

    // The set last seen and its transition count then.
    const PauseSet* pauseSource_;
    uint64_t transitionCount_;
};

} // namespace fallout

#endif /* FALLOUT_SIM_SIM_CLOCK_H_ */
