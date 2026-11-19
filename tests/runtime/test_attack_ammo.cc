#include "test_harness.h"

#include "sim/attack_ammo.h"

using namespace fallout;

namespace {
struct Weapon {
    int ammo;
};

void debit(void* weapon, int quantity)
{
    static_cast<Weapon*>(weapon)->ammo -= quantity;
}
} // namespace

TEST(attack_ammo_hand_switch_debits_only_captured_weapon_once)
{
    ObjectRegistry registry;
    Weapon a { 12 }, b { 20 };
    registry.create(&a);
    registry.create(&b);
    AttackAmmo ammo;
    Weapon* hand = &a;
    CHECK(ammo.prepare(registry, hand, 5, 3, true));
    hand = &b;
    CHECK(ammo.seal());
    CHECK(ammo.finish(registry, 5, debit));
    CHECK_EQ(a.ammo, 9);
    CHECK_EQ(hand->ammo, 20);
    CHECK(!ammo.finish(registry, 5, debit));
    CHECK_EQ(a.ammo, 9);
}

TEST(attack_ammo_synchronous_finish_waits_for_success_seal)
{
    ObjectRegistry registry;
    Weapon weapon { 10 };
    registry.create(&weapon);
    AttackAmmo ammo;
    CHECK(ammo.prepare(registry, &weapon, 5, 2, true));
    CHECK(!ammo.finish(registry, 5, debit));
    CHECK_EQ(weapon.ammo, 10);
    CHECK(ammo.active());
    CHECK(ammo.seal());
    CHECK_EQ(weapon.ammo, 10);
    CHECK(ammo.finish(registry, 5, debit));
    CHECK_EQ(weapon.ammo, 8);
    CHECK(!ammo.active());
}

TEST(attack_ammo_failure_cancels_preparation_without_debit)
{
    ObjectRegistry registry;
    Weapon weapon { 10 };
    registry.create(&weapon);
    AttackAmmo ammo;
    CHECK(ammo.prepare(registry, &weapon, 5, 2, true));
    ammo.cancel();
    CHECK(!ammo.seal());
    CHECK(!ammo.finish(registry, 5, debit));
    CHECK_EQ(weapon.ammo, 10);
    CHECK(ammo.prepare(registry, &weapon, 5, 1, true));
    CHECK(ammo.seal());
    ammo.cancel();
    CHECK(!ammo.finish(registry, 5, debit));
    CHECK_EQ(weapon.ammo, 10);
}

TEST(attack_ammo_reused_address_never_debits_replacement)
{
    ObjectRegistry registry;
    Weapon weapon { 10 };
    registry.create(&weapon);
    AttackAmmo ammo;
    CHECK(ammo.prepare(registry, &weapon, 5, 2, true));
    CHECK(ammo.seal());
    registry.destroy(&weapon);
    weapon.ammo = 99;
    registry.create(&weapon);
    CHECK(!ammo.finish(registry, 5, debit));
    CHECK_EQ(weapon.ammo, 99);
    CHECK(!ammo.active());
}

TEST(attack_ammo_epoch_change_rejects_live_weapon_and_clears)
{
    ObjectRegistry registry;
    Weapon weapon { 10 };
    registry.create(&weapon);
    AttackAmmo ammo;
    CHECK(ammo.prepare(registry, &weapon, 5, 2, true));
    CHECK(ammo.seal());
    CHECK(!ammo.finish(registry, 6, debit));
    CHECK(!ammo.finish(registry, 5, debit));
    CHECK_EQ(weapon.ammo, 10);
}

TEST(attack_ammo_unarmed_throw_and_nonpositive_quantity_do_not_debit)
{
    ObjectRegistry registry;
    Weapon weapon { 10 };
    registry.create(&weapon);
    struct Case {
        const void* weapon;
        int quantity;
        bool eligible;
    };
    const Case cases[] = {
        { nullptr, 1, false },
        { &weapon, 1, false },
        { &weapon, 0, true },
        { &weapon, -1, true },
        { nullptr, 1, true },
    };
    for (const auto& item : cases) {
        AttackAmmo ammo;
        CHECK(ammo.prepare(registry, item.weapon, 5, item.quantity, item.eligible));
        CHECK(ammo.seal());
        CHECK(!ammo.finish(registry, 5, debit));
        CHECK(!ammo.active());
        CHECK_EQ(weapon.ammo, 10);
    }
}

TEST(attack_ammo_claims_before_mutation_callback_and_rejects_active_overwrite)
{
    ObjectRegistry registry;
    Weapon a { 10 }, b { 20 };
    registry.create(&a);
    registry.create(&b);
    AttackAmmo ammo;
    CHECK(ammo.prepare(registry, &a, 5, 2, true));
    CHECK(!ammo.prepare(registry, &b, 5, 7, true));
    CHECK(ammo.seal());
    CHECK(ammo.finish(registry, 5, [&](void* weapon, int quantity) {
        CHECK(!ammo.active());
        CHECK(!ammo.finish(registry, 5, debit));
        debit(weapon, quantity);
    }));
    CHECK_EQ(a.ammo, 8);
    CHECK_EQ(b.ammo, 20);
}
