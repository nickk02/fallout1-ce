#include "game/proto_types.h"
#include "sim/attack_participants.h"
#include "test_harness.h"
#include <vector>
using namespace fallout;

namespace {
struct Rig {
    ObjectRegistry registry;
    Object player {};
    Object target {};
    Object extra {};
    Attack attack {};
    uint64_t epoch = 4;
    Rig()
    {
        registry.create(&player);
        registry.create(&target);
        registry.create(&extra);
        attack.attacker = &player;
        attack.defender = &target;
        attack.oops = &target;
        attack.extrasLength = 1;
        attack.extras[0] = &extra;
    }
    AttackParticipants capture() { return AttackParticipants(registry, attack, epoch, &player); }
};
}

TEST(attack_participants_valid_callbacks_keep_damage_order)
{
    Rig r;
    auto refs = r.capture();
    std::vector<int> order;
    CHECK(refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] { order.push_back(1); r.player.id += 2; }));
    CHECK(refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] { order.push_back(2); r.target.id += 3; }));
    CHECK(refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] { order.push_back(3); r.extra.id += 4; }));
    CHECK(order == std::vector<int>({ 1, 2, 3 }));
    CHECK_EQ(r.player.id, 2);
    CHECK_EQ(r.target.id, 3);
    CHECK_EQ(r.extra.id, 4);
}

TEST(attack_participants_destroy_reuse_aborts_without_rolling_back_effect)
{
    Rig r;
    auto refs = r.capture();
    CHECK(!refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] {
        r.player.id += 2; r.registry.destroy(&r.target); r.registry.create(&r.target); }));
    bool called = false;
    CHECK(!refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] { called = true; }));
    CHECK(!called);
    CHECK_EQ(r.player.id, 2);
}

TEST(attack_participants_deleted_later_extra_stops_remaining_stages)
{
    Rig r;
    auto refs = r.capture();
    CHECK(!refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] {
        r.target.id = 5; r.registry.destroy(&r.extra); }));
    CHECK_EQ(r.target.id, 5);
    CHECK_EQ(r.extra.id, 0);
    CHECK(!refs.refresh(r.registry, r.epoch, &r.player, r.attack));
}

TEST(attack_participants_epoch_and_player_replacement_abort)
{
    Rig r;
    auto refs = r.capture();
    CHECK(!refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] { r.epoch++; }));
    CHECK(r.registry.resolve(r.registry.handleFor(&r.player)) == &r.player);
    CHECK(!refs.refresh(r.registry, r.epoch, &r.player, r.attack));
    auto current = r.capture();
    CHECK(!current.refresh(r.registry, r.epoch, &r.target, r.attack));
}

TEST(attack_participants_precompletion_deletion_and_optional_nulls)
{
    Rig r;
    auto refs = r.capture();
    r.registry.destroy(&r.target);
    bool called = false;
    CHECK(!refs.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] { called = true; }));
    CHECK(!called);
    Attack empty {};
    AttackParticipants none(r.registry, empty, r.epoch, nullptr);
    CHECK(none.run(r.registry, [&] { return r.epoch; }, []() -> Object* { return nullptr; }, empty, [&] { called = true; }));
    CHECK(called);
    CHECK(empty.attacker == nullptr);
    CHECK(empty.defender == nullptr);
    CHECK(empty.oops == nullptr);
}

TEST(attack_participants_run_reads_current_player_after_callback)
{
    Rig r;
    auto refs = r.capture();
    Object* player = &r.player;
    CHECK(!refs.run(r.registry, [&] { return r.epoch; }, [&] { return player; }, r.attack, [&] { player = &r.target; }));
}

TEST(deferred_attack_report_captures_before_queue_and_rejects_reuse)
{
    Rig r;
    auto report = std::make_shared<DeferredAttackReport>(r.registry, r.attack, r.epoch, &r.player, &r.player);
    std::weak_ptr<DeferredAttackReport> lifetime = report;
    void* queued;
    {
        AttackReportRegistration registration(report);
        queued = registration.token();
        registration.commit();
    }
    report.reset();
    CHECK(!lifetime.expired());
    r.registry.destroy(&r.target);
    r.registry.create(&r.target);
    {
        auto delivered = AttackReportRegistration::take(queued);
        CHECK(!delivered->refresh(r.registry, r.epoch, &r.player));
    }
    CHECK(lifetime.expired());
}

TEST(deferred_attack_report_visual_teardown_keeps_targets_and_source_required)
{
    Rig r;
    Object visual {};
    r.registry.create(&visual);
    r.attack.attacker = &visual;
    auto report = std::make_shared<DeferredAttackReport>(r.registry, r.attack, r.epoch, &r.player, &r.player);
    AttackReportRegistration registration(report);
    // anim_set_end erases forced HIDE owners before invoking forced reports.
    r.registry.destroy(&visual);
    auto delivered = AttackReportRegistration::take(registration.token());
    CHECK(delivered->refresh(r.registry, r.epoch, &r.player));
    CHECK(delivered->attack.attacker == nullptr);
    CHECK(delivered->attack.defender == &r.target);
    CHECK(delivered->source == &r.player);
    r.registry.destroy(&r.player);
    r.registry.create(&r.player);
    CHECK(!delivered->refresh(r.registry, r.epoch, &r.player));
}

TEST(deferred_attack_report_registration_failure_and_synchronous_delivery_release_once)
{
    Rig r;
    std::weak_ptr<DeferredAttackReport> failed;
    {
        auto report = std::make_shared<DeferredAttackReport>(r.registry, r.attack, r.epoch, &r.player, nullptr);
        failed = report;
        AttackReportRegistration registration(report);
        report.reset();
        CHECK(!failed.expired());
        // Simulate anim_cleanup dropping callbacks before register_end fails.
    }
    CHECK(failed.expired());
    std::weak_ptr<DeferredAttackReport> synchronous;
    {
        auto report = std::make_shared<DeferredAttackReport>(r.registry, r.attack, r.epoch, &r.player, nullptr);
        synchronous = report;
        AttackReportRegistration registration(report);
        report.reset();
        {
            auto delivered = AttackReportRegistration::take(registration.token());
            CHECK(delivered->refresh(r.registry, r.epoch, &r.player));
        }
        CHECK(synchronous.expired());
        registration.commit();
    }
    CHECK(synchronous.expired());
}

TEST(deferred_attack_report_epoch_invalidates_persistent_player)
{
    Rig r;
    DeferredAttackReport report(r.registry, r.attack, r.epoch, &r.player, nullptr);
    CHECK(!report.refresh(r.registry, r.epoch + 1, &r.player));
}

TEST(deferred_attack_report_immediate_delivery_and_failure_after_eager_callback)
{
    Rig r;
    for (bool commit : { false, true }) {
        auto report = std::make_shared<DeferredAttackReport>(r.registry, r.attack, r.epoch, &r.player, nullptr);
        std::weak_ptr<DeferredAttackReport> lifetime = report;
        {
            AttackReportRegistration registration(report);
            report.reset();
            {
                auto delivered = AttackReportRegistration::take(registration.token());
                CHECK(delivered->refresh(r.registry, r.epoch, &r.player));
            }
            if (commit) registration.commit();
        }
        CHECK(lifetime.expired());
    }
}

TEST(deferred_attack_report_separate_source_and_extra_reuse_reject)
{
    Rig r;
    Object source {};
    r.registry.create(&source);
    DeferredAttackReport report(r.registry, r.attack, r.epoch, &r.player, &source);
    r.registry.destroy(&source);
    r.registry.create(&source);
    CHECK(!report.refresh(r.registry, r.epoch, &r.player));
    DeferredAttackReport extras(r.registry, r.attack, r.epoch, &r.player, nullptr);
    r.registry.destroy(&r.extra);
    r.registry.create(&r.extra);
    CHECK(!extras.refresh(r.registry, r.epoch, &r.player));
}

TEST(attack_participants_clear_callback_reuse_stops_before_damage_and_cleanup)
{
    Rig r;
    Object visual {};
    r.registry.create(&visual);
    r.attack.attacker = &visual;
    const ObjectRef visualRef = object_ref_make(r.registry, &visual, r.epoch);
    auto beforeClear = r.capture();
    int committed = 0;
    bool allowed = beforeClear.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] {
        committed++;
        r.registry.destroy(&r.target);
        r.registry.create(&r.target);
        r.registry.destroy(&visual);
        r.registry.create(&visual); });
    CHECK(!allowed);
    CHECK(committed == 1);
    CHECK(object_ref_resolve(r.registry, visualRef, r.epoch) == nullptr);
    CHECK(r.registry.resolve(r.registry.handleFor(&visual)) == &visual);
}

TEST(attack_participants_clear_checks_discovered_extras_and_live_epoch)
{
    for (bool changeEpoch : { false, true }) {
        Rig r;
        const AttackParticipants discovered = r.capture();
        bool allowed = discovered.run(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, r.attack, [&] {
            if (changeEpoch) {
                r.epoch++;
            } else {
                r.registry.destroy(&r.extra);
                r.registry.create(&r.extra);
            } });
        CHECK(!allowed);
    }
}

TEST(attack_participants_clear_for_attack_guards_each_callback_and_weapon)
{
    for (int mutation = 0; mutation < 3; mutation++) {
        Rig r;
        Object weapon {};
        r.registry.create(&weapon);
        Attack local = r.attack;
        local.weapon = &weapon;
        auto refs = AttackParticipants(r.registry, local, r.epoch, &r.player);
        auto weaponRef = object_ref_make(r.registry, &weapon, r.epoch);
        int calls = 0;
        CHECK(!refs.clearForAttack(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, local, weaponRef, [&](Object*) {
            calls++;
            if(mutation==0){r.registry.destroy(&r.target);r.registry.create(&r.target);}
            if(mutation==1){r.registry.destroy(&weapon);r.registry.create(&weapon);}
            if(mutation==2)r.epoch++;
            return 0; }));
        CHECK(calls == 1);
    }
}

TEST(attack_participants_clear_for_attack_pins_numeric_snapshot_from_global_overwrite)
{
    Rig r;
    r.attack.defenderDamage = 17;
    r.attack.hitMode = 5;
    Attack local = r.attack;
    auto refs = r.capture();
    std::vector<Object*> cleared;
    CHECK(refs.clearForAttack(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, local, {}, [&](Object* actor) {
        cleared.push_back(actor);
        r.attack=Attack{}; // Forced script overwrites the caller's global record.
        return 0; }));
    CHECK(cleared == std::vector<Object*>({ &r.player, &r.target, &r.extra }));
    CHECK(local.defenderDamage == 17);
    CHECK(local.hitMode == 5);
    CHECK(refs.refreshForAttack(r.registry, r.epoch, &r.player, local, {}));
}

TEST(attack_participants_post_sequence_consumed_throw_omits_reused_weapon)
{
    Rig r;
    Object weapon {};
    r.registry.create(&weapon);
    r.attack.weapon = &weapon;
    auto refs = r.capture();
    auto weaponRef = object_ref_make(r.registry, &weapon, r.epoch);
    Attack unconsumed = r.attack, consumed = r.attack;
    r.registry.destroy(&weapon);
    r.registry.create(&weapon);
    CHECK(!refs.refreshForAttack(r.registry, r.epoch, &r.player, unconsumed, weaponRef));
    CHECK(refs.refreshForAttack(r.registry, r.epoch, &r.player, consumed, weaponRef, true));
    CHECK(consumed.weapon == nullptr);
    r.registry.destroy(&r.extra);
    r.registry.create(&r.extra);
    CHECK(!refs.refreshForAttack(r.registry, r.epoch, &r.player, consumed, weaponRef, true));
}

TEST(attack_participants_clear_for_attack_invalid_weapon_and_prioritized_failure)
{
    Rig r;
    Object unregistered {};
    Attack local = r.attack;
    local.weapon = &unregistered;
    auto refs = r.capture();
    int calls = 0;
    CHECK(!refs.clearForAttack(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, local, {}, [&](Object*) {calls++;return 0; }));
    CHECK(calls == 0);
    local.weapon = nullptr;
    CHECK(!refs.clearForAttack(r.registry, [&] { return r.epoch; }, [&] { return &r.player; }, local, {}, [&](Object*) {calls++;return -2; }));
    CHECK(calls == 1);
}

TEST(attack_participants_consumed_throw_visual_type_survives_weapon_lifetime)
{
    Rig r;
    Object grenade {};
    r.registry.create(&grenade);
    r.attack.weapon = &grenade;
    auto refs = r.capture();
    auto grenadeRef = object_ref_make(r.registry, &grenade, r.epoch);
    const int explosionType = DAMAGE_TYPE_EXPLOSION;
    int lookups = 0;
    auto lookup = [&](Object*) { lookups++; return 0; };
    Attack live = r.attack;
    CHECK(refs.refreshForAttack(r.registry, r.epoch, &r.player, live, grenadeRef, true));
    CHECK(live.weapon == &grenade);
    CHECK(attack_visual_damage_type(explosionType, live.weapon, lookup) == explosionType);
    r.registry.destroy(&grenade);
    Attack deleted = r.attack;
    CHECK(refs.refreshForAttack(r.registry, r.epoch, &r.player, deleted, grenadeRef, true));
    CHECK(deleted.weapon == nullptr);
    CHECK(attack_visual_damage_type(explosionType, deleted.weapon, lookup) == explosionType);
    r.registry.create(&grenade);
    Attack reused = r.attack;
    CHECK(refs.refreshForAttack(r.registry, r.epoch, &r.player, reused, grenadeRef, true));
    CHECK(reused.weapon == nullptr);
    CHECK(attack_visual_damage_type(explosionType, reused.weapon, lookup) == explosionType);
    CHECK(lookups == 0);
    CHECK(attack_visual_damage_type(-1, nullptr, lookup) == 0);
    CHECK(lookups == 1);
}
