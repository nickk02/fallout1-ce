#include "game/object_types.h"
#include "sim/movement_owner.h"
#include "test_harness.h"
#include <limits>
using namespace fallout;

TEST(movement_owner_legacy_failure_yields_without_pose_and_late_stop_is_stale)
{
    ObjectRegistry registry;
    Object actor {};
    registry.create(&actor);
    MovementOwner owner;
    auto old = owner.acquire(registry, &actor, 1, false);
    CHECK(old != 0);
    CHECK(owner.yield(&actor)); // Production registration yields before success is known.
    CHECK(owner.release(old, registry, 1, &actor) == nullptr);
    CHECK(owner.acquire(registry, &actor, 1, true) == 0);
    auto next = owner.acquire(registry, &actor, 1, false);
    CHECK(next != old);
    CHECK(owner.release(old, registry, 1, &actor) == nullptr);
    CHECK(owner.resolve(next, registry, 1, &actor) == &actor);
}

TEST(movement_owner_callback_reuse_epoch_and_player_changes_reject)
{
    for (int mutation = 0; mutation < 3; mutation++) {
        ObjectRegistry registry;
        Object actor {}, replacement {};
        registry.create(&actor);
        registry.create(&replacement);
        MovementOwner owner;
        auto token = owner.acquire(registry, &actor, 4, false);
        uint64_t epoch = 4;
        Object* player = &actor;
        if (mutation == 0) {
            registry.destroy(&actor);
            registry.create(&actor);
        }
        if (mutation == 1) epoch++;
        if (mutation == 2) player = &replacement;
        CHECK(owner.resolve(token, registry, epoch, player) == nullptr);
        CHECK(owner.release(token, registry, epoch, player) == nullptr);
    }
}

TEST(movement_owner_transfer_neutral_and_overflow)
{
    ObjectRegistry registry;
    Object actor {};
    registry.create(&actor);
    MovementOwner owner(std::numeric_limits<uint64_t>::max());
    auto token = owner.acquire(registry, &actor, 1, false);
    CHECK(token != 0);
    // Goto-to-stick transfers the same ownership; neutral releases it once.
    CHECK(owner.resolve(token, registry, 1, &actor) == &actor);
    CHECK(owner.release(token, registry, 1, &actor) == &actor);
    CHECK(owner.release(token, registry, 1, &actor) == nullptr);
    CHECK(owner.acquire(registry, &actor, 1, false) == 0);
}

TEST(movement_owner_clear_callback_new_owner_survives_old_continuation)
{
    ObjectRegistry registry;
    Object actor {}, other {};
    registry.create(&actor);
    MovementOwner owner;
    auto old = owner.acquire(registry, &actor, 1, false);
    CHECK(!owner.yield(&other));
    CHECK(owner.yield(&actor));
    auto next = owner.acquire(registry, &actor, 1, false);
    CHECK(owner.resolve(old, registry, 1, &actor) == nullptr);
    CHECK(owner.release(old, registry, 1, &actor) == nullptr);
    CHECK(owner.resolve(next, registry, 1, &actor) == &actor);
}

TEST(movement_owner_neutral_clears_unowned_pending_but_not_newer_owner)
{
    MovementOwner owner;
    CHECK(owner.mayClear(0));
    ObjectRegistry registry;
    Object actor {};
    registry.create(&actor);
    auto pending = owner.acquire(registry, &actor, 1, false);
    CHECK(!owner.mayClear(0));
    // Completion/cancellation before the first pose write releases ownership.
    CHECK(owner.release(pending, registry, 1, &actor) == &actor);
    CHECK(owner.mayClear(0));
    auto newer = owner.acquire(registry, &actor, 1, false);
    CHECK(!owner.mayClear(pending));
    CHECK(!owner.mayClear(0));
    CHECK(owner.resolve(newer, registry, 1, &actor) == &actor);
    // Dead/prone entry yields without a standing-pose callback.
    CHECK(owner.yield(&actor));
    CHECK(owner.release(newer, registry, 1, &actor) == nullptr);
    CHECK(owner.mayClear(0));
}

TEST(movement_stand_recovery_pause_neutral_claims_once)
{
    ObjectRegistry registry;
    Object actor {};
    registry.create(&actor);
    MovementOwner owner;
    MovementStandRecovery recovery;
    auto token = owner.acquire(registry, &actor, 1, false);
    recovery.request(registry, &actor, 1, token);
    CHECK(owner.release(token, registry, 1, &actor) == &actor);
    // No pose callback during cancellation. Dispatch later claims the request.
    CHECK(recovery.candidate(registry, 1, &actor) == &actor);
    CHECK(recovery.take(registry, 1, &actor, owner.generation(), false, false) == &actor);
    CHECK(recovery.take(registry, 1, &actor, owner.generation(), false, false) == nullptr);
}

TEST(movement_stand_recovery_rejects_new_owners_legacy_epoch_and_reuse)
{
    for (int change = 0; change < 7; change++) {
        ObjectRegistry registry;
        Object actor {}, other {};
        registry.create(&actor);
        registry.create(&other);
        MovementOwner owner;
        MovementStandRecovery recovery;
        auto token = owner.acquire(registry, &actor, 4, false);
        recovery.request(registry, &actor, 4, token);
        owner.release(token, registry, 4, &actor);
        uint64_t epoch = 4;
        Object* player = &actor;
        if (change == 0) CHECK(owner.acquire(registry, &actor, 4, false) != 0);
        if (change == 1) recovery.cancelFor(&actor); // Legacy attempt, even if registration fails.
        if (change == 2) epoch++;
        if (change == 3) {
            registry.destroy(&actor);
            registry.create(&actor);
        }
        if (change == 4) player = &other;
        if (change == 6) {
            auto newer = owner.acquire(registry, &actor, 4, false);
            CHECK(newer != 0);
            owner.release(newer, registry, 4, &actor);
        }
        CHECK(recovery.take(registry, epoch, player, owner.generation(), !owner.mayClear(0), change == 5) == nullptr);
        CHECK(recovery.candidate(registry, epoch, player) == nullptr);
    }
}
