#ifndef FALLOUT_SIM_MOVEMENT_OWNER_H_
#define FALLOUT_SIM_MOVEMENT_OWNER_H_
#include "object_handle.h"
#include <cstdint>
namespace fallout {
struct Object;

// One continuous movement consumer. Legacy sequences retain their own lifetime
// and counter leases; yielding here never cancels or completes a sequence.
class MovementOwner {
public:
    explicit MovementOwner(uint64_t next = 1)
        : next_(next)
    {
    }
    uint64_t acquire(const ObjectRegistry& registry, Object* actor, uint64_t epoch, bool legacyWork);
    Object* resolve(uint64_t token, const ObjectRegistry& registry, uint64_t epoch, Object* player) const;
    Object* release(uint64_t token, const ObjectRegistry& registry, uint64_t epoch, Object* player);
    bool yield(const void* actor);
    uint64_t generation() const { return generation_; }
    bool current(uint64_t token) const { return token != 0 && token == token_; }
    // Token zero may clear unowned pending state, never a newer owner.
    bool mayClear(uint64_t token) const { return token == token_; }

private:
    ObjectRef actor_;
    const void* address_ = nullptr;
    uint64_t next_;
    uint64_t token_ = 0;
    uint64_t generation_ = 0;
};
// A pause/menu may revoke movement while pose changes are forbidden. Recovery
// is claimed later by world input dispatch and cannot outlive its allocation,
// epoch, or movement generation. Legacy registration explicitly cancels it.
class MovementStandRecovery {
public:
    void request(const ObjectRegistry& registry, Object* actor, uint64_t epoch, uint64_t generation);
    Object* candidate(const ObjectRegistry& registry, uint64_t epoch, Object* player) const;
    Object* take(const ObjectRegistry& registry, uint64_t epoch, Object* player,
        uint64_t generation, bool movementOwned, bool legacyWork);
    void cancel()
    {
        actor_ = {};
        address_ = nullptr;
    }
    void cancelFor(const void* actor)
    {
        if (address_ == actor) cancel();
    }

private:
    ObjectRef actor_;
    const void* address_ = nullptr;
    uint64_t generation_ = 0;
};
} // namespace fallout
#endif
