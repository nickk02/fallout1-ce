#include "sim/buffered_attack.h"

namespace fallout {

BufferedAttackRequest buffered_attack_capture(const ObjectRegistry& registry,
    const void* attacker, const void* target, const void* weapon, bool unarmed,
    int hitMode, BufferedAttackKind kind, uint64_t epoch, uint32_t now)
{
    BufferedAttackRequest request;
    request.attacker = object_ref_make(registry, attacker, epoch);
    request.target = object_ref_make(registry, target, epoch);
    request.weapon = object_ref_make(registry, unarmed ? nullptr : weapon, epoch);
    request.unarmed = unarmed;
    request.hitMode = hitMode;
    request.kind = kind;
    request.epoch = epoch;
    request.capturedAt = now;
    return request;
}

void BufferedAttack::put(const BufferedAttackRequest& request)
{
    request_ = request;
}

void BufferedAttack::cancel()
{
    request_.reset();
}

void BufferedAttack::delayExpiry(uint32_t delta)
{
    if (request_) {
        request_->capturedAt += delta;
    }
}

BufferedAttackResult BufferedAttack::take(const ObjectRegistry& registry, uint64_t epoch,
    uint32_t now, const BufferedAttackWeaponCheck& weaponMatchesMode)
{
    if (!request_) {
        return {};
    }

    const BufferedAttackRequest request = *request_;
    request_.reset();

    auto reject = [](BufferedAttackReject reason) {
        return BufferedAttackResult { reason, std::nullopt };
    };

    if (request.epoch != epoch || request.attacker.epoch != epoch
        || request.target.epoch != epoch || (!request.unarmed && request.weapon.epoch != epoch)) {
        return reject(BufferedAttackReject::EpochChanged);
    }
    if (static_cast<uint32_t>(now - request.capturedAt) > 2500u) {
        return reject(BufferedAttackReject::Expired);
    }

    const void* attacker = object_ref_resolve(registry, request.attacker, epoch);
    if (attacker == nullptr) {
        return reject(BufferedAttackReject::StaleAttacker);
    }
    const void* target = object_ref_resolve(registry, request.target, epoch);
    if (target == nullptr) {
        return reject(BufferedAttackReject::StaleTarget);
    }
    const void* weapon = nullptr;
    if (!request.unarmed) {
        weapon = object_ref_resolve(registry, request.weapon, epoch);
        if (weapon == nullptr) {
            return reject(BufferedAttackReject::StaleWeapon);
        }
        if (!weaponMatchesMode) {
            return reject(BufferedAttackReject::WeaponMismatch);
        }
    }
    if (weaponMatchesMode && !weaponMatchesMode(attacker, weapon, request.hitMode)) {
        return reject(BufferedAttackReject::WeaponMismatch);
    }

    return BufferedAttackResult { BufferedAttackReject::None, request };
}

} // namespace fallout
