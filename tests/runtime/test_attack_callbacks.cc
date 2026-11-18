#include "test_harness.h"

#include <functional>
#include <vector>

#include "game/anim.h"
#include "sim/attack_callbacks.h"

using namespace fallout;

namespace {
uint64_t epoch;
uint64_t liveEpoch() { return epoch; }
ObjectRegistry* registry;
std::vector<int> calls;
std::function<void()> onDrop;
std::function<void()> onDestroy;
std::function<void()> onBound;
std::function<void()> onDeathDrop;
std::function<void()> onFlatten;
std::function<void()> onOutline;
int refreshedElevation;
Rect refreshedRect;
int drop(Object*, Object*)
{
    calls.push_back(1);
    if (onDrop) onDrop();
    return 0;
}
int destroy(Object* weapon)
{
    calls.push_back(2);
    if (onDestroy) {
        onDestroy();
        return 0;
    }
    registry->destroy(weapon);
    *weapon = {};
    registry->create(weapon);
    return 0;
}
void bound(Object*, Rect* rect)
{
    calls.push_back(3);
    *rect = { 2, 3, 4, 5 };
    if (onBound) onBound();
}
int flatten(Object*, Rect* rect)
{
    calls.push_back(4);
    *rect = { 0, 1, 4, 5 };
    if (onFlatten) onFlatten();
    return 0;
}
int outline(Object*, Rect* rect)
{
    calls.push_back(5);
    *rect = { 2, 3, 8, 9 };
    if (onOutline) onOutline();
    return 0;
}
int dropAll(Object*, int)
{
    calls.push_back(6);
    if (onDeathDrop) onDeathDrop();
    return -1;
}
void refresh(Rect* rect, int elevation)
{
    calls.push_back(7);
    refreshedElevation = elevation;
    refreshedRect = *rect;
}
const AttackCallbackHooks hooks { drop, destroy, bound, flatten, outline, dropAll, refresh };

struct Fixture {
    ObjectRegistry objects;
    Object actor {}, weapon {};
    AnimationCallbacks bank;
    Fixture()
    {
        epoch = 10;
        registry = &objects;
        calls.clear();
        onDrop = onDestroy = onBound = onDeathDrop = onFlatten = onOutline = {};
        objects.create(&actor);
        objects.create(&weapon);
        actor.pid = 0x1000000;
        actor.tile = 100;
        actor.elevation = 2;
        bank.begin(1);
    }
    std::unique_ptr<AnimationPayload> payload(AttackCallbackKind kind, int anim = ANIM_EXPLODED_TO_NOTHING)
    {
        return std::make_unique<AttackAnimationPayload>(objects, object_ref_make(objects, &actor, epoch), object_ref_make(objects, &weapon, epoch), kind, anim, liveEpoch, hooks);
    }
    void reuse(Object& object)
    {
        objects.destroy(&object);
        object = {};
        objects.create(&object);
    }
};
} // namespace

TEST(attack_callbacks_forced_drop_destroy_death_order_and_once)
{
    Fixture f;
    CHECK(f.bank.add(1, 0, f.payload(AttackCallbackKind::Drop), true));
    CHECK(f.bank.add(1, 1, f.payload(AttackCallbackKind::Destroy), true));
    CHECK(f.bank.add(1, 2, f.payload(AttackCallbackKind::Death), true));
    for (int i = 0; i < 3; ++i)
        CHECK(f.bank.dispatch(1, i, false).current);
    CHECK_EQ(calls, (std::vector<int> { 1, 2, 3, 4, 5, 6, 7 }));
    for (int i = 0; i < 3; ++i)
        f.bank.dispatch(1, i, false);
    CHECK_EQ(calls.size(), 7u);
    CHECK_EQ(refreshedElevation, 2);
    CHECK_EQ(refreshedRect.ulx, 0);
    CHECK_EQ(refreshedRect.lry, 9);
    CHECK((f.actor.flags & OBJECT_NO_BLOCK) != 0);
}

TEST(attack_callbacks_stale_actor_weapon_and_epoch_skip)
{
    for (auto kind : { AttackCallbackKind::Drop, AttackCallbackKind::Destroy, AttackCallbackKind::Death }) {
        Fixture f;
        CHECK(f.bank.add(1, 0, f.payload(kind), true));
        f.reuse(f.actor);
        f.bank.dispatch(1, 0, false);
        CHECK(calls.empty());
    }
    for (auto kind : { AttackCallbackKind::Drop, AttackCallbackKind::Destroy }) {
        Fixture f;
        CHECK(f.bank.add(1, 0, f.payload(kind), true));
        f.reuse(f.weapon);
        f.bank.dispatch(1, 0, false);
        CHECK(calls.empty());
    }
    for (auto kind : { AttackCallbackKind::Drop, AttackCallbackKind::Destroy, AttackCallbackKind::Death }) {
        Fixture f;
        CHECK(f.bank.add(1, 0, f.payload(kind), true));
        ++epoch;
        f.bank.dispatch(1, 0, false);
        CHECK(calls.empty());
    }
}

TEST(attack_callbacks_death_revalidates_after_drop_all)
{
    for (bool changeEpoch : { false, true }) {
        Fixture f;
        CHECK(f.bank.add(1, 0, f.payload(AttackCallbackKind::Death), true));
        onDeathDrop = [&] { if (changeEpoch) ++epoch; else f.reuse(f.actor); };
        f.bank.dispatch(1, 0, false);
        CHECK_EQ(calls, (std::vector<int> { 3, 4, 5, 6 }));
    }
}

TEST(attack_callbacks_death_revalidates_other_resource_seams)
{
    for (bool atFlatten : { true, false }) {
        Fixture f;
        CHECK(f.bank.add(1, 0, f.payload(AttackCallbackKind::Death), true));
        auto invalidate = [&] { f.reuse(f.actor); };
        if (atFlatten)
            onFlatten = invalidate;
        else
            onOutline = invalidate;
        f.bank.dispatch(1, 0, false);
        CHECK_EQ(calls, (atFlatten ? std::vector<int> { 3, 4 } : std::vector<int> { 3, 4, 5 }));
    }
}

TEST(attack_callbacks_drop_reset_reentry_cannot_repeat_or_continue_replacement)
{
    Fixture f;
    CHECK(f.bank.add(1, 0, f.payload(AttackCallbackKind::Drop), true));
    onDrop = [&] {
        f.bank.dispatch(1, 0, false);
        f.bank.clear();
        f.bank.begin(2);
        CHECK(f.bank.add(2, 0, f.payload(AttackCallbackKind::Destroy), true));
    };
    CHECK(!f.bank.dispatch(1, 0, false).current);
    CHECK_EQ(calls, (std::vector<int> { 1 }));
    f.bank.dispatch(1, 0, false);
    CHECK_EQ(calls.size(), 1u);
    CHECK(f.bank.dispatch(2, 0, false).current);
    CHECK_EQ(calls, (std::vector<int> { 1, 2 }));
}

TEST(attack_callbacks_death_scalar_and_pid_policy_preserved)
{
    Fixture f;
    CHECK(f.bank.add(1, 0, f.payload(AttackCallbackKind::Death, ANIM_FALL_BACK), true));
    // Selected death anim is a scalar snapshot, not reread from actor fid.
    f.actor.fid = ANIM_EXPLODED_TO_NOTHING << 16;
    f.bank.dispatch(1, 0, false);
    CHECK_EQ(calls, (std::vector<int> { 3, 4, 5, 7 }));
    calls.clear();
    f.actor.pid = 16777265;
    CHECK(f.bank.add(1, 1, f.payload(AttackCallbackKind::Death), true));
    f.bank.dispatch(1, 1, false);
    CHECK_EQ(calls, (std::vector<int> { 3, 5, 7 }));
}

TEST(attack_callbacks_each_forced_kind_claims_before_reset_reentry)
{
    for (auto kind : { AttackCallbackKind::Drop, AttackCallbackKind::Destroy, AttackCallbackKind::Death }) {
        Fixture f;
        int delivered = 0;
        CHECK(f.bank.add(1, 0, f.payload(kind), true));
        auto reset = [&] {
            ++delivered;
            f.bank.dispatch(1, 0, false);
            f.bank.clear();
            f.bank.begin(2);
        };
        if (kind == AttackCallbackKind::Drop)
            onDrop = reset;
        else if (kind == AttackCallbackKind::Destroy)
            onDestroy = reset;
        else
            onDeathDrop = reset;
        CHECK(!f.bank.dispatch(1, 0, false).current);
        f.bank.dispatch(1, 0, false);
        CHECK_EQ(delivered, 1);
    }
}

TEST(attack_callbacks_destroy_consumes_heap_weapon_without_later_access)
{
    Fixture f;
    auto weapon = std::make_unique<Object>();
    f.objects.create(weapon.get());
    const ObjectRef ref = object_ref_make(f.objects, weapon.get(), epoch);
    auto payload = std::make_unique<AttackAnimationPayload>(f.objects, object_ref_make(f.objects, &f.actor, epoch), ref, AttackCallbackKind::Destroy, 0, liveEpoch, hooks);
    CHECK(f.bank.add(1, 0, std::move(payload), true));
    onDestroy = [&] { f.objects.destroy(weapon.get()); weapon.reset(); };
    CHECK(f.bank.dispatch(1, 0, false).current);
    CHECK(!weapon);
    CHECK(f.objects.resolve(ref.handle) == nullptr);
    CHECK_EQ(calls, (std::vector<int> { 2 }));
}

TEST(attack_callbacks_null_required_weapon_skips_but_death_needs_only_actor)
{
    Fixture f;
    const ObjectRef actor = object_ref_make(f.objects, &f.actor, epoch);
    for (auto kind : { AttackCallbackKind::Drop, AttackCallbackKind::Destroy }) {
        AttackAnimationPayload payload(f.objects, actor, {}, kind, 0, liveEpoch, hooks);
        CHECK_EQ(payload.invoke(), 0);
        CHECK(calls.empty());
    }
    AttackAnimationPayload death(f.objects, actor, {}, AttackCallbackKind::Death, ANIM_FALL_BACK, liveEpoch, hooks);
    CHECK_EQ(death.invoke(), 0);
    CHECK_EQ(calls, (std::vector<int> { 3, 4, 5, 7 }));
}
