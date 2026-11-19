#include "test_harness.h"

#include "sim/buffered_attack.h"

using namespace fallout;

namespace {

struct Actor {
    int ap = 0;
};

struct Weapon {
    const Actor* owner;
};

struct Rig {
    ObjectRegistry registry;
    Actor attacker;
    Actor target;
    Weapon weapon { &attacker };
    BufferedAttack buffer;

    Rig()
    {
        registry.create(&attacker);
        registry.create(&target);
        registry.create(&weapon);
    }

    BufferedAttackRequest capture(uint32_t now = 100, bool unarmed = false)
    {
        return buffered_attack_capture(registry, &attacker, &target,
            unarmed ? nullptr : &weapon, unarmed, 7, BufferedAttackKind::Ordinary, 12, now);
    }

    BufferedAttackResult take(uint32_t now = 100, uint64_t epoch = 12)
    {
        return buffer.take(registry, epoch, now, [](const void* actor, const void* item, int) {
            return item == nullptr || static_cast<const Weapon*>(item)->owner == actor;
        });
    }
};

} // namespace

TEST(buffered_attack_returns_captured_mode_and_consumes_once_without_ap)
{
    Rig rig;
    auto request = rig.capture();
    rig.buffer.put(request);
    request.hitMode = 99;
    rig.attacker.ap = 0;
    auto result = rig.take();
    CHECK(result.reason == BufferedAttackReject::None);
    CHECK(result.request.has_value());
    if (result.request) {
        CHECK_EQ(result.request->hitMode, 7);
        CHECK(result.request->attacker.handle == rig.registry.handleFor(&rig.attacker));
        CHECK(result.request->target.handle == rig.registry.handleFor(&rig.target));
        CHECK(result.request->weapon.handle == rig.registry.handleFor(&rig.weapon));
        CHECK(result.request->kind == BufferedAttackKind::Ordinary);
    }
    CHECK(!rig.buffer.pending());
    CHECK(rig.take().reason == BufferedAttackReject::Empty);
}

TEST(buffered_attack_target_address_reuse_cannot_retarget)
{
    Rig rig;
    rig.buffer.put(rig.capture());
    rig.registry.destroy(&rig.target);
    rig.registry.create(&rig.target);
    auto result = rig.take();
    CHECK(result.reason == BufferedAttackReject::StaleTarget);
    CHECK(!result.request);
    CHECK(rig.take().reason == BufferedAttackReject::Empty);
}

TEST(buffered_attack_destroyed_target_is_rejected_before_ownership_callback)
{
    Rig rig;
    rig.buffer.put(rig.capture());
    rig.registry.destroy(&rig.target);
    bool called = false;
    auto result = rig.buffer.take(rig.registry, 12, 100, [&](const void*, const void*, int) {
        called = true;
        return true;
    });
    CHECK(result.reason == BufferedAttackReject::StaleTarget);
    CHECK(!result.request);
    CHECK(!called);
}

TEST(buffered_attack_stale_attacker_and_weapon_reject)
{
    Rig rig;
    rig.buffer.put(rig.capture());
    rig.registry.destroy(&rig.attacker);
    auto result = rig.take();
    CHECK(result.reason == BufferedAttackReject::StaleAttacker);
    CHECK(!result.request);
    rig.registry.create(&rig.attacker);
    rig.buffer.put(rig.capture());
    rig.registry.destroy(&rig.weapon);
    rig.registry.create(&rig.weapon);
    result = rig.take();
    CHECK(result.reason == BufferedAttackReject::StaleWeapon);
    CHECK(!result.request);
}

TEST(buffered_attack_weapon_transfer_is_rejected)
{
    Rig rig;
    rig.buffer.put(rig.capture());
    rig.weapon.owner = &rig.target;
    auto result = rig.take();
    CHECK(result.reason == BufferedAttackReject::WeaponMismatch);
    CHECK(!result.request);
    CHECK(rig.take().reason == BufferedAttackReject::Empty);
}

TEST(buffered_attack_epoch_change_rejects_even_persistent_attacker)
{
    Rig rig;
    auto handle = rig.registry.handleFor(&rig.attacker);
    rig.buffer.put(rig.capture());
    auto result = rig.take(100, 13);
    CHECK(result.reason == BufferedAttackReject::EpochChanged);
    CHECK(!result.request);
    CHECK(rig.registry.resolve(handle) == &rig.attacker);
}

TEST(buffered_attack_mismatched_ref_epoch_rejects_before_callback)
{
    for (int field = 0; field < 3; field++) {
        Rig rig;
        auto request = rig.capture();
        ObjectRef* refs[] = { &request.attacker, &request.target, &request.weapon };
        refs[field]->epoch = 11;
        rig.buffer.put(request);
        bool called = false;
        auto result = rig.buffer.take(rig.registry, 12, 100, [&](const void*, const void*, int) {
            called = true;
            return true;
        });
        CHECK(result.reason == BufferedAttackReject::EpochChanged);
        CHECK(!result.request);
        CHECK(!called);
    }
}

TEST(buffered_attack_ttl_boundary_and_unsigned_wrap)
{
    struct Case {
        uint32_t captured;
        uint32_t now;
        BufferedAttackReject reason;
    };
    const Case cases[] = {
        { 100, 2599, BufferedAttackReject::None },
        { 100, 2600, BufferedAttackReject::None },
        { 100, 2601, BufferedAttackReject::Expired },
        { 0xFFFFFF00u, 2244, BufferedAttackReject::None },
        { 0xFFFFFF00u, 2245, BufferedAttackReject::Expired },
    };
    for (const auto& item : cases) {
        Rig rig;
        rig.buffer.put(rig.capture(item.captured));
        auto result = rig.take(item.now);
        CHECK(result.reason == item.reason);
        CHECK(result.request.has_value() == (item.reason == BufferedAttackReject::None));
        CHECK(!rig.buffer.pending());
    }
}

TEST(buffered_attack_overwrite_preserves_latest_vats_snapshot)
{
    Rig rig;
    rig.buffer.put(rig.capture(100));
    auto latest = rig.capture(200);
    latest.hitMode = 3;
    latest.kind = BufferedAttackKind::Vats;
    rig.buffer.put(latest);
    auto result = rig.take(2700);
    CHECK(result.reason == BufferedAttackReject::None);
    CHECK(result.request.has_value());
    if (result.request) {
        CHECK_EQ(result.request->hitMode, 3);
        CHECK_EQ(result.request->capturedAt, 200u);
        CHECK(result.request->kind == BufferedAttackKind::Vats);
    }
}

TEST(buffered_attack_cancel_and_reset_clear_pending)
{
    Rig rig;
    rig.buffer.put(rig.capture());
    rig.buffer.cancel();
    rig.buffer.cancel();
    CHECK(!rig.buffer.pending());
    CHECK(rig.take().reason == BufferedAttackReject::Empty);
    rig.buffer.put(rig.capture());
    rig.buffer.reset();
    CHECK(!rig.buffer.pending());
    CHECK(rig.take().reason == BufferedAttackReject::Empty);
}

TEST(buffered_attack_unarmed_is_explicit_and_armed_needs_weapon_and_owner_check)
{
    Rig rig;
    auto unarmed = rig.capture(100, true);
    rig.buffer.put(unarmed);
    auto result = rig.buffer.take(rig.registry, 12, 100);
    CHECK(result.reason == BufferedAttackReject::None);
    CHECK(result.request.has_value());
    if (result.request) {
        CHECK(result.request->unarmed);
        CHECK(!result.request->weapon.handle.valid());
    }
    unarmed.unarmed = false;
    rig.buffer.put(unarmed);
    result = rig.take();
    CHECK(result.reason == BufferedAttackReject::StaleWeapon);
    CHECK(!result.request);
    rig.buffer.put(rig.capture());
    result = rig.buffer.take(rig.registry, 12, 100);
    CHECK(result.reason == BufferedAttackReject::WeaponMismatch);
    CHECK(!result.request);
}

TEST(buffered_attack_takes_before_callback_so_reentry_cannot_consume_twice)
{
    Rig rig;
    rig.buffer.put(rig.capture());
    auto result = rig.buffer.take(rig.registry, 12, 100, [&](const void*, const void*, int) {
        CHECK(rig.take().reason == BufferedAttackReject::Empty);
        rig.buffer.put(rig.capture(200));
        return true;
    });
    CHECK(result.reason == BufferedAttackReject::None);
    CHECK(rig.buffer.pending());
    CHECK(rig.take(200).reason == BufferedAttackReject::None);
}

TEST(buffered_attack_pause_delays_expiry_without_changing_ttl)
{
    Rig rig;
    rig.buffer.delayExpiry(5000);
    CHECK(!rig.buffer.pending());
    rig.buffer.put(rig.capture(0xFFFFFF00u));
    rig.buffer.delayExpiry(5000);
    auto result = rig.take(7244);
    CHECK(result.reason == BufferedAttackReject::None);
    CHECK(result.request.has_value());
    if (result.request) {
        CHECK_EQ(result.request->capturedAt, 4744u);
    }
    rig.buffer.put(rig.capture(0xFFFFFF00u));
    rig.buffer.delayExpiry(5000);
    result = rig.take(7245);
    CHECK(result.reason == BufferedAttackReject::Expired);
    CHECK(!result.request);
}

TEST(buffered_attack_slot_change_rejects_with_captured_mode)
{
    Rig rig;
    rig.buffer.put(rig.capture());
    Weapon replacement { &rig.attacker };
    rig.registry.create(&replacement);
    const Weapon* currentSlot = &replacement;
    int checkedMode = -1;
    auto result = rig.buffer.take(rig.registry, 12, 100,
        [&](const void* actor, const void* item, int mode) {
            checkedMode = mode;
            return actor == &rig.attacker && mode == 7 && currentSlot == item;
        });
    CHECK_EQ(checkedMode, 7);
    CHECK(result.reason == BufferedAttackReject::WeaponMismatch);
    CHECK(!result.request);
}

TEST(buffered_attack_unarmed_mode_can_be_validated_without_a_weapon)
{
    Rig rig;
    rig.buffer.put(rig.capture(100, true));
    bool checked = false;
    auto result = rig.buffer.take(rig.registry, 12, 100,
        [&](const void* actor, const void* item, int mode) {
            checked = actor == &rig.attacker && item == nullptr && mode == 7;
            return checked;
        });
    CHECK(checked);
    CHECK(result.reason == BufferedAttackReject::None);
    rig.buffer.put(rig.capture(100, true));
    result = rig.buffer.take(rig.registry, 12, 100,
        [](const void*, const void*, int) { return false; });
    CHECK(result.reason == BufferedAttackReject::WeaponMismatch);
    CHECK(!result.request);
}
