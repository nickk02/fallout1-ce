#ifndef FALLOUT_SIM_ANIMATION_LEASE_H_
#define FALLOUT_SIM_ANIMATION_LEASE_H_

#include <array>
#include <cstdint>

namespace fallout {

struct CombatAnimationLease {
    uint64_t generation = 0;
    uint64_t identity = 0;
    bool valid() const { return generation != 0 && identity != 0; }
};

// Runtime-only leases. Neither loaded counts nor reset generations create work.
class CombatAnimationLeases {
public:
    static constexpr int kCapacity = 21;
    explicit CombatAnimationLeases(uint64_t nextIdentity = 1)
        : nextIdentity_(nextIdentity)
    {
    }
    CombatAnimationLease begin();
    bool finish(CombatAnimationLease lease);
    void reset();
    void normalizeLoadedCount(int) { reset(); }
    int count() const { return count_; }

private:
    std::array<uint64_t, kCapacity> active_ {};
    uint64_t generation_ = 1;
    uint64_t nextIdentity_;
    int count_ = 0;
    bool exhausted_ = false;
};

class AnimationLeaseSlot {
public:
    struct End {
        uint64_t revision = 0;
        CombatAnimationLease lease;
        bool valid = false;
    };
    explicit AnimationLeaseSlot(uint64_t revision = 0)
        : revision_(revision)
    {
    }
    bool begin();
    void reset();
    End claimEnd();
    bool current(const End& end) const;
    bool complete(const End& end);
    CombatAnimationLease lease;

private:
    uint64_t revision_;
    bool active_ = false;
    bool ending_ = false;
    bool exhausted_ = false;
};

} // namespace fallout
#endif
