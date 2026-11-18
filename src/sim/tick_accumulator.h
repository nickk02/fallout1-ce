#ifndef FALLOUT_SIM_TICK_ACCUMULATOR_H_
#define FALLOUT_SIM_TICK_ACCUMULATOR_H_

#include <stdint.h>

namespace fallout {

// Turns the running simulation tick count into whole periods, for a consumer
// that pulls from the main loop. It holds one mark, so reading it twice at the
// same tick gives nothing the second time, and no call can run game code.
//
// The count that is passed in must stop while the world is paused (the tick
// count of the simulation clock does). Progress toward the next period is kept
// across a pause because only the mark moves, and only by whole periods.
class TickAccumulator {
public:
    // A zero period is replaced with 1.
    explicit TickAccumulator(uint32_t period)
        : period_(period != 0 ? period : 1)
        , armed_(false)
        , mark_(0)
    {
    }

    // Forgets the mark and any progress. The next take() only arms the
    // accumulator and returns 0, so ticks that passed while nobody was reading
    // (a menu, a load, no actor) are never paid out.
    void disarm() { armed_ = false; }

    // Arms at `currentTicks` with no progress. For a boundary the consumer
    // marks itself, such as the start of a fight.
    void rebase(uint64_t currentTicks)
    {
        armed_ = true;
        mark_ = currentTicks;
    }

    bool armed() const { return armed_; }

    // The whole periods that passed since the mark. The mark moves forward by
    // exactly those, so the remainder stays for the next call.
    //
    // The first call after construction or disarm() arms and returns 0. A count
    // behind the mark means the counter was restarted; the accumulator takes the
    // new count as its mark and returns 0.
    uint64_t take(uint64_t currentTicks)
    {
        if (!armed_ || currentTicks < mark_) {
            rebase(currentTicks);
            return 0;
        }

        uint64_t periods = (currentTicks - mark_) / period_;
        mark_ += periods * period_;
        return periods;
    }

private:
    uint32_t period_;
    bool armed_;
    uint64_t mark_;
};

// How many action points `windows` whole periods give: one each, never past the
// maximum. Periods that find the points full are spent without effect.
inline int ap_regen_grant(uint64_t windows, int ap, int maxAp)
{
    if (windows == 0 || ap >= maxAp) {
        return 0;
    }

    uint64_t room = static_cast<uint64_t>(maxAp - ap);
    return static_cast<int>(windows < room ? windows : room);
}

} // namespace fallout

#endif /* FALLOUT_SIM_TICK_ACCUMULATOR_H_ */
