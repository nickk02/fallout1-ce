#include "sim/attack_instance.h"
#include <limits>
namespace fallout {
namespace {
    struct BusyScope {
        bool& flag;
        explicit BusyScope(bool& value)
            : flag(value)
        {
            flag = true;
        }
        ~BusyScope() { flag = false; }
    };
}
bool SerializedAttack::live(const Ptr& instance, uint64_t epoch) const
{
    return active_ == instance && instance->epoch == epoch
        && instance->phase != AttackPhase::Canceled && instance->phase != AttackPhase::Finished;
}
void SerializedAttack::cancel(const Ptr& instance)
{
    instance->ammo.cancel();
    instance->displayPending = false;
    instance->phase = AttackPhase::Canceled;
    if (active_ == instance) active_.reset();
}
void SerializedAttack::reset()
{
    if (active_) cancel(active_);
}
bool SerializedAttack::start(int barrier, const ObjectRegistry& registry, const Epoch& epoch, const Player& player,
    const Stage& build, const Stage& sequence, const Stage& commit)
{
    if (barrier != 0 || busy_ || active_ || nextIdentity_ == 0) return false;
    BusyScope busy(busy_);
    const Ptr instance = std::make_shared<AttackInstance>(nextIdentity_, epoch());
    nextIdentity_ = nextIdentity_ == std::numeric_limits<uint64_t>::max() ? 0 : nextIdentity_ + 1;
    active_ = instance;
    auto checkpoint = [&] {
        return live(instance, epoch()) && instance->participants.refresh(registry, epoch(), player(), instance->attack);
    };
    if (!build(*instance) || !live(instance, epoch())) {
        cancel(instance);
        return false;
    }
    instance->participants = AttackParticipants(registry, instance->attack, instance->epoch, player());
    if (!checkpoint() || !instance->ammo.prepare(registry, instance->attack.weapon, instance->epoch, instance->attack.ammoQuantity, instance->eligibleAmmo)) {
        cancel(instance);
        return false;
    }
    if (!sequence(*instance) || !checkpoint()) {
        cancel(instance);
        return false;
    }
    // Existing AP/throw effects are not rolled back if a callback cancels here.
    if (!commit(*instance) || !checkpoint()) {
        cancel(instance);
        return false;
    }
    if (!instance->ammo.seal()) {
        cancel(instance);
        return false;
    }
    instance->displayPending = true;
    instance->phase = AttackPhase::Sealed;
    return true;
}
SerializedAttack::Finish SerializedAttack::finish(int barrier, const ObjectRegistry& registry, const Epoch& epoch,
    const Player& player, const Completion& hooks)
{
    if (barrier != 0 || busy_ || !active_ || active_->phase != AttackPhase::Sealed) return Finish::NotReady;
    const Ptr instance = active_;
    BusyScope busy(busy_);
    instance->phase = AttackPhase::Completing; // Claim before any callback.
    const bool display = instance->displayPending;
    instance->displayPending = false;
    auto checkpoint = [&] {
        return live(instance, epoch()) && instance->participants.refresh(registry, epoch(), player(), instance->attack);
    };
    auto abort = [&] {
        bool sameWorld = live(instance, epoch());
        cancel(instance);
        if (sameWorld && hooks.abort) hooks.abort();
        return Finish::Aborted;
    };
    if (!live(instance, epoch())) return abort();
    if (hooks.ready) hooks.ready(*instance);
    if (!live(instance, epoch())) return abort();
    // Ammo was committed by seal. Participant loss must not silently erase it.
    bool debited = instance->ammo.finish(registry, epoch(), hooks.debit);
    if (!live(instance, epoch())) return abort();
    if (debited && hooks.ammoDebited) hooks.ammoDebited(*instance);
    if (!checkpoint()) return abort();
    if (display && hooks.display) hooks.display(*instance);
    if (!checkpoint()) return abort();
    if (hooks.damage && !hooks.damage(*instance)) return abort();
    if (!checkpoint()) return abort();
    if (hooks.afterDamage) hooks.afterDamage(*instance);
    if (!checkpoint()) return abort();
    bool ended = hooks.endCombat ? hooks.endCombat(*instance) : false;
    if (!checkpoint()) return abort();
    if (hooks.afterEnd) hooks.afterEnd(*instance, ended);
    if (!checkpoint()) return abort();
    instance->phase = AttackPhase::Finished;
    if (active_ == instance) active_.reset();
    return Finish::Completed;
}
} // namespace fallout
