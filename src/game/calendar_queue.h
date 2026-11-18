#ifndef FALLOUT_GAME_CALENDAR_QUEUE_H_
#define FALLOUT_GAME_CALENDAR_QUEUE_H_

#include "sim/calendar_pull.h"

namespace fallout {

// The game's event queue and game time, seen by the calendar. One event is run
// per processNext() call.
//
// The engine's time is a signed int. A negative game time is "invalid": the
// calendar does not write it or run events at it. An event with a negative
// deadline is overdue at every valid time (the engine compares signed values),
// and nextEventTime() reports it as 0, which has the same effect.
class GameCalendarQueue : public CalendarQueue {
public:
    // 0 when the game time is not valid. Check timeValid() first.
    uint32_t gameTime() const override;

    bool timeValid() const override;

    // Refuses 0 (the engine would store 1) and anything above INT_MAX.
    bool trySetGameTime(uint32_t time) override;

    // Same, ignoring the refusal.
    void setGameTime(uint32_t time) override;

    bool nextEventTime(uint32_t* time) const override;

    // Runs the earliest event if it is due at the game time. The caller has
    // checked that one is.
    int processNext() override;
};

} // namespace fallout

#endif /* FALLOUT_GAME_CALENDAR_QUEUE_H_ */
