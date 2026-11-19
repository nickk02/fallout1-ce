#ifndef FALLOUT_SIM_ATTACK_INSTANCE_H_
#define FALLOUT_SIM_ATTACK_INSTANCE_H_
#include "sim/attack_ammo.h"
#include "sim/attack_participants.h"
#include <memory>
namespace fallout {

enum class AttackPhase { Preparing,
    Sealed,
    Completing,
    Canceled,
    Finished };
struct AttackInstance {
    AttackInstance(uint64_t id, uint64_t worldEpoch)
        : identity(id)
        , epoch(worldEpoch)
    {
    }
    Attack attack {};
    AttackParticipants participants;
    AttackAmmo ammo;
    const uint64_t identity;
    const uint64_t epoch;
    AttackPhase phase = AttackPhase::Preparing;
    int actionPoints = 0;
    bool eligibleAmmo = false;
    bool displayPending = false;
};

// Whether a committed attack spends the attacker's action points. The player's
// ordinary real-time attacks are free. A shot queued in the pause (VATS) pays,
// as does everyone else's attack and any attack in turn-based combat.
inline bool attack_charges_ap(bool attackerIsPlayer, bool vatsFiring, bool realtime)
{
    return !attackerIsPlayer || vatsFiring || !realtime;
}

// Sole active attack, still behind the engine's global animation barrier.
// Callback pins survive reset; busy remains set until that stack unwinds.
class SerializedAttack {
public:
    using Ptr = std::shared_ptr<AttackInstance>;
    using Epoch = std::function<uint64_t()>;
    using Player = std::function<Object*()>;
    using Stage = std::function<bool(AttackInstance&)>;
    struct Completion {
        std::function<void(AttackInstance&)> ready;
        std::function<void(void*, int)> debit;
        std::function<void(AttackInstance&)> ammoDebited;
        std::function<void(AttackInstance&)> display;
        Stage damage;
        std::function<void(AttackInstance&)> afterDamage;
        Stage endCombat;
        std::function<void(AttackInstance&, bool)> afterEnd;
        std::function<void()> abort;
    };
    enum class Finish { NotReady,
        Completed,
        Aborted };
    explicit SerializedAttack(uint64_t nextIdentity = 1)
        : nextIdentity_(nextIdentity)
    {
    }
    bool start(int barrier, const ObjectRegistry& registry, const Epoch& epoch, const Player& player,
        const Stage& build, const Stage& sequence, const Stage& commit);
    Finish finish(int barrier, const ObjectRegistry& registry, const Epoch& epoch, const Player& player,
        const Completion& hooks);
    void reset();
    Ptr active() const { return active_; }
    bool busy() const { return busy_; }

private:
    bool live(const Ptr& instance, uint64_t epoch) const;
    void cancel(const Ptr& instance);
    Ptr active_;
    uint64_t nextIdentity_;
    bool busy_ = false;
};
} // namespace fallout
#endif
