#include "sim/animation_lease.h"
#include "sim/attack_instance.h"
#include "test_harness.h"
#include <limits>
#include <vector>
using namespace fallout;
namespace {
struct AttackHarness {
    ObjectRegistry registry;
    Object actor {}, target {}, weapon {};
    SerializedAttack attacks;
    CombatAnimationLeases leases;
    uint64_t epoch = 7;
    int ap = 9, ammo = 5;
    std::vector<int> order;
    SerializedAttack::Ptr pin;
    AttackHarness()
    {
        registry.create(&actor);
        registry.create(&target);
        registry.create(&weapon);
    }
    SerializedAttack::Completion hooks()
    {
        SerializedAttack::Completion h;
        h.debit = [&](void* resolved, int quantity) { CHECK(resolved==&weapon);ammo-=quantity;order.push_back(3); };
        h.display = [&](AttackInstance&) { order.push_back(4); };
        h.damage = [&](AttackInstance&) {order.push_back(5);return true; };
        h.endCombat = [&](AttackInstance&) {order.push_back(6);return false; };
        h.afterEnd = [&](AttackInstance&, bool) { order.push_back(7); };
        return h;
    }
    bool start(const SerializedAttack::Stage& sequence = {}, const SerializedAttack::Stage& commit = {})
    {
        return attacks.start(leases.count(), registry, [&] { return epoch; }, [&] { return &actor; }, [&](AttackInstance& i) {
            i.attack.attacker=&actor;i.attack.defender=&target;i.attack.oops=&target;i.attack.weapon=&weapon;
            i.attack.ammoQuantity=2;i.eligibleAmmo=true;i.actionPoints=3;return true; }, [&](AttackInstance& i) { pin=attacks.active();order.push_back(1);return sequence ? sequence(i) : true; }, [&](AttackInstance& i) {ap-=i.actionPoints;order.push_back(2);return commit ? commit(i) : true; });
    }
    SerializedAttack::Finish finish(const SerializedAttack::Completion& h)
    {
        return attacks.finish(leases.count(), registry, [&] { return epoch; }, [&] { return &actor; }, h);
    }
};
}
TEST(attack_instance_eager_completion_waits_for_seal_and_orders_commit)
{
    AttackHarness h;
    auto hooks = h.hooks();
    CHECK(h.start([&](AttackInstance&) {
        auto lease = h.leases.begin();
        CHECK(h.leases.finish(lease));
        CHECK(h.finish(hooks) == SerializedAttack::Finish::NotReady);
        CHECK(h.ap == 9 && h.ammo == 5);
        return true;
    }));
    CHECK(h.pin->phase == AttackPhase::Sealed);
    CHECK(h.ap == 6 && h.ammo == 5);
    CHECK(h.finish(hooks) == SerializedAttack::Finish::Completed);
    CHECK(h.order == std::vector<int>({ 1, 2, 3, 4, 5, 6, 7 }));
    CHECK(h.ammo == 3);
    CHECK(h.finish(hooks) == SerializedAttack::Finish::NotReady);
    CHECK(h.ammo == 3);
}
TEST(attack_instance_nested_reject_and_partial_registration_failure)
{
    AttackHarness h;
    CombatAnimationLease partial;
    CHECK(!h.start([&](AttackInstance&) {
        CHECK(!h.start());
        partial = h.leases.begin();
        return false;
    }));
    CHECK(h.ap == 9 && h.ammo == 5);
    CHECK(h.pin->phase == AttackPhase::Canceled);
    CHECK(!h.start());
    CHECK(h.leases.finish(partial));
    CHECK(h.finish(h.hooks()) == SerializedAttack::Finish::NotReady);
    CHECK(h.start());
}
TEST(attack_instance_reset_during_damage_pins_old_storage_and_rejects_replacement)
{
    AttackHarness h;
    CHECK(h.start());
    auto old = h.pin;
    auto hooks = h.hooks();
    int followups = 0;
    hooks.damage = [&](AttackInstance& i) {
        auto id = i.identity;
        h.epoch++;
        h.attacks.reset();
        CHECK(i.identity == id);
        CHECK(i.phase == AttackPhase::Canceled);
        CHECK(!h.start());
        return true;
    };
    hooks.afterDamage = [&](AttackInstance&) { followups++; };
    CHECK(h.finish(hooks) == SerializedAttack::Finish::Aborted);
    CHECK(h.ammo == 3 && h.ap == 6);
    CHECK(followups == 0);
    CHECK(h.start());
    CHECK(h.pin->identity != old->identity);
    CHECK(h.pin->phase == AttackPhase::Sealed);
}
TEST(attack_instance_participant_reuse_preserves_committed_ammo_only)
{
    AttackHarness h;
    CHECK(h.start());
    h.registry.destroy(&h.target);
    h.registry.create(&h.target);
    CHECK(h.finish(h.hooks()) == SerializedAttack::Finish::Aborted);
    CHECK(h.ammo == 3 && h.ap == 6);
    CHECK(h.order == std::vector<int>({ 1, 2, 3 }));
}
TEST(attack_instance_reset_during_sequence_never_commits_and_identity_never_wraps)
{
    AttackHarness h;
    CHECK(!h.start([&](AttackInstance& i) {h.attacks.reset();CHECK(i.phase==AttackPhase::Canceled);CHECK(!h.start());return true; }));
    CHECK(h.ap == 9 && h.ammo == 5);
    SerializedAttack last(std::numeric_limits<uint64_t>::max());
    auto build = [&](AttackInstance& i) {i.attack.attacker=&h.actor;i.attack.defender=&h.target;return true; };
    auto yes = [](AttackInstance&) { return true; };
    CHECK(last.start(0, h.registry, [&] { return h.epoch; }, [&] { return &h.actor; }, build, yes, yes));
    last.reset();
    CHECK(!last.start(0, h.registry, [&] { return h.epoch; }, [&] { return &h.actor; }, build, yes, yes));
}

TEST(attack_instance_commit_and_damage_callbacks_cannot_finish_or_start_nested)
{
    AttackHarness h;
    auto hooks = h.hooks();
    CHECK(h.start({}, [&](AttackInstance&) {
        CHECK(h.ap == 6);
        CHECK(h.finish(hooks) == SerializedAttack::Finish::NotReady);
        CHECK(!h.start());
        CHECK(h.ammo == 5);
        return true;
    }));
    hooks.damage = [&](AttackInstance&) {
        CHECK(h.pin->phase == AttackPhase::Completing);
        CHECK(h.finish(hooks) == SerializedAttack::Finish::NotReady);
        CHECK(!h.start());
        h.target.id = 42;
        h.registry.destroy(&h.target);
        h.registry.create(&h.target);
        return true;
    };
    int after = 0;
    hooks.afterDamage = [&](AttackInstance&) { after++; };
    CHECK(h.finish(hooks) == SerializedAttack::Finish::Aborted);
    CHECK(after == 0);
    CHECK(h.target.id == 42);
    CHECK(h.ap == 6 && h.ammo == 3);
}

TEST(attack_instance_lifecycle_is_checked_after_every_completion_callback)
{
    for (int stop = 1; stop <= 8; stop++) {
        AttackHarness h;
        CHECK(h.start());
        int last = 0;
        auto stage = [&](int n) {last=n;if(n==stop){h.epoch++;h.attacks.reset();} };
        SerializedAttack::Completion hooks;
        hooks.ready = [&](AttackInstance&) { stage(1); };
        hooks.debit = [&](void* weapon, int quantity) {CHECK(weapon==&h.weapon);h.ammo-=quantity;stage(2); };
        hooks.ammoDebited = [&](AttackInstance&) { stage(3); };
        hooks.display = [&](AttackInstance&) { stage(4); };
        hooks.damage = [&](AttackInstance&) {stage(5);return true; };
        hooks.afterDamage = [&](AttackInstance&) { stage(6); };
        hooks.endCombat = [&](AttackInstance&) {stage(7);return true; };
        hooks.afterEnd = [&](AttackInstance&, bool) { stage(8); };
        hooks.abort = [&] { CHECK(false); }; // No old-world UI/abort hook after lifecycle replacement.
        CHECK(h.finish(hooks) == SerializedAttack::Finish::Aborted);
        CHECK(last == stop);
        CHECK(h.ap == 6);
        CHECK(h.ammo == (stop == 1 ? 5 : 3));
    }
}
