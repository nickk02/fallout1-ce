#ifndef FALLOUT_SIM_ATTACK_CALLBACKS_H_
#define FALLOUT_SIM_ATTACK_CALLBACKS_H_

#include "game/object_types.h"
#include "plib/gnw/rect.h"
#include "sim/animation_callbacks.h"

namespace fallout {

enum class AttackCallbackKind { Drop,
    Destroy,
    Death };

// Engine resource seams. The payload and death continuation below are shared
// with asset-free tests; no seam stores an object beyond its call.
struct AttackCallbackHooks {
    int (*drop)(Object*, Object*);
    int (*destroy)(Object*);
    void (*bound)(Object*, Rect*);
    int (*flatten)(Object*, Rect*);
    int (*outlineOff)(Object*, Rect*);
    int (*dropAll)(Object*, int);
    void (*refresh)(Rect*, int);
};

class AttackAnimationPayload final : public AnimationPayload {
public:
    AttackAnimationPayload(const ObjectRegistry& registry, ObjectRef actor, ObjectRef weapon, AttackCallbackKind kind, int anim, AnimationTaunt::Epoch epoch, AttackCallbackHooks hooks);
    int invoke() override;

private:
    int death();
    Object* actor() const;
    const ObjectRegistry& _registry;
    ObjectRef _actor;
    ObjectRef _weapon;
    AttackCallbackKind _kind;
    int _anim;
    AnimationTaunt::Epoch _epoch;
    AttackCallbackHooks _hooks;
};

} // namespace fallout

#endif
