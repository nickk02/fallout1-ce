#include "sim/calendar_pull.h"

namespace fallout {

namespace {

    // Clears the flag on every way out of a pass.
    struct PassGuard {
        explicit PassGuard(bool& flag)
            : flag_(flag)
        {
            flag_ = true;
        }

        ~PassGuard() { flag_ = false; }

        bool& flag_;
    };

    enum class Stop {
        None,
        Epoch,
        Quit,
        InvalidTime,
        Deferred,
    };

} // namespace

CalendarPull::CalendarPull()
    : CalendarPull(CalendarPullConfig())
{
}

CalendarPull::CalendarPull(const CalendarPullConfig& config)
    : config_(config)
    , accumulator_(config.simTicksPerGameTick)
    , armed_(false)
    , inPass_(false)
    , epoch_(0)
    , pending_(0)
    , dropped_(0)
    , lastStatus_(CalendarPullStatus::Done)
{
}

void CalendarPull::disarm()
{
    armed_ = false;
    pending_ = 0;
    accumulator_.disarm();
}

void CalendarPull::rearm(const CalendarPullEnv& env, uint64_t epoch)
{
    accumulator_.rebase(env.simTicks());
    epoch_ = epoch;
    pending_ = 0;
    armed_ = true;
}

CalendarPullResult CalendarPull::pull(CalendarQueue& queue, const CalendarPullEnv& env)
{
    if (inPass_) {
        CalendarPullResult reentered;
        reentered.status = CalendarPullStatus::Reentered;
        return reentered;
    }

    CalendarPullResult result = pass(queue, env);
    lastStatus_ = result.status;
    return result;
}

CalendarPullResult CalendarPull::pass(CalendarQueue& queue, const CalendarPullEnv& env)
{
    CalendarPullResult result;

    // Held through the first drain and every step.
    PassGuard guard(inPass_);

    uint64_t epoch = env.epoch();
    if (!armed_ || epoch != epoch_) {
        rearm(env, epoch);
        result.status = CalendarPullStatus::Rearmed;
        return result;
    }

    // Ticks owed. The cap is what bounds time that built up while nobody pulled.
    uint64_t owed = static_cast<uint64_t>(pending_) + accumulator_.take(env.simTicks());
    if (owed > config_.maxCatchUpTicks) {
        uint64_t over = owed - config_.maxCatchUpTicks;
        dropped_ += over;
        result.droppedTicks = over > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(over);
        owed = config_.maxCatchUpTicks;
    }
    pending_ = static_cast<uint32_t>(owed);

    // What would end the pass, in order of strength. The terminal ones also end
    // the lifecycle of the owed ticks.
    auto stopNow = [&]() {
        if (env.epoch() != epoch_) {
            return Stop::Epoch;
        }
        if (env.quit && env.quit()) {
            return Stop::Quit;
        }
        if (!queue.timeValid()) {
            return Stop::InvalidTime;
        }
        if (env.deferred && env.deferred()) {
            return Stop::Deferred;
        }
        return Stop::None;
    };

    auto finish = [&](Stop stop) {
        switch (stop) {
        case Stop::Epoch:
            disarm();
            result.status = CalendarPullStatus::EpochChanged;
            break;
        case Stop::Quit:
            disarm();
            result.status = CalendarPullStatus::Quit;
            break;
        case Stop::InvalidTime:
            pending_ = 0;
            result.status = CalendarPullStatus::InvalidTime;
            break;
        default:
            result.status = CalendarPullStatus::Deferred;
            break;
        }
    };

    uint32_t budget = config_.maxEventsPerPass;

    // Runs what is due. Returns true when the pass must end, with the result set.
    Stop lastStop = Stop::None;
    auto drain = [&]() {
        CampaignAdvanceOptions options;
        options.maxEvents = budget;
        options.shouldStop = [&]() {
            lastStop = stopNow();
            return lastStop != Stop::None;
        };

        CampaignAdvanceResult drained = drainDueEvents(queue, options);
        result.events += drained.events;
        budget -= drained.events;

        switch (drained.status) {
        case CampaignAdvanceStatus::Stopped:
            finish(lastStop);
            return true;
        case CampaignAdvanceStatus::Interrupted:
            result.status = CalendarPullStatus::Interrupted;
            return true;
        case CampaignAdvanceStatus::NoProgress:
            result.status = CalendarPullStatus::EventBudget;
            return true;
        default:
            return false;
        }
    };

    auto invalidTime = [&]() {
        pending_ = 0;
        result.status = CalendarPullStatus::InvalidTime;
    };

    if (!queue.timeValid()) {
        invalidTime();
        return result;
    }

    if (drain()) {
        return result;
    }

    while (pending_ > 0) {
        Stop stop = stopNow();
        if (stop != Stop::None) {
            finish(stop);
            return result;
        }

        if (!queue.timeValid()) {
            invalidTime();
            return result;
        }

        uint32_t now = queue.gameTime();
        if (now >= config_.timeLimit) {
            pending_ = 0;
            result.status = CalendarPullStatus::TimeLimit;
            return result;
        }

        if (!queue.trySetGameTime(now + 1)) {
            invalidTime();
            return result;
        }

        pending_--;
        result.gameTicks++;

        if (!queue.timeValid()) {
            invalidTime();
            return result;
        }

        if (drain()) {
            return result;
        }
    }

    result.status = CalendarPullStatus::Done;
    return result;
}

const char* calendar_pull_status_name(CalendarPullStatus status)
{
    switch (status) {
    case CalendarPullStatus::Done:
        return "done";
    case CalendarPullStatus::Reentered:
        return "reentered";
    case CalendarPullStatus::Rearmed:
        return "rearmed";
    case CalendarPullStatus::Interrupted:
        return "interrupted";
    case CalendarPullStatus::EventBudget:
        return "event_budget";
    case CalendarPullStatus::Deferred:
        return "deferred";
    case CalendarPullStatus::Quit:
        return "quit";
    case CalendarPullStatus::EpochChanged:
        return "epoch_changed";
    case CalendarPullStatus::TimeLimit:
        return "time_limit";
    case CalendarPullStatus::InvalidTime:
        return "invalid_time";
    }
    return "unknown";
}

bool calendar_pull_status_should_log(StatusLogGate& gate, CalendarPullStatus status, uint64_t nowTicks, uint32_t* suppressed)
{
    switch (status) {
    case CalendarPullStatus::EventBudget:
    case CalendarPullStatus::TimeLimit:
    case CalendarPullStatus::InvalidTime:
        return gate.shouldLog(static_cast<int>(status), nowTicks, suppressed);
    case CalendarPullStatus::Reentered:
        return false;
    default:
        gate.clear();
        return false;
    }
}

} // namespace fallout
