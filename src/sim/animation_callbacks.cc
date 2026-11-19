#include "sim/animation_callbacks.h"
#include "sim/animation_lease.h"

#include <utility>

namespace fallout {

AnimationCallbacks AnimationCallbacks::detach()
{
    AnimationCallbacks retired;
    retired._entries.swap(_entries);
    retired._serial = _serial;
    _serial = 0;
    return retired;
}

void AnimationCallbacks::clear()
{
    auto retired = detach();
}

void AnimationCallbacks::begin(uint64_t serial)
{
    auto retired = detach();
    _serial = serial;
    // No writes after retired's destructors, which can reset or replace us.
}

void discard_animation_registration(AnimationCallbacks& callbacks, AnimationLeaseSlot& lease, int& currentIndex, int& descriptionCount)
{
    auto retired = callbacks.detach();
    lease.reset();
    currentIndex = -1;
    descriptionCount = 0;
    // Disposal is last. Do not consult currentIndex again or overwrite work
    // registered by a destructor, even when it reuses this same slot.
}

bool AnimationCallbacks::add(uint64_t serial, int index, std::unique_ptr<AnimationPayload> payload, bool forced)
{
    if (serial == 0 || serial != _serial || index < 0 || index >= kCapacity || !payload || _entries[index].owned) return false;
    _entries[index].owned = true;
    _entries[index].forced = forced;
    _entries[index].payload = std::move(payload);
    return true;
}

bool AnimationCallbacks::owns(uint64_t serial, int index) const
{
    return serial != 0 && serial == _serial && index >= 0 && index < kCapacity && _entries[index].owned;
}

AnimationCallbacks::Delivery AnimationCallbacks::dispatch(uint64_t serial, int index, bool normalDelivery)
{
    if (!owns(serial, index)) return { 0, false };
    // Claim before invoking. Reset and slot reuse cannot destroy this local pin.
    const bool invoke = normalDelivery || _entries[index].forced;
    auto payload = std::move(_entries[index].payload);
    int result = payload && invoke ? payload->invoke() : 0;
    payload.reset();
    // Disposal is also a reentry boundary. Do not authorize continuation of a
    // reset/reused slot even when the callback returned success.
    return { result, _serial == serial };
}

AnimationTaunt::AnimationTaunt(const ObjectRegistry& registry, ObjectRef actor, const char* text, int type, Epoch epoch, Present present)
    : _registry(registry)
    , _actor(actor)
    , _text(text)
    , _type(type)
    , _epoch(epoch)
    , _present(present)
{
}

int AnimationTaunt::invoke()
{
    void* actor = object_ref_resolve(_registry, _actor, _epoch());
    return actor != nullptr ? _present(actor, _text.c_str(), _type) : 0;
}

} // namespace fallout
