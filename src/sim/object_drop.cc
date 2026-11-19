#include "sim/object_drop.h"

namespace fallout {

int object_drop_guarded(const ObjectRegistry& registry, Object* actor, Object* item, uint64_t (*epoch)(), const ObjectDropHooks& hooks)
{
    const uint64_t capturedEpoch = epoch();
    const ObjectRef actorRef = object_ref_make(registry, actor, capturedEpoch);
    const ObjectRef itemRef = object_ref_make(registry, item, capturedEpoch);
    auto refresh = [&] {
        const uint64_t currentEpoch = epoch();
        actor = static_cast<Object*>(object_ref_resolve(registry, actorRef, currentEpoch));
        item = static_cast<Object*>(object_ref_resolve(registry, itemRef, currentEpoch));
        return actor != nullptr && item != nullptr;
    };
    if (!refresh()) return -1;

    int sid = -1;
    const bool scripted = hooks.scriptId(item, &sid) != -1;
    if (!refresh()) return -1;
    if (scripted) {
        hooks.runScript(sid, actor, item);
        // Reacquire both objects and the live epoch BEFORE looking up the
        // script result. A rebound script must not supply another override.
        if (!refresh() || item->sid != sid) return -1;
        bool overrides = false;
        if (!hooks.readOverride(sid, &overrides)) return -1;
        if (!refresh()) return -1;
        if (overrides) return 0;
    }

    const int removed = hooks.remove(actor, item);
    if (!refresh()) return -1;
    if (removed != 0) return 0;

    Object* owner = hooks.topEnvironment(actor);
    if (!refresh()) return -1;
    if (owner == nullptr) owner = actor;
    const ObjectRef ownerRef = object_ref_make(registry, owner, capturedEpoch);
    owner = static_cast<Object*>(object_ref_resolve(registry, ownerRef, epoch()));
    if (owner == nullptr) return -1;
    const int tile = owner->tile;
    const int elevation = owner->elevation;
    Rect rect;
    const int connected = hooks.connect(item, tile, elevation, &rect);
    if (!refresh() || object_ref_resolve(registry, ownerRef, epoch()) == nullptr) return -1;
    // Removal/connection are committed effects. Cancellation does not undo
    // them. A failed connection has no initialized dirty rectangle to draw.
    if (connected == 0) hooks.refresh(&rect, elevation);
    return 0;
}

} // namespace fallout
