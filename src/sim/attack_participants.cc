#include "sim/attack_participants.h"

namespace fallout {

AttackParticipants::AttackParticipants(const ObjectRegistry& registry, const Attack& attack,
    uint64_t epoch, Object* player)
    : epoch_(epoch)
    , extrasLength_(attack.extrasLength)
{
    if (extrasLength_ < 0 || extrasLength_ > EXPLOSION_TARGET_COUNT) return;
    std::array<Object*, kCount> objects {};
    objects[0] = attack.attacker;
    objects[1] = attack.defender;
    objects[2] = attack.oops;
    objects[3] = player;
    for (int i = 0; i < extrasLength_; i++)
        objects[4 + i] = attack.extras[i];
    for (int i = 0; i < kCount; i++) {
        required_[i] = objects[i] != nullptr;
        refs_[i] = object_ref_make(registry, objects[i], epoch);
    }
    captured_ = true;
}

bool AttackParticipants::refresh(const ObjectRegistry& registry, uint64_t epoch, Object* player, Attack& attack) const
{
    if (!captured_ || epoch != epoch_) return false;
    std::array<Object*, kCount> objects {};
    for (int i = 0; i < kCount; i++) {
        objects[i] = static_cast<Object*>(object_ref_resolve(registry, refs_[i], epoch));
        if (required_[i] && objects[i] == nullptr) return false;
    }
    if (objects[3] != player) return false;
    attack.attacker = objects[0];
    attack.defender = objects[1];
    attack.oops = objects[2];
    attack.extrasLength = extrasLength_;
    for (int i = 0; i < extrasLength_; i++)
        attack.extras[i] = objects[4 + i];
    return true;
}

bool AttackParticipants::refreshForAttack(const ObjectRegistry& registry, uint64_t epoch, Object* player,
    Attack& attack, const ObjectRef& weapon, bool consumedThrow) const
{
    if (!refresh(registry, epoch, player, attack)) return false;
    if (consumedThrow) {
        attack.weapon = static_cast<Object*>(object_ref_resolve(registry, weapon, epoch));
        return true;
    }
    if (!weapon.handle.valid()) return attack.weapon == nullptr;
    attack.weapon = static_cast<Object*>(object_ref_resolve(registry, weapon, epoch));
    return attack.weapon != nullptr;
}

bool AttackParticipants::clearForAttack(const ObjectRegistry& registry, const std::function<uint64_t()>& epoch,
    const std::function<Object*()>& player, Attack& attack, const ObjectRef& weapon,
    const std::function<int(Object*)>& clear) const
{
    if (!refreshForAttack(registry, epoch(), player(), attack, weapon)) return false;
    const int count = attack.extrasLength + 2;
    for (int i = 0; i < count; i++) {
        Object* actor = i == 0 ? attack.attacker : i == 1 ? attack.defender
                                                          : attack.extras[i - 2];
        if (actor == nullptr) continue;
        int status = clear(actor);
        if (!refreshForAttack(registry, epoch(), player(), attack, weapon) || status == -2) return false;
    }
    return true;
}

bool AttackParticipants::run(const ObjectRegistry& registry, const std::function<uint64_t()>& epoch,
    const std::function<Object*()>& player,
    Attack& attack, const std::function<void()>& callback) const
{
    if (!refresh(registry, epoch(), player(), attack)) return false;
    callback();
    return refresh(registry, epoch(), player(), attack);
}

int attack_visual_damage_type(int capturedType, Object* weapon, const std::function<int(Object*)>& lookup)
{
    return capturedType >= 0 ? capturedType : lookup(weapon);
}

DeferredAttackReport::DeferredAttackReport(const ObjectRegistry& registry, const Attack& original,
    uint64_t epoch, Object* player, Object* realSource)
    : attack(original)
    , source(realSource)
    , sourceRef_(object_ref_make(registry, realSource, epoch))
    , hadSource_(realSource != nullptr)
{
    attack.attacker = nullptr;
    participants = AttackParticipants(registry, attack, epoch, player);
}

bool DeferredAttackReport::refresh(const ObjectRegistry& registry, uint64_t epoch, Object* player)
{
    if (!participants.refresh(registry, epoch, player, attack)) return false;
    source = static_cast<Object*>(object_ref_resolve(registry, sourceRef_, epoch));
    return !hadSource_ || source != nullptr;
}

AttackReportRegistration::AttackReportRegistration(std::shared_ptr<DeferredAttackReport> report)
    : state_(std::make_shared<State>())
    , token_(new Token { std::move(report), state_ })
{
}

AttackReportRegistration::~AttackReportRegistration()
{
    if (!committed_ && !state_->delivered) delete token_;
}

std::shared_ptr<DeferredAttackReport> AttackReportRegistration::take(void* opaque)
{
    std::unique_ptr<Token> token(static_cast<Token*>(opaque));
    token->state->delivered = true;
    return token->report;
}

} // namespace fallout
