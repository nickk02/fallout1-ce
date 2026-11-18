#ifndef FALLOUT_OBJECT_HANDLE_H_
#define FALLOUT_OBJECT_HANDLE_H_

#include <stddef.h>
#include <stdint.h>

#include <unordered_map>
#include <vector>

namespace fallout {

// Runtime identity for an object. A handle names a slot and the generation the
// slot had when the handle was issued. Freeing an object bumps the generation,
// so a handle taken before the free never matches a later object that reuses
// the same address or the same slot. Generation 0 is never issued.
struct ObjectHandle {
    uint32_t slot = 0;
    uint64_t generation = 0;

    bool valid() const { return generation != 0; }
    bool operator==(const ObjectHandle& other) const { return slot == other.slot && generation == other.generation; }
    bool operator!=(const ObjectHandle& other) const { return !(*this == other); }
};

// Maps live pointers to handles. It never dereferences a pointer it is given.
class ObjectRegistry {
public:
    // firstGeneration is the generation a fresh slot starts with. Tests use it
    // to reach generation exhaustion without 2^64 cycles.
    explicit ObjectRegistry(uint64_t firstGeneration = 1);

    // A live pointer returns its existing handle and counts a duplicate. Null
    // returns an invalid handle. When memory runs out the call returns an
    // invalid handle and leaves the registry unchanged.
    ObjectHandle create(const void* ptr);

    // True if the pointer was live. Unknown or null pointers return false and
    // change nothing.
    bool destroy(const void* ptr);

    // Null for an invalid, unknown, stale or destroyed handle.
    void* resolve(ObjectHandle handle) const;

    // Invalid when the pointer is not live.
    ObjectHandle handleFor(const void* ptr) const;

    size_t liveCount() const { return _liveCount; }
    uint64_t duplicateCreates() const { return _duplicateCreates; }
    size_t retiredSlots() const { return _retiredSlots; }

private:
    struct Slot {
        const void* ptr;
        uint64_t generation;
        bool live;
        bool retired;
    };

    uint64_t _firstGeneration;
    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeSlots;
    std::unordered_map<const void*, uint32_t> _slotByPtr;
    size_t _liveCount = 0;
    uint64_t _duplicateCreates = 0;
    size_t _retiredSlots = 0;
};

// The process-wide instance. Map loads and game resets do not clear it.
ObjectRegistry& object_registry();

// A handle plus the lifecycle epoch it was taken in. The caller owns the epoch
// counter. A ref taken in an older epoch no longer resolves, even when the
// object is still alive (the player object keeps its handle through a load).
struct ObjectRef {
    ObjectHandle handle;
    uint64_t epoch = 0;
};

// The handle is invalid when ptr is not live.
ObjectRef object_ref_make(const ObjectRegistry& registry, const void* ptr, uint64_t epoch);

// Null if currentEpoch differs from ref.epoch or the handle is stale.
void* object_ref_resolve(const ObjectRegistry& registry, const ObjectRef& ref, uint64_t currentEpoch);

} // namespace fallout

#endif /* FALLOUT_OBJECT_HANDLE_H_ */
