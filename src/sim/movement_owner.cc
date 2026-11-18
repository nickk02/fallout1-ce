#include "sim/movement_owner.h"
#include <limits>
namespace fallout {
uint64_t MovementOwner::acquire(const ObjectRegistry& registry, Object* actor, uint64_t epoch, bool legacyWork)
{
    if (token_ != 0 || next_ == 0 || legacyWork || actor == nullptr) return 0;
    ObjectRef ref = object_ref_make(registry, actor, epoch);
    if (!ref.handle.valid()) return 0;
    actor_ = ref;
    address_ = actor;
    token_ = next_;
    generation_ = token_;
    next_ = next_ == std::numeric_limits<uint64_t>::max() ? 0 : next_ + 1;
    return token_;
}
Object* MovementOwner::resolve(uint64_t token, const ObjectRegistry& registry, uint64_t epoch, Object* player) const
{
    if (!current(token)) return nullptr;
    auto* actor = static_cast<Object*>(object_ref_resolve(registry, actor_, epoch));
    return actor == player ? actor : nullptr;
}
Object* MovementOwner::release(uint64_t token, const ObjectRegistry& registry, uint64_t epoch, Object* player)
{
    if (!current(token)) return nullptr;
    Object* actor = resolve(token, registry, epoch, player);
    token_ = 0;
    address_ = nullptr;
    return actor;
}
bool MovementOwner::yield(const void* actor)
{
    if (token_ == 0 || actor != address_) return false;
    token_ = 0;
    address_ = nullptr;
    return true;
}
void MovementStandRecovery::request(const ObjectRegistry& registry, Object* actor, uint64_t epoch, uint64_t generation)
{
    actor_ = object_ref_make(registry, actor, epoch);
    address_ = actor;
    generation_ = generation;
}
Object* MovementStandRecovery::candidate(const ObjectRegistry& registry, uint64_t epoch, Object* player) const
{
    Object* actor = static_cast<Object*>(object_ref_resolve(registry, actor_, epoch));
    return actor == player ? actor : nullptr;
}
Object* MovementStandRecovery::take(const ObjectRegistry& registry, uint64_t epoch, Object* player,
    uint64_t generation, bool movementOwned, bool legacyWork)
{
    Object* actor = candidate(registry, epoch, player);
    bool allowed = generation == generation_ && !movementOwned && !legacyWork;
    cancel();
    return allowed ? actor : nullptr;
}
} // namespace fallout
