#include "sim/timed_events.h"

#include <limits.h>

namespace fallout {

namespace {

    const uint32_t kSecondInterval = 100;
    const uint32_t kLightInterval = 30000;

    // Same rule as elapsed_tocks(): a start in the future counts as a very long
    // time.
    uint32_t elapsed(uint32_t now, uint32_t start)
    {
        if (start > now) {
            return INT_MAX;
        }

        return now - start;
    }

} // namespace

bool TimedEventClock::secondTickDue(uint32_t now)
{
    if (elapsed(now, lastSecond_) >= kSecondInterval) {
        lastSecond_ = now;
        return true;
    }

    return false;
}

bool TimedEventClock::lightUpdateDue(uint32_t now)
{
    if (elapsed(now, lastLight_) >= kLightInterval) {
        lastLight_ = now;
        return true;
    }

    return false;
}

void TimedEventClock::rebase(uint32_t now)
{
    lastSecond_ = now;
    lastLight_ = now;
}

} // namespace fallout
