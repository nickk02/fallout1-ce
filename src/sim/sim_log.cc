#include "sim/sim_log.h"

#include <stdio.h>

namespace fallout {

namespace {

    unsigned long long wide(uint64_t value)
    {
        return static_cast<unsigned long long>(value);
    }

    // A failed snprintf leaves the buffer undefined. Make it an empty string.
    int finish(char* out, size_t size, int written)
    {
        if (size > 0 && written < 0) {
            out[0] = '\0';
        }
        return written;
    }

} // namespace

int sim_log_format_pause(char* out, size_t size, bool begin, uint64_t epoch, uint64_t ticks, uint32_t elapsedMs)
{
    int written;
    if (begin) {
        written = snprintf(out, size, "simlog pause begin epoch=%llu ticks=%llu", wide(epoch), wide(ticks));
    } else {
        written = snprintf(out, size, "simlog pause end epoch=%llu ticks=%llu elapsed_ms=%u", wide(epoch), wide(ticks), elapsedMs);
    }
    return finish(out, size, written);
}

int sim_log_format_lifecycle(char* out, size_t size, const char* what, uint64_t epoch, uint64_t ticks, bool paused)
{
    int written = snprintf(out, size, "simlog %s epoch=%llu ticks=%llu paused=%d",
        what != nullptr ? what : "", wide(epoch), wide(ticks), paused ? 1 : 0);
    return finish(out, size, written);
}

int sim_log_format_summary(char* out, size_t size, int mode, uint64_t epoch, uint64_t ticks, uint64_t legacyCalendarTicks, const char* detail)
{
    int written = snprintf(out, size, "simlog summary mode=%d epoch=%llu ticks=%llu legacy_calendar_ticks=%llu%s",
        mode, wide(epoch), wide(ticks), wide(legacyCalendarTicks), detail != nullptr ? detail : "");
    return finish(out, size, written);
}

StatusLogGate::StatusLogGate(uint32_t intervalTicks)
    : interval_(intervalTicks)
    , logged_(false)
    , lastStatus_(0)
    , lastTicks_(0)
    , suppressed_(0)
{
}

bool StatusLogGate::shouldLog(int status, uint64_t nowTicks, uint32_t* suppressed)
{
    bool log = !logged_
        || status != lastStatus_
        || nowTicks < lastTicks_
        || nowTicks - lastTicks_ >= interval_;

    if (!log) {
        if (suppressed_ < UINT32_MAX) {
            suppressed_++;
        }
        return false;
    }

    if (suppressed != nullptr) {
        *suppressed = suppressed_;
    }
    suppressed_ = 0;
    logged_ = true;
    lastStatus_ = status;
    lastTicks_ = nowTicks;
    return true;
}

void StatusLogGate::clear()
{
    logged_ = false;
    lastStatus_ = 0;
    lastTicks_ = 0;
    suppressed_ = 0;
}

} // namespace fallout
