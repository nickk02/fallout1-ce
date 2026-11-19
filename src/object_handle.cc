#include "object_handle.h"

#include <new>

namespace fallout {

ObjectRegistry::ObjectRegistry(uint64_t firstGeneration)
{
    // 0 is invalid and UINT64_MAX is the retirement marker.
    if (firstGeneration == 0) {
        firstGeneration = 1;
    } else if (firstGeneration >= UINT64_MAX) {
        firstGeneration = UINT64_MAX - 1;
    }
    _firstGeneration = firstGeneration;
}

ObjectHandle ObjectRegistry::create(const void* ptr)
{
    if (ptr == nullptr) {
        return ObjectHandle();
    }

    try {
        auto found = _slotByPtr.find(ptr);
        if (found != _slotByPtr.end()) {
            _duplicateCreates++;
            return ObjectHandle { found->second, _slots[found->second].generation };
        }

        uint32_t index;
        if (!_freeSlots.empty()) {
            index = _freeSlots.back();
            // The only call that can throw; nothing has changed before it.
            _slotByPtr.emplace(ptr, index);
            _freeSlots.pop_back();
        } else {
            if (_slots.size() >= UINT32_MAX) {
                return ObjectHandle();
            }
            index = static_cast<uint32_t>(_slots.size());

            // Make room for destroy() to push this slot onto the free list
            // without allocating, so destroy() cannot fail.
            _freeSlots.reserve(_slots.size() + 1);
            _slots.push_back(Slot { nullptr, _firstGeneration, false, false });
            try {
                _slotByPtr.emplace(ptr, index);
            } catch (...) {
                _slots.pop_back();
                throw;
            }
        }

        Slot& slot = _slots[index];
        slot.ptr = ptr;
        slot.live = true;
        _liveCount++;
        return ObjectHandle { index, slot.generation };
    } catch (const std::bad_alloc&) {
        return ObjectHandle();
    }
}

bool ObjectRegistry::destroy(const void* ptr)
{
    if (ptr == nullptr) {
        return false;
    }

    auto found = _slotByPtr.find(ptr);
    if (found == _slotByPtr.end()) {
        return false;
    }

    uint32_t index = found->second;
    _slotByPtr.erase(found);

    Slot& slot = _slots[index];
    slot.ptr = nullptr;
    slot.live = false;
    _liveCount--;

    uint64_t next = slot.generation + 1;
    slot.generation = next;
    if (next == UINT64_MAX) {
        // Reusing this slot would eventually issue a generation that wraps.
        slot.retired = true;
        _retiredSlots++;
    } else {
        // Capacity was reserved when the slot was added.
        _freeSlots.push_back(index);
    }

    return true;
}

void* ObjectRegistry::resolve(ObjectHandle handle) const
{
    if (!handle.valid() || handle.slot >= _slots.size()) {
        return nullptr;
    }

    const Slot& slot = _slots[handle.slot];
    if (!slot.live || slot.generation != handle.generation) {
        return nullptr;
    }

    return const_cast<void*>(slot.ptr);
}

ObjectHandle ObjectRegistry::handleFor(const void* ptr) const
{
    if (ptr == nullptr) {
        return ObjectHandle();
    }

    auto found = _slotByPtr.find(ptr);
    if (found == _slotByPtr.end()) {
        return ObjectHandle();
    }

    return ObjectHandle { found->second, _slots[found->second].generation };
}

ObjectRegistry& object_registry()
{
    static ObjectRegistry registry;
    return registry;
}

ObjectRef object_ref_make(const ObjectRegistry& registry, const void* ptr, uint64_t epoch)
{
    ObjectRef ref;
    ref.handle = registry.handleFor(ptr);
    ref.epoch = epoch;
    return ref;
}

void* object_ref_resolve(const ObjectRegistry& registry, const ObjectRef& ref, uint64_t currentEpoch)
{
    if (currentEpoch != ref.epoch) {
        return nullptr;
    }

    return registry.resolve(ref.handle);
}

} // namespace fallout
