#include "sim/animation_lease.h"
#include <limits>

namespace fallout {

CombatAnimationLease CombatAnimationLeases::begin()
{
    if (exhausted_ || nextIdentity_ == 0) return {};
    for (uint64_t& entry : active_) {
        if (entry == 0) {
            entry = nextIdentity_;
            if (nextIdentity_ == std::numeric_limits<uint64_t>::max())
                exhausted_ = true;
            else
                ++nextIdentity_;
            ++count_;
            return { generation_, entry };
        }
    }
    return {};
}

bool CombatAnimationLeases::finish(CombatAnimationLease lease)
{
    if (!lease.valid() || lease.generation != generation_) return false;
    for (uint64_t& entry : active_) {
        if (entry == lease.identity) {
            entry = 0;
            --count_;
            return true;
        }
    }
    return false;
}

void CombatAnimationLeases::reset()
{
    active_.fill(0);
    count_ = 0;
    if (generation_ == std::numeric_limits<uint64_t>::max())
        exhausted_ = true;
    else
        ++generation_;
}

bool AnimationLeaseSlot::begin()
{
    if (active_ || exhausted_ || revision_ == std::numeric_limits<uint64_t>::max()) return false;
    ++revision_;
    active_ = true;
    ending_ = false;
    lease = {};
    return true;
}

void AnimationLeaseSlot::reset()
{
    if (revision_ == std::numeric_limits<uint64_t>::max())
        exhausted_ = true;
    else
        ++revision_;
    active_ = false;
    ending_ = false;
    lease = {};
}

AnimationLeaseSlot::End AnimationLeaseSlot::claimEnd()
{
    if (!active_ || ending_) return {};
    ending_ = true;
    End result { revision_, lease, true };
    lease = {};
    return result;
}

bool AnimationLeaseSlot::current(const End& end) const
{
    return end.valid && active_ && ending_ && end.revision == revision_;
}

bool AnimationLeaseSlot::complete(const End& end)
{
    if (!current(end)) return false;
    active_ = false;
    ending_ = false;
    return true;
}

} // namespace fallout
