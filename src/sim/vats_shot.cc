#include "sim/vats_shot.h"

namespace fallout {

VatsShot vats_shot_capture(const ObjectRegistry& registry, const void* attacker,
    const void* target, const void* weapon, bool unarmed, int hitMode, int hitLocation, uint64_t epoch)
{
    return VatsShot {
        object_ref_make(registry, attacker, epoch),
        object_ref_make(registry, target, epoch),
        object_ref_make(registry, unarmed ? nullptr : weapon, epoch),
        epoch, unarmed, hitMode, hitLocation
    };
}

VatsShotValidation vats_shot_validate(const ObjectRegistry& registry, const VatsShot& shot,
    uint64_t epoch, const VatsShotCheck& check)
{
    if (shot.epoch != epoch || shot.attacker.epoch != epoch || shot.target.epoch != epoch
        || (!shot.unarmed && shot.weapon.epoch != epoch)) {
        return { VatsShotReject::EpochChanged };
    }
    void* attacker = object_ref_resolve(registry, shot.attacker, epoch);
    if (attacker == nullptr) return { VatsShotReject::StaleAttacker };
    void* target = object_ref_resolve(registry, shot.target, epoch);
    if (target == nullptr) return { VatsShotReject::StaleTarget };
    void* weapon = nullptr;
    if (!shot.unarmed) {
        weapon = object_ref_resolve(registry, shot.weapon, epoch);
        if (weapon == nullptr) return { VatsShotReject::StaleWeapon };
    }
    if (!check) return { VatsShotReject::MissingCheck };
    VatsShotReject reason = check(attacker, target, weapon, shot.hitMode);
    if (reason != VatsShotReject::None) return { reason };
    return { VatsShotReject::None, attacker, target, weapon };
}

} // namespace fallout
