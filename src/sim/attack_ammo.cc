#include "sim/attack_ammo.h"

namespace fallout {

bool AttackAmmo::prepare(const ObjectRegistry& registry, const void* weapon, uint64_t epoch,
    int quantity, bool eligible)
{
    if (active_) return false;
    weapon_ = object_ref_make(registry, weapon, epoch);
    quantity_ = quantity;
    eligible_ = eligible;
    active_ = true;
    sealed_ = false;
    return true;
}

bool AttackAmmo::seal()
{
    if (!active_) return false;
    sealed_ = true;
    return true;
}

void AttackAmmo::cancel()
{
    weapon_ = ObjectRef();
    quantity_ = 0;
    eligible_ = false;
    active_ = false;
    sealed_ = false;
}

bool AttackAmmo::finish(const ObjectRegistry& registry, uint64_t epoch,
    const std::function<void(void*, int)>& debit)
{
    if (!active_ || !sealed_) return false;
    const ObjectRef weapon = weapon_;
    const int quantity = quantity_;
    const bool eligible = eligible_;
    cancel();
    if (!eligible || quantity <= 0 || !debit) return false;
    void* resolved = object_ref_resolve(registry, weapon, epoch);
    if (resolved == nullptr) return false;
    debit(resolved, quantity);
    return true;
}

} // namespace fallout
