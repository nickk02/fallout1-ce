#ifndef FALLOUT_SIM_ATTACK_AMMO_H_
#define FALLOUT_SIM_ATTACK_AMMO_H_

#include <functional>

#include "object_handle.h"

namespace fallout {

// One ammo claim for the engine's serialized active attack. This does not track
// animation IDs or participants and does not own the engine completion barrier.
class AttackAmmo {
public:
    // Capture after damage/ammo computation, before sequencing. An active
    // attack cannot be overwritten. Ineligible attacks still need seal/cancel.
    bool prepare(const ObjectRegistry& registry, const void* weapon, uint64_t epoch,
        int quantity, bool eligible);
    // Seal only after sequencing succeeds and the engine commits its AP cost.
    // Sealing never emits completion or debits anything.
    bool seal();
    void cancel();
    bool active() const { return active_; }

    // Before seal this leaves preparation intact. After seal it claims once,
    // validates identity and calls debit only for a live eligible weapon.
    // The callback may mutate ammo. No ownership/selected-hand check is made.
    bool finish(const ObjectRegistry& registry, uint64_t epoch,
        const std::function<void(void* weapon, int quantity)>& debit);

private:
    ObjectRef weapon_;
    int quantity_ = 0;
    bool eligible_ = false;
    bool active_ = false;
    bool sealed_ = false;
};

} // namespace fallout

#endif /* FALLOUT_SIM_ATTACK_AMMO_H_ */
