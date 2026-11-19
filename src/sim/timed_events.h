#ifndef FALLOUT_SIM_TIMED_EVENTS_H_
#define FALLOUT_SIM_TIMED_EVENTS_H_

#include <stdint.h>

namespace fallout {

// The two wall-clock intervals of the script engine's background pass: the
// calendar second (a tenth of a game second every 100 ms) and the map update
// scripts (every 30 s). `now` is the same millisecond counter for every call.
class TimedEventClock {
public:
    // `true` once 100 ms have passed since the last time it was true. Restarts
    // the interval.
    bool secondTickDue(uint32_t now);

    // `true` once 30 s have passed since the last time it was true.
    bool lightUpdateDue(uint32_t now);

    // Starts both intervals at `now`. Use the current reading of the clock, not
    // one saved by an earlier pass, or the intervals start early by the age of
    // that reading.
    void rebase(uint32_t now);

private:
    uint32_t lastSecond_ = 0;
    uint32_t lastLight_ = 0;
};

} // namespace fallout

#endif /* FALLOUT_SIM_TIMED_EVENTS_H_ */
