// A buffered attack through the serialized attack owner and the real animation
// engine to completion.
//
// Real here: anim.cc (registration, stepping, combat lease callbacks),
// BufferedAttack, SerializedAttack, AttackAmmo, AttackParticipants, the combat
// animation leases, and the AP rule combat.cc applies at commit.
//
// Stubbed or hand-built here: compute_attack and action_attack (the Attack is
// filled by hand and the sequence is one real animation of the attacker), the
// weapon's ammo count (a plain int), damage (a counter), art, sound, the map
// and the combat state. combat.cc and actions.cc are not linked. The missing
// native adapter is exactly the bodies of combat_attack()'s build, sequence
// and commit stages and of combat_finish_attack_if_ready()'s completion hooks.
// This test mirrors the wiring of those hooks, not their game effects.

#include "test_harness.h"

#include "animation_stubs.h"
#include "game/anim.h"
#include "game/art.h"
#include "game/object_types.h"
#include "object_handle.h"
#include "sim/attack_instance.h"
#include "sim/buffered_attack.h"

using namespace fallout;
using namespace fallout_test;

namespace {

struct Flow;
Flow* current = nullptr;

struct Flow {
    ObjectRegistry registry;
    Object player {};
    Object target {};
    Object other {};
    Object weapon {};
    SerializedAttack attacks;
    BufferedAttack buffer;
    uint64_t epoch = 7;
    int ap = 6;
    int ammo = 10;
    int actionPoints = 3;
    bool realtime = true;
    bool vatsFiring = false;
    int commits = 0;
    int debits = 0;
    int displays = 0;
    int damages = 0;
    int ends = 0;

    Flow()
    {
        animation_stub_reset();
        animationStub.art = true;
        player.fid = art_id(OBJ_TYPE_CRITTER, 1, ANIM_STAND, 0, 1);
        registry.create(&player);
        registry.create(&target);
        registry.create(&other);
        registry.create(&weapon);
        animation_stub_set_combat(true);
        anim_init();
        current = this;
        animationStub.leaseFinishedHook = [] { current->finishIfReady(); };
    }

    ~Flow()
    {
        anim_exit();
        current = nullptr;
    }

    // combat_finish_attack_if_ready() with the effects counted.
    void finishIfReady()
    {
        const int running = animation_stub_outstanding_leases();
        if (running != 0 || attacks.busy() || !attacks.active()) return;
        SerializedAttack::Completion hooks;
        hooks.debit = [this](void* resolved, int quantity) {
            CHECK(resolved == &weapon);
            ammo -= quantity;
            debits++;
        };
        hooks.display = [this](AttackInstance&) { displays++; };
        hooks.damage = [this](AttackInstance&) {
            damages++;
            return true;
        };
        hooks.endCombat = [this](AttackInstance&) {
            ends++;
            return false;
        };
        attacks.finish(running, registry, [this] { return epoch; }, [this] { return &player; }, hooks);
    }

    SerializedAttack::Finish tryFinish()
    {
        return attacks.finish(animation_stub_outstanding_leases(), registry, [this] { return epoch; }, [this] { return &player; }, {});
    }

    // The request names `other` and hit mode 5, which no fixture default does.
    BufferedAttackResult takeRequest()
    {
        buffer.put(buffered_attack_capture(registry, &player, &other, &weapon, false, 5, BufferedAttackKind::Ordinary, epoch, 1000));
        return buffer.take(registry, epoch, 1100, [this](const void* attacker, const void* held, int mode) {
            return attacker == &player && held == &weapon && mode == 5;
        });
    }

    // With a request the build stage resolves its identities and mode, as the
    // buffered fire path does. Without one it uses the fixture's objects.
    bool start(const BufferedAttackRequest* request = nullptr)
    {
        return attacks.start(
            animation_stub_outstanding_leases(), registry, [this] { return epoch; }, [this] { return &player; },
            [this, request](AttackInstance& instance) {
                if (request != nullptr) {
                    instance.attack.attacker = static_cast<Object*>(object_ref_resolve(registry, request->attacker, request->epoch));
                    instance.attack.defender = static_cast<Object*>(object_ref_resolve(registry, request->target, request->epoch));
                    instance.attack.weapon = static_cast<Object*>(object_ref_resolve(registry, request->weapon, request->epoch));
                    instance.attack.hitMode = request->hitMode;
                    if (instance.attack.attacker == nullptr || instance.attack.defender == nullptr || instance.attack.weapon == nullptr) return false;
                } else {
                    instance.attack.attacker = &player;
                    instance.attack.defender = &target;
                    instance.attack.weapon = &weapon;
                    instance.attack.hitMode = 1;
                }
                instance.attack.oops = instance.attack.defender;
                instance.attack.ammoQuantity = 1;
                instance.eligibleAmmo = true;
                instance.actionPoints = actionPoints;
                return true;
            },
            [this](AttackInstance& instance) {
                // action_attack() registers a sequence of the attacker.
                if (register_begin(ANIMATION_REQUEST_RESERVED) != 0) return false;
                if (register_object_animate(instance.attack.attacker, ANIM_STAND, 0) != 0) return false;
                return register_end() == 0;
            },
            [this](AttackInstance&) {
                commits++;
                if (attack_charges_ap(true, vatsFiring, realtime)) ap -= actionPoints;
                return true;
            });
    }

    int runAnimation()
    {
        int ticks = 0;
        while (attacks.active() && ticks < 200) {
            animationStub.now += 1000;
            object_animate();
            ticks++;
        }
        return ticks;
    }
};

} // namespace

TEST(attack_ap_rule_only_the_players_ordinary_realtime_attack_is_free)
{
    CHECK(!attack_charges_ap(true, false, true));
    CHECK(attack_charges_ap(true, true, true)); // VATS pays.
    CHECK(attack_charges_ap(true, false, false)); // Turn based pays.
    CHECK(attack_charges_ap(false, false, true)); // Everyone else pays.
    CHECK(attack_charges_ap(false, true, false));
}

TEST(buffered_attack_runs_to_one_committed_shot_through_the_real_animation)
{
    Flow flow;
    BufferedAttackResult taken = flow.takeRequest();
    CHECK(taken.reason == BufferedAttackReject::None);
    CHECK(taken.request.has_value());
    CHECK(!flow.buffer.pending());

    CHECK(flow.start(&*taken.request));
    CHECK(flow.attacks.active() != nullptr);
    CHECK(flow.attacks.active()->phase == AttackPhase::Sealed);

    // The request's identities and mode reached the attack instance.
    const Attack& attack = flow.attacks.active()->attack;
    CHECK(attack.attacker == &flow.player);
    CHECK(attack.defender == &flow.other);
    CHECK(attack.defender != &flow.target);
    CHECK(attack.weapon == &flow.weapon);
    CHECK_EQ(attack.hitMode, 5);

    // Committed at start: one sequence, one lease, the ordinary attack is free.
    // Nothing lands until the animation ends.
    CHECK_EQ(flow.commits, 1);
    CHECK_EQ(flow.ap, 6);
    CHECK_EQ(animationStub.leaseBegins, 1);
    CHECK_EQ(animation_stub_outstanding_leases(), 1);
    CHECK_EQ(flow.ammo, 10);
    CHECK_EQ(flow.damages, 0);
    CHECK(flow.tryFinish() == SerializedAttack::Finish::NotReady);

    CHECK(flow.runAnimation() > 0);

    // The real engine finished the lease and the attack completed once.
    CHECK(flow.attacks.active() == nullptr);
    CHECK_EQ(animationStub.leaseFinishes, 1);
    CHECK_EQ(animation_stub_outstanding_leases(), 0);
    CHECK_EQ(flow.debits, 1);
    CHECK_EQ(flow.ammo, 9);
    CHECK_EQ(flow.displays, 1);
    CHECK_EQ(flow.damages, 1);
    CHECK_EQ(flow.ends, 1);
    CHECK_EQ(flow.commits, 1);
    CHECK_EQ(flow.ap, 6);

    // Later ticks and a second completion attempt change nothing.
    for (int i = 0; i < 10; i++) {
        animationStub.now += 1000;
        object_animate();
    }
    flow.finishIfReady();
    CHECK(flow.tryFinish() == SerializedAttack::Finish::NotReady);
    CHECK_EQ(flow.debits, 1);
    CHECK_EQ(flow.ammo, 9);
    CHECK_EQ(flow.damages, 1);
    CHECK_EQ(flow.displays, 1);
    CHECK_EQ(animationStub.leaseFinishes, 1);
    CHECK_EQ(animation_stub_outstanding_leases(), 0);
}

TEST(vats_shot_charges_its_action_points_once_at_commit)
{
    Flow flow;
    flow.vatsFiring = true;
    CHECK(flow.start());
    CHECK_EQ(flow.ap, 3);
    CHECK(flow.runAnimation() > 0);
    CHECK_EQ(flow.ap, 3);
    CHECK_EQ(flow.damages, 1);
    CHECK_EQ(flow.ammo, 9);
}

TEST(second_attack_is_refused_while_the_first_animates_and_leases_stay_balanced)
{
    Flow flow;
    CHECK(flow.start());
    CHECK(!flow.start());
    CHECK_EQ(flow.commits, 1);
    CHECK_EQ(animationStub.leaseBegins, 1);
    CHECK(flow.runAnimation() > 0);
    CHECK_EQ(flow.damages, 1);
    CHECK_EQ(flow.ammo, 9);
    CHECK_EQ(animation_stub_outstanding_leases(), 0);

    // The slot is free again for the next attack.
    CHECK(flow.start());
    CHECK(flow.runAnimation() > 0);
    CHECK_EQ(flow.damages, 2);
    CHECK_EQ(flow.ammo, 8);
    CHECK_EQ(animationStub.leaseBegins, 2);
    CHECK_EQ(animationStub.leaseFinishes, 2);
    CHECK_EQ(animation_stub_outstanding_leases(), 0);
}
