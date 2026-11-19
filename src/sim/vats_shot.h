#ifndef FALLOUT_SIM_VATS_SHOT_H_
#define FALLOUT_SIM_VATS_SHOT_H_

#include <functional>

#include "object_handle.h"

namespace fallout {

// Queue identity only. VATS order, reservation costs and execution stay in combat.
struct VatsShot {
    ObjectRef attacker;
    ObjectRef target;
    ObjectRef weapon;
    uint64_t epoch = 0;
    bool unarmed = false;
    int hitMode = 0;
    int hitLocation = 0;
};

VatsShot vats_shot_capture(const ObjectRegistry& registry, const void* attacker,
    const void* target, const void* weapon, bool unarmed, int hitMode, int hitLocation, uint64_t epoch);

enum class VatsShotReject {
    None,
    EpochChanged,
    StaleAttacker,
    StaleTarget,
    StaleWeapon,
    WrongAttacker,
    InactiveAttacker,
    InactiveTarget,
    WeaponMismatch,
    MissingCheck,
};

// Ephemeral resolved view. Revalidate after any input pump or gameplay callback.
// A rejected view contains no pointers.
struct VatsShotValidation {
    VatsShotReject reason;
    void* attacker = nullptr;
    void* target = nullptr;
    void* weapon = nullptr;
};

// Check actor identity/liveness and weapon ownership plus selection by the
// captured mode. The check must only read state. Every required reference has
// resolved before it runs. weapon is null for an explicitly unarmed shot.
using VatsShotCheck = std::function<VatsShotReject(void* attacker, void* target, void* weapon, int hitMode)>;

// Does not consume, expire or change the snapshot. Call before and after a modal.
VatsShotValidation vats_shot_validate(const ObjectRegistry& registry, const VatsShot& shot,
    uint64_t epoch, const VatsShotCheck& check);

} // namespace fallout

#endif /* FALLOUT_SIM_VATS_SHOT_H_ */
