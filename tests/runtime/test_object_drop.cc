#include "test_harness.h"

#include <functional>
#include <memory>
#include <vector>

#include "sim/object_drop.h"
#include "sim/world_sim.h"

using namespace fallout;

namespace {
uint64_t epoch;
uint64_t currentEpoch() { return epoch; }
std::vector<int> calls;
std::function<void()> script;
std::function<void()> removeAction;
std::function<void()> connectAction;
bool overrideDrop, scriptExists;
int removeResult, connectResult;
Object* topOwner;
int connectedTile, connectedElevation, refreshedElevation;
int scriptId(Object* item, int* sid)
{
    calls.push_back(1);
    *sid = item->sid;
    return item->sid == -1 ? -1 : 0;
}
void runScript(int, Object*, Object*)
{
    calls.push_back(2);
    if (script) script();
}
bool readOverride(int, bool* value)
{
    calls.push_back(3);
    *value = overrideDrop;
    return scriptExists;
}
int removeItem(Object*, Object*)
{
    calls.push_back(4);
    if (removeAction) removeAction();
    return removeResult;
}
Object* topEnvironment(Object*)
{
    calls.push_back(5);
    return topOwner;
}
int connectItem(Object*, int tile, int elevation, Rect* rect)
{
    calls.push_back(6);
    connectedTile = tile;
    connectedElevation = elevation;
    *rect = { 1, 2, 3, 4 };
    if (connectAction) connectAction();
    return connectResult;
}
void refresh(Rect*, int elevation)
{
    calls.push_back(7);
    refreshedElevation = elevation;
}
const ObjectDropHooks hooks { scriptId, runScript, readOverride, removeItem, topEnvironment, connectItem, refresh };
struct Fixture {
    ObjectRegistry registry;
    Object actor {}, item {}, owner {};
    Fixture()
    {
        epoch = 20;
        calls.clear();
        script = removeAction = connectAction = {};
        overrideDrop = false;
        scriptExists = true;
        removeResult = connectResult = 0;
        topOwner = nullptr;
        connectedTile = connectedElevation = refreshedElevation = -1;
        actor.tile = 100;
        actor.elevation = 2;
        owner.tile = 200;
        owner.elevation = 1;
        item.sid = 50;
        registry.create(&actor);
        registry.create(&item);
        registry.create(&owner);
    }
    int run() { return object_drop_guarded(registry, &actor, &item, currentEpoch, hooks); }
    void reuse(Object& object)
    {
        registry.destroy(&object);
        object = {};
        registry.create(&object);
    }
};
} // namespace

TEST(object_drop_live_script_and_no_script_success_preserve_order)
{
    for (bool hasScript : { false, true }) {
        Fixture f;
        if (!hasScript) f.item.sid = -1;
        CHECK_EQ(f.run(), 0);
        CHECK_EQ(calls, (hasScript ? std::vector<int> { 1, 2, 3, 4, 5, 6, 7 } : std::vector<int> { 1, 4, 5, 6, 7 }));
        CHECK_EQ(connectedTile, 100);
        CHECK_EQ(connectedElevation, 2);
        CHECK_EQ(refreshedElevation, 2);
    }
}

TEST(object_drop_script_deletion_reuse_epoch_and_reset_cancel_before_override)
{
    for (int mutation = 0; mutation < 6; ++mutation) {
        Fixture f;
        script = [&] {
            switch (mutation) {
            case 0:
                f.registry.destroy(&f.actor);
                break;
            case 1:
                f.registry.destroy(&f.item);
                break;
            case 2:
                f.reuse(f.actor);
                break;
            case 3:
                f.reuse(f.item);
                break;
            case 4:
                ++epoch;
                break;
            case 5:
                ++epoch;
                f.item.sid = -1;
                break;
            }
        };
        CHECK_EQ(f.run(), -1);
        // In particular, the script record is never read after cancellation.
        CHECK_EQ(calls, (std::vector<int> { 1, 2 }));
    }
}

TEST(object_drop_override_and_script_error_preserve_returns)
{
    Fixture f;
    overrideDrop = true;
    CHECK_EQ(f.run(), 0);
    CHECK_EQ(calls, (std::vector<int> { 1, 2, 3 }));
    calls.clear();
    scriptExists = false;
    CHECK_EQ(f.run(), -1);
    CHECK_EQ(calls, (std::vector<int> { 1, 2, 3 }));
}

TEST(object_drop_override_does_not_mask_deleted_actor)
{
    Fixture f;
    overrideDrop = true;
    script = [&] { f.reuse(f.actor); };
    CHECK_EQ(f.run(), -1);
    CHECK_EQ(calls, (std::vector<int> { 1, 2 }));
}

TEST(object_drop_invalid_inputs_never_call_engine_seams)
{
    Fixture f;
    CHECK_EQ(object_drop_guarded(f.registry, &f.actor, nullptr, currentEpoch, hooks), -1);
    CHECK_EQ(object_drop_guarded(f.registry, nullptr, &f.item, currentEpoch, hooks), -1);
    f.registry.destroy(&f.item);
    CHECK_EQ(f.run(), -1);
    CHECK(calls.empty());
}

TEST(object_drop_owner_coordinates_and_remove_failure)
{
    Fixture f;
    topOwner = &f.owner;
    CHECK_EQ(f.run(), 0);
    CHECK_EQ(connectedTile, 200);
    CHECK_EQ(connectedElevation, 1);
    calls.clear();
    removeResult = -1;
    CHECK_EQ(f.run(), 0);
    CHECK_EQ(calls, (std::vector<int> { 1, 2, 3, 4 }));
}

TEST(object_drop_script_rebind_cannot_read_different_script_override)
{
    Fixture f;
    script = [&] { f.item.sid = 60; };
    CHECK_EQ(f.run(), -1);
    CHECK_EQ(calls, (std::vector<int> { 1, 2 }));
}

TEST(object_drop_resource_boundary_cancellation_preserves_partial_commit)
{
    for (bool atRemoval : { true, false }) {
        Fixture f;
        int committed = 0;
        auto cancel = [&] { ++committed; ++epoch; };
        if (atRemoval)
            removeAction = cancel;
        else
            connectAction = cancel;
        CHECK_EQ(f.run(), -1);
        CHECK_EQ(committed, 1);
        CHECK_EQ(calls, (atRemoval ? std::vector<int> { 1, 2, 3, 4 } : std::vector<int> { 1, 2, 3, 4, 5, 6 }));
    }
}

TEST(object_drop_connect_failure_keeps_return_without_uninitialized_refresh)
{
    Fixture f;
    connectResult = -1;
    CHECK_EQ(f.run(), 0);
    CHECK_EQ(calls, (std::vector<int> { 1, 2, 3, 4, 5, 6 }));
}

TEST(object_drop_real_world_reset_invalidate_exit_cancel_persistent_actor)
{
    for (auto transition : { world_sim_reset, world_sim_invalidate, world_sim_exit }) {
        world_sim_exit();
        CHECK(world_sim_init(WORLD_SIM_EXISTING, {}));
        Fixture f;
        const ObjectHandle actor = f.registry.handleFor(&f.actor);
        script = transition;
        CHECK_EQ(object_drop_guarded(f.registry, &f.actor, &f.item, world_sim_epoch, hooks), -1);
        CHECK_EQ(calls, (std::vector<int> { 1, 2 }));
        CHECK(f.registry.resolve(actor) == &f.actor);
        world_sim_exit();
    }
}

TEST(object_drop_script_frees_heap_item_before_continuation)
{
    Fixture f;
    auto item = std::make_unique<Object>();
    item->sid = 50;
    Object* raw = item.get();
    f.registry.create(raw);
    script = [&] { f.registry.destroy(item.get()); item.reset(); };
    CHECK_EQ(object_drop_guarded(f.registry, &f.actor, raw, currentEpoch, hooks), -1);
    CHECK(!item);
    CHECK_EQ(calls, (std::vector<int> { 1, 2 }));
}
