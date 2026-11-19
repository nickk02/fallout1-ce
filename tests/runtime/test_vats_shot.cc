#include "test_harness.h"

#include "sim/vats_shot.h"

using namespace fallout;

namespace {

struct Actor {
    bool dead = false;
};
struct Weapon {
    Actor* owner;
};
struct Rig {
    ObjectRegistry registry;
    Actor player;
    Actor target;
    Weapon weapon { &player };
    Weapon replacement { &player };
    Actor* currentPlayer = &player;
    Weapon* selected = &weapon;
    uint64_t epoch = 9;
    int checks = 0;
    int modeSeen = -1;

    Rig()
    {
        registry.create(&player);
        registry.create(&target);
        registry.create(&weapon);
        registry.create(&replacement);
    }

    VatsShot capture(bool unarmed = false)
    {
        return vats_shot_capture(registry, &player, &target, unarmed ? nullptr : &weapon,
            unarmed, 7, 3, epoch);
    }

    VatsShotValidation validate(const VatsShot& shot)
    {
        return vats_shot_validate(registry, shot, epoch,
            [&](void* attacker, void* defender, void* item, int mode) {
                checks++;
                modeSeen = mode;
                auto actor = static_cast<Actor*>(attacker);
                auto targetActor = static_cast<Actor*>(defender);
                auto weaponItem = static_cast<Weapon*>(item);
                if (actor != currentPlayer) return VatsShotReject::WrongAttacker;
                if (actor->dead) return VatsShotReject::InactiveAttacker;
                if (targetActor->dead) return VatsShotReject::InactiveTarget;
                if (weaponItem != selected || (weaponItem && weaponItem->owner != actor)) return VatsShotReject::WeaponMismatch;
                return VatsShotReject::None;
            });
    }
};

void checkRejected(const VatsShotValidation& result, VatsShotReject reason)
{
    CHECK(result.reason == reason);
    CHECK(result.attacker == nullptr);
    CHECK(result.target == nullptr);
    CHECK(result.weapon == nullptr);
}

} // namespace

TEST(vats_shot_preserves_mode_location_and_can_validate_repeatedly_without_consumption)
{
    Rig rig;
    auto shot = rig.capture();
    auto result = rig.validate(shot);
    CHECK(result.reason == VatsShotReject::None);
    CHECK(result.attacker == &rig.player);
    CHECK(result.target == &rig.target);
    CHECK(result.weapon == &rig.weapon);
    CHECK_EQ(rig.modeSeen, 7);
    CHECK_EQ(shot.hitMode, 7);
    CHECK_EQ(shot.hitLocation, 3);
    CHECK(rig.validate(shot).reason == VatsShotReject::None);
}

TEST(vats_shot_revalidates_after_modal_target_destroy_and_address_reuse)
{
    Rig rig;
    auto shot = rig.capture();
    CHECK(rig.validate(shot).reason == VatsShotReject::None);
    auto modal = [&] {
        rig.registry.destroy(&rig.target);
        rig.registry.create(&rig.target);
    };
    modal();
    checkRejected(rig.validate(shot), VatsShotReject::StaleTarget);
    CHECK_EQ(rig.checks, 1);
}

TEST(vats_shot_epoch_rejects_persistent_player_after_modal)
{
    Rig rig;
    auto shot = rig.capture();
    CHECK(rig.validate(shot).reason == VatsShotReject::None);
    rig.epoch++;
    checkRejected(rig.validate(shot), VatsShotReject::EpochChanged);
    CHECK(rig.registry.resolve(shot.attacker.handle) == &rig.player);
    CHECK_EQ(rig.checks, 1);
}

TEST(vats_shot_stale_and_mixed_epoch_refs_never_reach_callback)
{
    for (int field = 0; field < 3; field++) {
        Rig rig;
        auto shot = rig.capture();
        ObjectRef* refs[] = { &shot.attacker, &shot.target, &shot.weapon };
        refs[field]->epoch--;
        checkRejected(rig.validate(shot), VatsShotReject::EpochChanged);
        CHECK_EQ(rig.checks, 0);
    }
    Rig rig;
    auto shot = rig.capture();
    rig.registry.destroy(&rig.player);
    checkRejected(rig.validate(shot), VatsShotReject::StaleAttacker);
    CHECK_EQ(rig.checks, 0);
    rig.registry.create(&rig.player);
    shot = rig.capture();
    rig.registry.destroy(&rig.weapon);
    checkRejected(rig.validate(shot), VatsShotReject::StaleWeapon);
    CHECK_EQ(rig.checks, 0);
}

TEST(vats_shot_dead_or_replaced_actor_and_dead_target_reject_through_engine_check)
{
    Rig rig;
    auto shot = rig.capture();
    rig.player.dead = true;
    checkRejected(rig.validate(shot), VatsShotReject::InactiveAttacker);
    rig.player.dead = false;
    rig.target.dead = true;
    checkRejected(rig.validate(shot), VatsShotReject::InactiveTarget);
    rig.target.dead = false;
    rig.currentPlayer = &rig.target;
    checkRejected(rig.validate(shot), VatsShotReject::WrongAttacker);
}

TEST(vats_shot_transfer_and_changed_mode_slot_reject_without_mutating_snapshot)
{
    Rig rig;
    auto shot = rig.capture();
    rig.weapon.owner = &rig.target;
    checkRejected(rig.validate(shot), VatsShotReject::WeaponMismatch);
    rig.weapon.owner = &rig.player;
    rig.selected = &rig.replacement;
    checkRejected(rig.validate(shot), VatsShotReject::WeaponMismatch);
    CHECK(shot.weapon.handle == rig.registry.handleFor(&rig.weapon));
    CHECK_EQ(shot.hitMode, 7);
}

TEST(vats_shot_unarmed_is_explicit_and_missing_armed_weapon_rejects)
{
    Rig rig;
    rig.selected = nullptr;
    auto shot = rig.capture(true);
    auto result = rig.validate(shot);
    CHECK(result.reason == VatsShotReject::None);
    CHECK(result.weapon == nullptr);
    shot.unarmed = false;
    checkRejected(rig.validate(shot), VatsShotReject::StaleWeapon);
    shot = vats_shot_capture(rig.registry, nullptr, &rig.target, nullptr, true, 7, 3, rig.epoch);
    checkRejected(rig.validate(shot), VatsShotReject::StaleAttacker);
    shot = vats_shot_capture(rig.registry, &rig.player, nullptr, nullptr, true, 7, 3, rig.epoch);
    checkRejected(rig.validate(shot), VatsShotReject::StaleTarget);
}

TEST(vats_shot_requires_engine_validation_even_unarmed)
{
    Rig rig;
    auto shot = rig.capture(true);
    checkRejected(vats_shot_validate(rig.registry, shot, rig.epoch, {}), VatsShotReject::MissingCheck);
}
