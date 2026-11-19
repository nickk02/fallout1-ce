#include "sim/attack_callbacks.h"

#include "game/anim.h"

namespace fallout {

AttackAnimationPayload::AttackAnimationPayload(const ObjectRegistry& registry, ObjectRef actor, ObjectRef weapon, AttackCallbackKind kind, int anim, AnimationTaunt::Epoch epoch, AttackCallbackHooks hooks)
    : _registry(registry)
    , _actor(actor)
    , _weapon(weapon)
    , _kind(kind)
    , _anim(anim)
    , _epoch(epoch)
    , _hooks(hooks)
{
}

Object* AttackAnimationPayload::actor() const
{
    return static_cast<Object*>(object_ref_resolve(_registry, _actor, _epoch()));
}

int AttackAnimationPayload::invoke()
{
    Object* liveActor = actor();
    if (liveActor == nullptr) return 0;
    if (_kind == AttackCallbackKind::Death) return death();
    Object* liveWeapon = static_cast<Object*>(object_ref_resolve(_registry, _weapon, _epoch()));
    if (liveWeapon == nullptr) return 0;
    if (_kind == AttackCallbackKind::Drop) return _hooks.drop(liveActor, liveWeapon);
    // Destroy may consume the weapon. Neither pointer is accessed afterward.
    return _hooks.destroy(liveWeapon);
}

int AttackAnimationPayload::death()
{
    Object* obj = actor();
    if (obj == nullptr) return 0;
    Rect tempRect;
    Rect dirtyRect;
    _hooks.bound(obj, &dirtyRect);
    obj = actor();
    if (obj == nullptr) return 0;

    if (obj->pid != 16777266 && obj->pid != 16777265 && obj->pid != 16777224) {
        obj->flags |= OBJECT_NO_BLOCK;
        const int result = _hooks.flatten(obj, &tempRect);
        obj = actor();
        if (obj == nullptr) return 0;
        if (result == 0) rect_min_bound(&dirtyRect, &tempRect, &dirtyRect);
    }

    const int outlined = _hooks.outlineOff(obj, &tempRect);
    obj = actor();
    if (obj == nullptr) return 0;
    if (outlined == 0) rect_min_bound(&dirtyRect, &tempRect, &dirtyRect);

    if (_anim >= ANIM_ELECTRIFIED_TO_NOTHING && _anim <= ANIM_EXPLODED_TO_NOTHING) {
        if (obj->pid != 16777265 && obj->pid != 16777266 && obj->pid != 16777239) {
            _hooks.dropAll(obj, obj->tile);
            // Item operations may trigger lifecycle changes. Reacquire the
            // original actor before any later field read or screen refresh.
            obj = actor();
            if (obj == nullptr) return 0;
        }
    }
    _hooks.refresh(&dirtyRect, obj->elevation);
    return 0;
}

} // namespace fallout
