#include "sim/animation_lease.h"
#include "test_harness.h"
#include <limits>
using namespace fallout;

TEST(animation_lease_duplicate_stale_and_new_generation)
{
    CombatAnimationLeases leases;
    auto a = leases.begin();
    auto b = leases.begin();
    CHECK(leases.count() == 2);
    CHECK(leases.finish(a));
    CHECK(!leases.finish(a));
    leases.reset();
    auto c = leases.begin();
    CHECK(!leases.finish(b));
    CHECK(leases.count() == 1);
    CHECK(leases.finish(c));
    CHECK(leases.count() == 0);
}

TEST(animation_lease_reentrant_claim_and_synchronous_finish)
{
    CombatAnimationLeases leases;
    AnimationLeaseSlot slot;
    CHECK(slot.begin());
    slot.lease = leases.begin();
    auto end = slot.claimEnd();
    CHECK(end.valid);
    CHECK(!slot.claimEnd().valid);
    CHECK(leases.count() == 1);
    CHECK(slot.complete(end));
    CHECK(leases.finish(end.lease));
    CHECK(!leases.finish(end.lease));
    CHECK(leases.count() == 0);
}

TEST(animation_lease_reset_during_callback_cannot_finalize_reused_slot)
{
    CombatAnimationLeases leases;
    AnimationLeaseSlot slot;
    CHECK(slot.begin());
    slot.lease = leases.begin();
    auto old = slot.claimEnd();
    slot.reset();
    leases.reset();
    CHECK(slot.begin());
    slot.lease = leases.begin();
    CHECK(!slot.current(old));
    CHECK(!slot.complete(old));
    CHECK(!leases.finish(old.lease));
    CHECK(leases.count() == 1);
    auto next = slot.claimEnd();
    CHECK(slot.complete(next));
    CHECK(leases.finish(next.lease));
}

TEST(animation_lease_legacy_load_normalizes_any_count)
{
    for (int count : { -1, 0, 7, 2147483647 }) {
        CombatAnimationLeases leases;
        auto old = leases.begin();
        leases.normalizeLoadedCount(count);
        CHECK(leases.count() == 0);
        CHECK(!leases.finish(old));
        CHECK(leases.finish(leases.begin()));
    }
}

TEST(animation_lease_exhaustion_fails_closed)
{
    CombatAnimationLeases leases(std::numeric_limits<uint64_t>::max());
    auto last = leases.begin();
    CHECK(last.valid());
    CHECK(!leases.begin().valid());
    CHECK(leases.finish(last));
    leases.reset();
    CHECK(!leases.begin().valid());
    AnimationLeaseSlot slot(std::numeric_limits<uint64_t>::max());
    CHECK(!slot.begin());
}
