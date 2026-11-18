#include "sim/object_teardown.h"
#include "sim/world_sim.h"
#include "test_harness.h"
#include <algorithm>
#include <map>
#include <memory>

using namespace fallout;
namespace {
struct Rig {
    ObjectRegistry registry;
    ObjectListNode* lists[2] {};
    ObjectListNode* floating = nullptr;
    uint64_t epoch = 1;
    int budget = -1;
    bool realEpoch = false;
    std::map<int, uint64_t> scripts;
    std::map<int, std::function<void()>> callbacks;
    std::vector<int> destroyed, freed, released;
    int arraysFreed = 0, nodesFreed = 0;
    int iteratorInvalidations = 0;
    ObjectListNode* iterator = nullptr;
    std::function<bool(Object*)> recycle;
    ObjectTeardown teardown;
    Rig()
        : teardown(registry, lists, 2, floating, hooks())
    {
    }
    ObjectTeardownHooks hooks()
    {
        ObjectTeardownHooks h;
        h.epoch = [&] { return realEpoch ? world_sim_epoch() : epoch; };
        h.scriptIdentity = [&](int sid) {
            auto i = scripts.find(sid);
            return i == scripts.end() ? 0 : i->second;
        };
        h.destroyScript = [&](int sid) {
            destroyed.push_back(sid);
            auto callback = callbacks[sid];
            if (callback) callback();
        };
        h.releaseScript = [&](int sid) { released.push_back(sid); scripts.erase(sid); };
        h.disposeObject = [&](Object* obj) {
            freed.push_back(obj->id);
            registry.destroy(obj);
            if (!recycle || !recycle(obj)) delete obj;
        };
        h.disposeNode = [&](ObjectListNode* node) { ++nodesFreed; delete node; };
        h.disposeInventory = [&](InventoryItem* items) { ++arraysFreed; delete[] items; };
        h.allocationAllowed = [&] {
            if (budget < 0) return true;
            if (budget == 0) return false;
            --budget;
            return true;
        };
        h.bound = [](Object* obj, Rect* rect) { *rect = { obj->id, 2, 30, 40 }; };
        h.invalidateIterators = [&] { ++iteratorInvalidations; iterator = nullptr; };
        return h;
    }
    Object* object(int id, int list = -1)
    {
        auto* obj = new Object {};
        obj->id = obj->sid = id;
        obj->tile = list;
        registry.create(obj);
        scripts[id] = id + 1;
        if (list >= 0) lists[list] = new ObjectListNode { obj, lists[list] };
        return obj;
    }
    void children(Object* parent, std::initializer_list<Object*> children)
    {
        auto& inv = parent->data.inventory;
        inv.length = inv.capacity = children.size();
        inv.items = new InventoryItem[children.size()];
        int i = 0;
        for (auto* child : children) {
            inv.items[i++] = { child, 1 };
            child->owner = parent;
        }
    }
};
}

TEST(object_teardown_detaches_graph_and_repeated_destroy_is_noop)
{
    Rig r;
    auto* parent = r.object(1, 0);
    auto* child = r.object(2);
    r.children(parent, { child });
    r.callbacks[2] = [&] {
        CHECK(r.lists[0] == nullptr);
        CHECK(parent->data.inventory.items == nullptr);
        CHECK(child->owner == nullptr);
        CHECK(r.registry.handleFor(parent).valid());
        CHECK(r.teardown.erase(child) == 0);
        CHECK(r.teardown.erase(parent) == 0);
    };
    CHECK(r.teardown.erase(parent) == 0);
    CHECK(r.destroyed == std::vector<int>({ 2, 1 }));
    CHECK(r.freed == std::vector<int>({ 2, 1 }));
    CHECK(r.nodesFreed == 1 && r.arraysFreed == 1);
    CHECK(r.registry.liveCount() == 0);
}

TEST(object_teardown_preflight_failure_leaves_entire_graph_unchanged)
{
    for (int budget = 0; budget < 4; ++budget) {
        Rig r;
        auto* parent = r.object(1, 0);
        auto* child = r.object(2);
        r.children(parent, { child });
        auto* items = parent->data.inventory.items;
        auto* node = r.lists[0];
        r.budget = budget;
        CHECK(r.teardown.erase(parent) == -1);
        CHECK(r.lists[0] == node && parent->data.inventory.items == items);
        CHECK(child->owner == parent && r.registry.liveCount() == 2);
        CHECK(r.destroyed.empty() && r.freed.empty());
        r.budget = -1;
        CHECK(r.teardown.erase(parent) == 0);
    }
}

TEST(object_teardown_epoch_cancels_scripts_but_releases_only_original_bindings)
{
    for (bool replacement : { false, true }) {
        Rig r;
        auto* parent = r.object(1, 0);
        auto* child = r.object(2);
        r.children(parent, { child });
        r.callbacks[2] = [&] {
            ++r.epoch;
            if (replacement) r.scripts[1] = 999;
        };
        CHECK(r.teardown.erase(parent) == 0);
        CHECK(r.destroyed == std::vector<int>({ 2 }));
        CHECK(r.freed == std::vector<int>({ 2, 1 }));
        CHECK(r.scripts.count(2) == 0);
        CHECK(r.scripts.count(1) == (replacement ? 1u : 0u));
        if (replacement) CHECK(r.scripts[1] == 999);
    }
}

TEST(object_teardown_sibling_and_parent_recursion_do_not_repeat_claims)
{
    Rig r;
    auto* parent = r.object(1, 0);
    auto* child = r.object(2);
    auto* sibling = r.object(3);
    r.children(parent, { child, sibling });
    r.callbacks[2] = [&] {
        CHECK(r.teardown.erase(sibling) == 0);
        CHECK(r.teardown.erase(parent) == 0);
        CHECK(r.teardown.inventory(parent->data.inventory) == 0);
    };
    CHECK(r.teardown.erase(parent) == 0);
    CHECK(r.destroyed == std::vector<int>({ 2, 3, 1 }));
    CHECK(r.freed == std::vector<int>({ 2, 3, 1 }));
    CHECK(r.arraysFreed == 1);
}

TEST(object_teardown_predecessor_deletion_uses_detached_list_not_saved_links)
{
    Rig r;
    auto* tail = r.object(1, 0);
    auto* head = r.object(2, 0);
    auto* other = r.object(3, 1);
    r.callbacks[1] = [&] {
        CHECK(r.lists[0]->obj == head);
        CHECK(r.teardown.erase(head) == 0);
        CHECK(r.teardown.erase(other) == 0);
    };
    CHECK(r.teardown.erase(tail) == 0);
    CHECK(r.destroyed == std::vector<int>({ 1, 2, 3 }));
    CHECK(r.lists[0] == nullptr && r.lists[1] == nullptr);
    CHECK(r.nodesFreed == 3 && r.registry.liveCount() == 0);
}

TEST(object_teardown_inventory_detaches_array_before_owner_can_be_freed)
{
    Rig r;
    auto* parent = r.object(1, 0);
    auto* child = r.object(2);
    r.children(parent, { child });
    r.callbacks[2] = [&] {
        CHECK(parent->data.inventory.items == nullptr);
        CHECK(r.teardown.erase(parent) == 0);
    };
    CHECK(r.teardown.inventory(parent->data.inventory) == 0);
    CHECK(r.freed == std::vector<int>({ 1, 2 }));
    CHECK(r.arraysFreed == 1 && r.registry.liveCount() == 0);
}

TEST(object_teardown_reset_owns_originals_and_preserves_new_world_lists)
{
    Rig r;
    auto* parent = r.object(1, 0);
    auto* child = r.object(2);
    auto* sibling = r.object(3, 1);
    r.children(parent, { child });
    Object* replacement = nullptr;
    r.callbacks[2] = [&] {
        ++r.epoch;
        CHECK(r.teardown.all([&] { r.scripts.clear(); }) == 0);
        replacement = r.object(4, 0);
        r.scripts[1] = 999;
    };
    CHECK(r.teardown.erase(parent) == 0);
    CHECK(r.destroyed == std::vector<int>({ 2 }));
    CHECK(r.freed == std::vector<int>({ 3, 2, 1 }));
    CHECK(r.lists[0]->obj == replacement && r.lists[1] == nullptr);
    CHECK(r.scripts[1] == 999 && r.registry.liveCount() == 1);
    CHECK(r.teardown.erase(replacement) == 0);
    (void)sibling;
}

TEST(object_teardown_nested_allocation_failure_does_not_detach_other_graph)
{
    Rig r;
    auto* first = r.object(1, 0);
    auto* other = r.object(2, 1);
    auto* child = r.object(3);
    r.children(other, { child });
    auto* originalItems = other->data.inventory.items;
    r.callbacks[1] = [&] {
        r.budget = 1;
        CHECK(r.teardown.erase(other) == -1);
        CHECK(r.lists[1]->obj == other);
        CHECK(other->data.inventory.items == originalItems);
        CHECK(child->owner == other);
        r.budget = -1;
    };
    CHECK(r.teardown.erase(first) == 0);
    CHECK(r.destroyed == std::vector<int>({ 1 }));
    CHECK(r.teardown.erase(other) == 0);
}

TEST(object_teardown_bulk_preflight_and_legacy_script_removal_order)
{
    Rig r;
    auto* protectedObject = r.object(1, 0);
    protectedObject->flags |= OBJECT_NO_REMOVE;
    r.object(2, 0);
    r.object(3, 1);
    auto* head = r.lists[0];
    r.budget = 2;
    int clears = 0;
    CHECK(r.teardown.all([&] { ++clears; r.scripts.clear(); }) == -1);
    CHECK(clears == 0 && r.lists[0] == head && r.registry.liveCount() == 3);
    r.budget = -1;
    CHECK(r.teardown.all([&] {
        ++clears;
        CHECK(r.lists[0]->obj == protectedObject && r.lists[1] == nullptr);
        r.scripts.clear();
    }) == 0);
    CHECK(clears == 1 && r.destroyed.empty());
    CHECK(r.freed == std::vector<int>({ 2, 3 }));
    protectedObject->flags &= ~OBJECT_NO_REMOVE;
    CHECK(r.teardown.erase(protectedObject) == 0);
}

TEST(object_teardown_same_address_new_generation_is_not_an_old_claim)
{
    Rig r;
    auto* parent = r.object(1, 0);
    auto* child = r.object(2);
    const auto old = r.registry.handleFor(child);
    r.children(parent, { child });
    Object* replacement = nullptr;
    r.recycle = [&](Object* object) {
        if (object->id != 2) return false;
        // Reuse this heap storage only after the old allocation is retired.
        *object = {};
        object->id = object->sid = 3;
        object->tile = 1;
        replacement = object;
        r.registry.create(object);
        r.scripts[3] = 4;
        r.lists[1] = new ObjectListNode { object, nullptr };
        return true;
    };
    r.callbacks[1] = [&] {
        CHECK(r.registry.resolve(old) == nullptr);
        CHECK(!r.teardown.destroying(replacement));
        CHECK(r.teardown.erase(replacement) == 0);
    };
    CHECK(r.teardown.erase(parent) == 0);
    CHECK(r.destroyed == std::vector<int>({ 2, 1, 3 }));
    CHECK(r.freed == std::vector<int>({ 2, 3, 1 }));
    CHECK(r.registry.liveCount() == 0);
}

TEST(object_teardown_noop_and_preflight_failure_initialize_requested_bounds)
{
    Rig r;
    auto* object = r.object(1, 0);
    r.callbacks[1] = [&] {
        Rect rect { -99, -99, -99, -99 };
        CHECK(r.teardown.erase(object, {}, &rect) == 0);
        CHECK(rect.ulx == 1 && rect.lry == 40);
    };
    r.budget = 0;
    Rect rect { -99, -99, -99, -99 };
    CHECK(r.teardown.erase(object, {}, &rect) == -1);
    CHECK(rect.ulx == 1 && rect.lry == 40);
    CHECK(r.lists[0]->obj == object && r.registry.handleFor(object).valid());
    r.budget = -1;
    CHECK(r.teardown.erase(object) == 0);
}

TEST(object_teardown_real_lifecycle_cancels_execution_not_original_release)
{
    for (auto change : { world_sim_reset, world_sim_invalidate, world_sim_exit }) {
        world_sim_exit();
        CHECK(world_sim_init(WORLD_SIM_EXISTING, {}));
        Rig r;
        r.realEpoch = true;
        auto* parent = r.object(1, 0);
        auto* child = r.object(2);
        r.children(parent, { child });
        r.callbacks[2] = change;
        CHECK(r.teardown.erase(parent) == 0);
        CHECK(r.destroyed == std::vector<int>({ 2 }));
        CHECK(r.released == std::vector<int>({ 2, 1 }));
        CHECK(r.freed == std::vector<int>({ 2, 1 }));
        CHECK(r.registry.liveCount() == 0);
        world_sim_exit();
    }
}

TEST(object_teardown_inventory_preflight_preserves_array_and_protected_child_on_failure)
{
    Rig r;
    auto* parent = r.object(1, 0);
    auto* child = r.object(2);
    r.children(parent, { child });
    child->flags |= OBJECT_NO_REMOVE;
    auto* items = parent->data.inventory.items;
    r.budget = 1;
    CHECK(r.teardown.inventory(parent->data.inventory) == -1);
    CHECK(parent->data.inventory.items == items && parent->data.inventory.length == 1);
    CHECK((child->flags & OBJECT_NO_REMOVE) != 0 && child->owner == parent);
    r.budget = -1;
    r.callbacks[2] = [&] {
        CHECK(child->owner == nullptr);
        CHECK((child->flags & OBJECT_NO_REMOVE) == 0);
    };
    CHECK(r.teardown.inventory(parent->data.inventory) == 0);
    CHECK(r.teardown.erase(parent) == 0);
}

TEST(object_teardown_invalidates_cursors_before_detach_callbacks_or_free)
{
    Rig r;
    auto* object = r.object(1);
    r.floating = new ObjectListNode { object, nullptr };
    r.iterator = r.floating;
    r.budget = 0;
    CHECK(r.teardown.erase(object) == -1);
    CHECK(r.iterator == r.floating && r.iteratorInvalidations == 0);
    r.budget = -1;
    r.callbacks[1] = [&] {
        CHECK(r.iterator == nullptr && r.iteratorInvalidations == 1);
        CHECK(r.floating == nullptr);
        CHECK(r.teardown.erase(object) == 0);
        CHECK(r.iteratorInvalidations == 1);
    };
    CHECK(r.teardown.erase(object) == 0);
    CHECK(r.nodesFreed == 1 && r.registry.liveCount() == 0);
}

TEST(object_teardown_transition_failure_does_not_publish_or_enter_new_world)
{
    Rig r;
    auto* original = r.object(1, 0);
    const auto identity = r.registry.handleFor(original);
    struct Header {
        int map;
        int script;
    } live { 7, 19 }, incoming { 8, -1 };
    int loads = 0;
    r.budget = 0;
    const auto transition = [&] {
        return object_transition_after_clear([&] { return r.teardown.all([&] { r.scripts.clear(); }); }, [&] {
            live = incoming;
            ++loads; });
    };
    CHECK(transition() == OBJECT_TRANSITION_CANCELLED);
    CHECK(live.map == 7 && live.script == 19);
    CHECK(loads == 0);
    CHECK(r.lists[0]->obj == original);
    CHECK(r.registry.resolve(identity) == original);
    CHECK(r.scripts.count(1) == 1);
    CHECK(r.freed.empty());
    r.budget = -1;
    CHECK(transition() == 0);
    CHECK(live.map == 8 && live.script == -1);
    CHECK(loads == 1);
    CHECK(r.lists[0] == nullptr);
}

TEST(object_teardown_exit_preflight_preserves_protected_flags_on_failure)
{
    Rig r;
    auto* actor = r.object(1, 0);
    auto* egg = r.object(2, 1);
    auto* otherProtected = r.object(3, 0);
    actor->flags = egg->flags = otherProtected->flags = OBJECT_NO_REMOVE;
    r.budget = 2;
    CHECK(r.teardown.all({}, actor, egg) == -1);
    CHECK(actor->flags == OBJECT_NO_REMOVE);
    CHECK(egg->flags == OBJECT_NO_REMOVE);
    CHECK(r.freed.empty());
    r.budget = -1;
    CHECK(r.teardown.all({}, actor, egg) == 0);
    CHECK(r.freed == std::vector<int>({ 1, 2 }));
    CHECK(otherProtected->flags == OBJECT_NO_REMOVE);
    CHECK(r.teardown.erase(otherProtected, {}, nullptr, [&] { return -1; }) == -1);
    otherProtected->flags = 0;
    CHECK(r.teardown.erase(otherProtected) == 0);
}

namespace {
struct DestroyRig : Rig {
    int removals = 0, queued = 1, refreshes = 0;
    bool failRemoval = false;
    ObjectDestroyHooks destroyHooks()
    {
        ObjectDestroyHooks h;
        h.epoch = [&] { return epoch; };
        h.destroying = [&](const Object* obj) { return teardown.destroying(obj); };
        h.erase = [&](Object* obj, Rect* rect, const std::function<int()>& commit) {
            return teardown.erase(obj, {}, rect, commit);
        };
        h.removeInventory = [&](Object* owner, Object* obj) {
            ++removals;
            if (failRemoval) return -1;
            auto& inv = owner->data.inventory;
            CHECK(inv.length == 1 && inv.items[0].item == obj);
            inv.length = 0;
            obj->owner = nullptr;
            return 0;
        };
        h.removeQueued = [&](Object*) { CHECK(queued == 1); queued = 0; };
        h.refresh = [&](Rect* rect, int) { CHECK(rect->lrx == 30); ++refreshes; };
        return h;
    }
};
}

TEST(object_teardown_destroy_wrapper_preflights_before_inventory_and_queue)
{
    DestroyRig r;
    auto* owner = r.object(1, 0);
    auto* obj = r.object(2);
    auto* child = r.object(3);
    r.children(owner, { obj });
    r.children(obj, { child });
    auto hooks = r.destroyHooks();
    // The root fits but its child does not. Commit must wait for the whole graph.
    r.budget = 1;
    CHECK(object_destroy_guarded(obj, hooks) == -1);
    CHECK(r.removals == 0 && r.queued == 1 && r.refreshes == 0);
    CHECK(owner->data.inventory.length == 1);
    CHECK(obj->owner == owner && obj->data.inventory.items[0].item == child);
    CHECK(r.freed.empty());
    r.budget = -1;
    r.callbacks[3] = [&] {
        CHECK(owner->data.inventory.length == 0);
        CHECK(r.queued == 0);
        CHECK(object_destroy_guarded(obj, hooks) == 0);
        CHECK(r.removals == 1);
    };
    CHECK(object_destroy_guarded(obj, hooks) == 0);
    CHECK(r.removals == 1 && r.queued == 0 && r.refreshes == 0);
    CHECK(r.freed == std::vector<int>({ 3, 2 }));
    CHECK(r.teardown.erase(owner) == 0);
}

TEST(object_teardown_destroy_wrapper_inventory_failure_leaves_queue_and_graph)
{
    DestroyRig r;
    auto* owner = r.object(1, 0);
    auto* obj = r.object(2);
    r.children(owner, { obj });
    r.failRemoval = true;
    CHECK(object_destroy_guarded(obj, r.destroyHooks()) == -1);
    CHECK(r.removals == 1 && r.queued == 1 && r.refreshes == 0);
    CHECK(owner->data.inventory.length == 1 && obj->owner == owner);
    CHECK(r.iteratorInvalidations == 0);
    CHECK(r.freed.empty() && r.destroyed.empty());
    CHECK(r.teardown.erase(owner) == 0);
}

TEST(object_teardown_destroy_wrapper_refreshes_only_successful_current_world)
{
    DestroyRig r;
    auto* obj = r.object(1, 0);
    auto hooks = r.destroyHooks();
    r.budget = 0;
    CHECK(object_destroy_guarded(obj, hooks) == -1);
    CHECK(r.refreshes == 0 && r.queued == 1);
    r.budget = -1;
    CHECK(object_destroy_guarded(obj, hooks) == 0);
    CHECK(r.refreshes == 1 && r.queued == 0);
    obj = r.object(2, 0);
    r.queued = 1;
    r.callbacks[2] = [&] { ++r.epoch; };
    CHECK(object_destroy_guarded(obj, hooks) == 0);
    CHECK(r.refreshes == 1);
}

TEST(object_teardown_destroy_wrapper_keeps_stack_replacement_outside_captured_batch)
{
    DestroyRig r;
    auto* owner = r.object(1, 0);
    auto* original = r.object(2);
    auto* remaining = r.object(3);
    r.children(owner, { original });
    owner->data.inventory.items[0].quantity = 3;
    auto hooks = r.destroyHooks();
    hooks.removeInventory = [&](Object* actor, Object* item) {
        CHECK(actor == owner && item == original);
        auto& entry = actor->data.inventory.items[0];
        CHECK(entry.quantity == 3 && entry.item == original);
        // The native item_remove_mult split publishes its newly allocated
        // representative here. No later teardown allocation may fail.
        entry.item = remaining;
        --entry.quantity;
        remaining->owner = actor;
        item->owner = nullptr;
        r.budget = 0;
        return 0;
    };
    CHECK(object_destroy_guarded(original, hooks) == 0);
    CHECK(r.freed == std::vector<int>({ 2 }));
    CHECK(owner->data.inventory.items[0].item == remaining);
    CHECK(owner->data.inventory.items[0].quantity == 2);
    CHECK(r.registry.handleFor(remaining).valid());
    r.budget = -1;
    CHECK(r.teardown.erase(owner) == 0);
}

TEST(object_teardown_cancel_recovers_staged_party_before_return_and_allows_next_prep)
{
    Rig r;
    auto* original = r.object(1, 0);
    const auto identity = r.registry.handleFor(original);
    bool partyPrepped = true;
    r.scripts.erase(1); // Party preparation retains the script privately.
    int recoveries = 0, published = 0;
    int liveHeader = 7;
    const int savedHeader = liveHeader;
    liveHeader = 8; // Save metadata was committed before teardown preparation.
    r.budget = 0;
    const int result = object_transition_after_clear([&] { return r.teardown.all(); }, [&] { ++published; }, [&] {
        liveHeader = savedHeader;
        CHECK(partyPrepped);
        CHECK(r.registry.resolve(identity) == original);
        CHECK(r.lists[0]->obj == original);
        r.scripts[1] = 2;
        partyPrepped = false;
        ++recoveries; });
    CHECK(result == OBJECT_TRANSITION_CANCELLED);
    CHECK(recoveries == 1 && published == 0);
    CHECK(liveHeader == savedHeader);
    CHECK(!partyPrepped && r.scripts.count(1) == 1);
    CHECK(r.freed.empty());
    // A subsequent preparation is admitted. Successful removal must not also
    // call cancellation recovery, whose ownership passes to the load path.
    partyPrepped = true;
    r.scripts.erase(1);
    r.budget = -1;
    CHECK(object_transition_after_clear([&] { return r.teardown.all(); }, [&] { ++published; }, [&] { ++recoveries; }) == 0);
    CHECK(published == 1 && recoveries == 1 && partyPrepped);
}
