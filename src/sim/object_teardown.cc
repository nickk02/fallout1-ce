#include "sim/object_teardown.h"

#include <new>
#include <utility>

namespace fallout {

ObjectTeardown::ObjectTeardown(ObjectRegistry& registry, ObjectListNode** lists, size_t count,
    ObjectListNode*& floating, ObjectTeardownHooks hooks)
    : registry_(registry)
    , lists_(lists)
    , count_(count)
    , floating_(floating)
    , hooks_(std::move(hooks))
{
}

bool ObjectTeardown::destroying(const Object* object) const
{
    const ObjectHandle identity = registry_.handleFor(object);
    if (!identity.valid()) return false;
    for (const Batch* batch = active_; batch != nullptr; batch = batch->previous)
        for (const Entry& entry : batch->entries)
            if (!entry.complete && entry.object == object && entry.identity == identity) return true;
    return false;
}

bool ObjectTeardown::allocationAllowed() const
{
    return !hooks_.allocationAllowed || hooks_.allocationAllowed();
}

ObjectListNode* ObjectTeardown::findNode(Object* object) const
{
    ObjectListNode* head = object->tile >= 0 && static_cast<size_t>(object->tile) < count_
        ? lists_[object->tile]
        : floating_;
    for (auto* node = head; node != nullptr; node = node->next)
        if (node->obj == object) return node;
    return nullptr;
}

bool ObjectTeardown::gather(Batch& batch, Object* object, bool force)
{
    if (object == nullptr || destroying(object)) return true;
    const auto identity = registry_.handleFor(object);
    if (!identity.valid() || (!force && (object->flags & OBJECT_NO_REMOVE) != 0)) return false;
    // A malformed cyclic/shared inventory cannot be detached as a tree.
    for (const auto& entry : batch.entries)
        if (entry.object == object) return false;
    const Inventory inventory = object->data.inventory;
    if (inventory.length < 0 || inventory.capacity < inventory.length
        || (inventory.length != 0 && inventory.items == nullptr)) return false;
    const size_t index = batch.entries.size();
    if (!allocationAllowed()) return false;
    batch.entries.push_back({ object, identity, inventory, findNode(object), object->sid,
        object->sid != -1 ? hooks_.scriptIdentity(object->sid) : 0, force });
    for (int i = 0; i < inventory.length; ++i)
        if (!gather(batch, inventory.items[i].item, true)) return false;
    if (!allocationAllowed()) return false;
    batch.order.push_back(index);
    return true;
}

void ObjectTeardown::unlink(ObjectListNode* node, Object* object)
{
    if (node == nullptr) return;
    ObjectListNode** link = object->tile >= 0 && static_cast<size_t>(object->tile) < count_
        ? &lists_[object->tile]
        : &floating_;
    while (*link != nullptr && *link != node)
        link = &(*link)->next;
    if (*link == node) *link = node->next;
    node->next = nullptr;
}

int ObjectTeardown::run(Batch& batch, Inventory* external, const std::function<void()>& beforeScripts)
{
    const uint64_t epoch = hooks_.epoch();
    batch.previous = active_;
    active_ = &batch;
    if (!batch.entries.empty() && hooks_.invalidateIterators) hooks_.invalidateIterators();
    // All fallible work has finished. Publish every claim/detachment before
    // the first callback. Never retain an external inventory across a callback.
    InventoryItem* externalItems = external != nullptr ? external->items : nullptr;
    if (external != nullptr) *external = {};
    for (auto& entry : batch.entries) {
        unlink(entry.node, entry.object);
        entry.object->data.inventory = {};
        if (entry.force) entry.object->flags &= ~OBJECT_NO_REMOVE;
        if (external != nullptr)
            entry.object->owner = nullptr;
        else
            for (const auto& parent : batch.entries)
                if (entry.object->owner == parent.object) {
                    entry.object->owner = nullptr;
                    break;
                }
    }
    if (beforeScripts) beforeScripts();
    for (size_t index : batch.order) {
        Entry& entry = batch.entries[index];
        // Script records can compact, disappear or be rebound during a child
        // callback. Only a fresh lookup of the captured incarnation is allowed.
        if (hooks_.epoch() == epoch && entry.scriptIdentity != 0
            && hooks_.scriptIdentity(entry.sid) == entry.scriptIdentity) {
            hooks_.destroyScript(entry.sid);
        }
        // Releasing an original binding is cleanup, not old-world execution.
        // An epoch change alone does not delete scripts. A reset/rebind does,
        // and its replacement must never be removed through the old SID.
        if (entry.scriptIdentity != 0 && hooks_.scriptIdentity(entry.sid) == entry.scriptIdentity)
            hooks_.releaseScript(entry.sid);
        if (entry.inventory.items != nullptr) hooks_.disposeInventory(entry.inventory.items);
        if (entry.node != nullptr) hooks_.disposeNode(entry.node);
        hooks_.disposeObject(entry.object);
        entry.complete = true;
    }
    if (externalItems != nullptr) hooks_.disposeInventory(externalItems);
    active_ = batch.previous;
    return 0;
}

int ObjectTeardown::erase(Object* object, const std::function<void()>& beforeScripts, Rect* rect,
    const std::function<int()>& beforeDetach)
{
    if (rect != nullptr) {
        *rect = {};
        if (registry_.handleFor(object).valid() && hooks_.bound) hooks_.bound(object, rect);
    }
    if (destroying(object)) return 0;
    Batch batch;
    try {
        if (object == nullptr || !gather(batch, object, false)) return -1;
    } catch (const std::bad_alloc&) {
        return -1;
    }
    if (beforeDetach && beforeDetach() != 0) return -1;
    return run(batch, nullptr, beforeScripts);
}

int ObjectTeardown::inventory(Inventory& inventory)
{
    Batch batch;
    try {
        if (inventory.length < 0 || inventory.capacity < inventory.length
            || (inventory.length != 0 && inventory.items == nullptr)) return -1;
        for (int i = 0; i < inventory.length; ++i)
            if (!gather(batch, inventory.items[i].item, true)) return -1;
    } catch (const std::bad_alloc&) {
        return -1;
    }
    return run(batch, &inventory, {});
}

int ObjectTeardown::all(const std::function<void()>& beforeScripts, Object* forceFirst, Object* forceSecond)
{
    Batch batch;
    try {
        for (size_t i = 0; i <= count_; ++i)
            for (auto* node = i == count_ ? floating_ : lists_[i]; node != nullptr; node = node->next)
                if (node->obj != nullptr) {
                    const bool force = node->obj == forceFirst || node->obj == forceSecond;
                    if ((force || (node->obj->flags & OBJECT_NO_REMOVE) == 0)
                        && !gather(batch, node->obj, force)) return -1;
                }
    } catch (const std::bad_alloc&) {
        return -1;
    }
    return run(batch, nullptr, beforeScripts);
}

int object_destroy_guarded(Object* object, const ObjectDestroyHooks& hooks)
{
    if (object == nullptr) return -1;
    if (hooks.destroying(object)) return 0;
    const uint64_t epoch = hooks.epoch();
    Object* owner = object->owner;
    const int elevation = object->elevation;
    Rect rect {};
    const int result = hooks.erase(object, &rect, [&] {
        if (owner != nullptr && hooks.removeInventory(owner, object) != 0) return -1;
        hooks.removeQueued(object);
        return 0;
    });
    if (result != 0) return -1;
    if (owner == nullptr && hooks.epoch() == epoch) hooks.refresh(&rect, elevation);
    return 0;
}

int object_transition_after_clear(const std::function<int()>& clear,
    const std::function<void()>& publish, const std::function<void()>& cancel)
{
    if (clear() != 0) {
        if (cancel) cancel();
        return OBJECT_TRANSITION_CANCELLED;
    }
    publish();
    return 0;
}

} // namespace fallout
