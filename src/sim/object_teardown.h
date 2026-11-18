#ifndef FALLOUT_SIM_OBJECT_TEARDOWN_H_
#define FALLOUT_SIM_OBJECT_TEARDOWN_H_

#include "game/object_types.h"
#include "object_handle.h"
#include "plib/gnw/rect.h"
#include <functional>
#include <vector>

namespace fallout {

// Resource hooks must not throw. Identity lookup and allocation admission are
// read-only preflight. Disposal only releases the supplied original allocation.
struct ObjectTeardownHooks {
    std::function<uint64_t()> epoch;
    std::function<uint64_t(int)> scriptIdentity;
    std::function<void(int)> destroyScript;
    std::function<void(int)> releaseScript;
    std::function<void(Object*)> disposeObject;
    std::function<void(ObjectListNode*)> disposeNode;
    std::function<void(InventoryItem*)> disposeInventory;
    std::function<bool()> allocationAllowed;
    std::function<void(Object*, Rect*)> bound;
    std::function<void()> invalidateIterators;
};

// One instance lives outside world lifecycle state. A stack-linked batch owns
// detached originals through callbacks/reset. Nested batches see its claims.
class ObjectTeardown {
public:
    ObjectTeardown(ObjectRegistry& registry, ObjectListNode** lists, size_t count,
        ObjectListNode*& floating, ObjectTeardownHooks hooks);
    bool destroying(const Object* object) const;
    // beforeDetach runs only after full preflight. Failure must leave the graph
    // unchanged; success must finish without scripts or other reentrant calls.
    int erase(Object* object, const std::function<void()>& beforeScripts = {}, Rect* rect = nullptr,
        const std::function<int()>& beforeDetach = {});
    int inventory(Inventory& inventory);
    int all(const std::function<void()>& beforeScripts = {}, Object* forceFirst = nullptr, Object* forceSecond = nullptr);

private:
    struct Entry {
        Object* object;
        ObjectHandle identity;
        Inventory inventory;
        ObjectListNode* node;
        int sid;
        uint64_t scriptIdentity;
        bool force;
        bool complete = false;
    };
    struct Batch {
        std::vector<Entry> entries;
        std::vector<size_t> order;
        Batch* previous = nullptr;
    };
    bool gather(Batch& batch, Object* object, bool force);
    ObjectListNode* findNode(Object* object) const;
    void unlink(ObjectListNode* node, Object* object);
    int run(Batch& batch, Inventory* external, const std::function<void()>& beforeScripts);
    bool allocationAllowed() const;
    ObjectRegistry& registry_;
    ObjectListNode** lists_;
    size_t count_;
    ObjectListNode*& floating_;
    ObjectTeardownHooks hooks_;
    Batch* active_ = nullptr;
};

struct ObjectDestroyHooks {
    std::function<uint64_t()> epoch;
    std::function<bool(const Object*)> destroying;
    std::function<int(Object*, Rect*, const std::function<int()>&)> erase;
    std::function<int(Object*, Object*)> removeInventory;
    std::function<void(Object*)> removeQueued;
    std::function<void(Rect*, int)> refresh;
};

int object_destroy_guarded(Object* object, const ObjectDestroyHooks& hooks);

inline constexpr int OBJECT_TRANSITION_CANCELLED = -2;

// Publish the new world state only after removal succeeds. The caller handles
// failure without falling through to its ordinary destructive error recovery.
// Cancellation cleanup completes before failure is returned to an outer loader.
int object_transition_after_clear(const std::function<int()>& clear,
    const std::function<void()>& publish,
    const std::function<void()>& cancel = {});

} // namespace fallout
#endif
