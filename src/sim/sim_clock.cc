#include "sim/sim_clock.h"

namespace fallout {

namespace {

    // Clears the re-entrancy flag on every exit, including a throw from a tick.
    class RunningGuard {
    public:
        explicit RunningGuard(bool& flag)
            : flag_(flag)
        {
            flag_ = true;
        }

        ~RunningGuard() { flag_ = false; }

        RunningGuard(const RunningGuard&) = delete;
        RunningGuard& operator=(const RunningGuard&) = delete;

    private:
        bool& flag_;
    };

} // namespace

SimClock::SimClock()
    : SimClock(Config())
{
}

SimClock::SimClock(const Config& config)
    : config_(config)
    , started_(false)
    , running_(false)
    , last_(0)
    , remainder_(0)
    , tickCount_(0)
    , droppedUnits_(0)
    , backwardSamples_(0)
    , pauseSource_(nullptr)
    , transitionCount_(0)
{
    if (config_.tickRate == 0) {
        config_.tickRate = 1;
    }

    if (config_.unitsPerSecond == 0) {
        config_.unitsPerSecond = 1;
    }
}

void SimClock::reset()
{
    started_ = false;
    remainder_ = 0;
    pauseSource_ = nullptr;
}

uint32_t SimClock::advance(uint32_t now, const PauseSet* pauses, const TickFunction& tick)
{
    if (running_) {
        return 0;
    }

    if (!started_) {
        started_ = true;
        last_ = now;
        remainder_ = 0;
        syncPauses(pauses);
        return 0;
    }

    // Unsigned subtraction stays correct across the 2^32 wrap. A result in the
    // upper half of the range is time going backward.
    uint32_t delta = now - last_;
    if (delta >= 0x80000000u) {
        backwardSamples_++;
        return 0;
    }

    last_ = now;

    // A pause that began or ended since the last call voids the interval. That
    // includes the first call after a release, when the pauses before it were
    // all seen while paused.
    if (pausesChanged(pauses) || (pauses != nullptr && pauses->isPaused())) {
        remainder_ = 0;
        return 0;
    }

    if (config_.maxFrameDelta != 0 && delta > config_.maxFrameDelta) {
        droppedUnits_ += delta - config_.maxFrameDelta;
        delta = config_.maxFrameDelta;
    }

    // delta < 2^32 and tickRate < 2^32, so the product fits in 64 bits. The
    // remainder is below unitsPerSecond before the add, so the sum fits too.
    remainder_ += static_cast<uint64_t>(delta) * config_.tickRate;

    RunningGuard guard(running_);

    uint32_t ran = 0;
    while (remainder_ >= config_.unitsPerSecond) {
        remainder_ -= config_.unitsPerSecond;
        tickCount_++;
        ran++;

        if (tick) {
            tick();
        }

        // A tick may reset the clock, for example to tear the world down. The
        // pause set passed in may be gone then, so do not touch it.
        if (!started_) {
            break;
        }

        // A tick may also pause, or pause and release.
        if (pausesChanged(pauses) || (pauses != nullptr && pauses->isPaused())) {
            remainder_ = 0;
            break;
        }
    }

    return ran;
}

void SimClock::syncPauses(const PauseSet* pauses)
{
    pauseSource_ = pauses;
    transitionCount_ = pauses != nullptr ? pauses->transitionCount() : 0;
}

bool SimClock::pausesChanged(const PauseSet* pauses)
{
    if (pauses != pauseSource_) {
        // A different set: nothing to compare with.
        syncPauses(pauses);
        return false;
    }

    if (pauses != nullptr && pauses->transitionCount() != transitionCount_) {
        syncPauses(pauses);
        return true;
    }

    return false;
}

} // namespace fallout
