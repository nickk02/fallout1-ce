#ifndef FALLOUT_SIM_ANIMATION_CALLBACKS_H_
#define FALLOUT_SIM_ANIMATION_CALLBACKS_H_

#include <array>
#include <memory>
#include <stdint.h>
#include <string>

#include "object_handle.h"

namespace fallout {

class AnimationLeaseSlot;

class AnimationPayload {
public:
    virtual ~AnimationPayload() = default;
    virtual int invoke() = 0;
};

// The animation engine's payload storage. Descriptors retain their owned marker
// after dispatch so recursive teardown cannot invoke the raw callback union.
class AnimationCallbacks {
public:
    static constexpr int kCapacity = 40;
    void begin(uint64_t serial);
    void clear();
    AnimationCallbacks detach();
    bool add(uint64_t serial, int index, std::unique_ptr<AnimationPayload> payload, bool forced = false);
    bool owns(uint64_t serial, int index) const;
    struct Delivery {
        int result;
        bool current;
    };
    Delivery dispatch(uint64_t serial, int index, bool normalDelivery);

private:
    struct Entry {
        bool owned = false;
        bool forced = false;
        std::unique_ptr<AnimationPayload> payload;
    };
    uint64_t _serial = 0;
    std::array<Entry, kCapacity> _entries;
};

// The accumulating-registration cleanup used by anim.cc. Finalize mutable
// engine state before any detached payload destructor can reenter registration.
void discard_animation_registration(AnimationCallbacks& callbacks, AnimationLeaseSlot& lease, int& currentIndex, int& descriptionCount);

// Text and identity are selected before registration. Presentation remains an
// engine seam so the existing style lookup and floating-text cap stay intact.
class AnimationTaunt final : public AnimationPayload {
public:
    using Epoch = uint64_t (*)();
    using Present = int (*)(void*, const char*, int);
    AnimationTaunt(const ObjectRegistry& registry, ObjectRef actor, const char* text, int type, Epoch epoch, Present present);
    int invoke() override;

private:
    const ObjectRegistry& _registry;
    ObjectRef _actor;
    std::string _text;
    int _type;
    Epoch _epoch;
    Present _present;
};

} // namespace fallout

#endif
