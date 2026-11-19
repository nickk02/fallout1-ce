#ifndef FALLOUT_SIM_SIM_LOG_H_
#define FALLOUT_SIM_SIM_LOG_H_

#include <stddef.h>
#include <stdint.h>

namespace fallout {

// Line formatters for the opt-in simulation diagnostics. Each one behaves like
// snprintf: it writes at most size - 1 characters plus a NUL (nothing when size
// is 0) and returns the length the whole line would have had.

int sim_log_format_pause(char* out, size_t size, bool begin, uint64_t epoch, uint64_t ticks, uint32_t elapsedMs);

// `what` is "init", "reset", "invalidate" or "exit".
int sim_log_format_lifecycle(char* out, size_t size, const char* what, uint64_t epoch, uint64_t ticks, bool paused);

// `detail` may be NULL. It is appended as is and starts with a space.
int sim_log_format_summary(char* out, size_t size, int mode, uint64_t epoch, uint64_t ticks, uint64_t legacyCalendarTicks, const char* detail);

// Lets a repeating status through once, then again when it changes or after an
// interval, and counts what it held back.
class StatusLogGate {
public:
    explicit StatusLogGate(uint32_t intervalTicks = 600);

    // `suppressed`, if not null, receives the calls held back since the last one
    // that logged, when this call logs.
    bool shouldLog(int status, uint64_t nowTicks, uint32_t* suppressed);

    // Forgets everything. The next call logs.
    void clear();

private:
    uint32_t interval_;
    bool logged_;
    int lastStatus_;
    uint64_t lastTicks_;
    uint32_t suppressed_;
};

} // namespace fallout

#endif /* FALLOUT_SIM_SIM_LOG_H_ */
