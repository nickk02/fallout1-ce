#include "test_harness.h"

#include "animation_stubs.h"
#include "game/anim.h"
#include "game/art.h"
#include "game/object_types.h"
#include "object_handle.h"
#include "sim/route_follower.h"

using namespace fallout;
using namespace fallout_test;

namespace {
int count_callback(void* count, void*)
{
    ++*static_cast<int*>(count);
    return 0;
}
}

TEST(anim_public_registration_runs_successful_callback_once)
{
    animation_stub_reset();
    anim_init();
    animation_stub_set_combat(true);
    int calls = 0;
    CHECK_EQ(register_begin(ANIMATION_REQUEST_RESERVED), 0);
    CHECK_EQ(register_object_call(&calls, nullptr, count_callback, 0), 0);
    CHECK_EQ(calls, 0);
    CHECK_EQ(register_end(), 0);
    CHECK_EQ(calls, 1);
    CHECK_EQ(animationStub.leaseBegins, 1);
    CHECK_EQ(animationStub.leaseFinishes, 1);
    anim_stop();
    CHECK_EQ(calls, 1);
    CHECK_EQ(animation_stub_outstanding_leases(), 0);
    anim_exit();
}

namespace {

// The rule free_move.cc applies to every yield, with the route and registry of
// the test. free_move.cc itself is not linked here.
RouteFollower* hookRoute = nullptr;
ObjectRegistry* hookRegistry = nullptr;
uint64_t hookStandSerial = 0;

void route_yield_hook(Object* actor)
{
    if (!hookRoute->active()) return;
    if (hookRoute->actor(*hookRegistry, 1) != actor) return;
    const uint64_t serial = anim_auto_stand_serial();
    if (serial != 0) hookStandSerial = serial;
    hookRoute->noteYield(false, serial);
}

struct DoorRoute {
    ObjectRegistry registry;
    Object player {};
    Object door {};
    RouteFollower route;
    uint64_t sequence = 0;

    DoorRoute()
    {
        player.fid = art_id(OBJ_TYPE_CRITTER, 1, ANIM_STAND, 0, 1);
        player.tile = 100;
        registry.create(&player);
        registry.create(&door);
        hookRoute = &route;
        hookRegistry = &registry;
        hookStandSerial = 0;
        animation_stub_reset();
        animationStub.art = true;
        animationStub.yieldHook = route_yield_hook;
        anim_init();
    }

    ~DoorRoute()
    {
        anim_exit();
        hookRoute = nullptr;
    }

    // A route to tile 105 that met the door and asked for it.
    void requestDoor()
    {
        auto plan = [](int, int, std::vector<int>& tiles) {
            tiles = { 101, 102, 103, 104, 105 };
            return 5;
        };
        CHECK(route.start(registry, &player, 1, 100, 105, plan) == RouteFollower::Start::Started);
        CHECK(route.requestDoor(registry, 1, &player, &door));
    }

    // Registers the door action's own animation inside the handoff and leaves
    // it running, then ends the handoff with the sequence it created.
    void handOff()
    {
        const uint64_t mark = anim_serial_mark();
        CHECK(route.beginHandoff(registry, 1, &player, &door));
        CHECK_EQ(register_begin(ANIMATION_REQUEST_RESERVED), 0);
        CHECK_EQ(register_object_animate(&player, ANIM_STAND, 0), 0);
        CHECK_EQ(register_end(), 0);
        uint64_t sequences[8];
        const int count = anim_actor_sequences_since(&player, mark, sequences, 8);
        CHECK_EQ(count, 1);
        sequence = sequences[0];
        CHECK(route.endHandoff(true, sequences, count));
        CHECK(route.phase() == RouteFollower::Phase::WaitingDoor);
        CHECK(anim_actor_has_work(&player));
    }

    // Runs the animation engine until the player has no work left.
    int finish()
    {
        int ticks = 0;
        while (anim_actor_has_work(&player) && ticks < 200) {
            animationStub.now += 1000;
            object_animate();
            ticks++;
        }
        return ticks;
    }
};

} // namespace

// The real anim.cc steps a registered sequence to its end and stands the player
// up on its own. That stand is a yield made long after the handoff returned. It
// must keep an owned door route and the goal. Art, sound, scripts and the map
// are stubs, and the door action itself (actions.cc, the door object and its
// use script) is not run: the sequence is a real one-step animation of the
// player registered where the door action registers.
TEST(anim_automatic_completion_stand_keeps_an_owned_door_route)
{
    DoorRoute fixture;
    fixture.requestDoor();
    fixture.handOff();

    const int yieldsBefore = animationStub.yields;
    CHECK(fixture.finish() > 0);
    CHECK(!anim_actor_has_work(&fixture.player));

    // The engine yielded for its own end-of-sequence stand, and the route lived.
    CHECK(animationStub.yields > yieldsBefore);
    CHECK_EQ(hookStandSerial, fixture.sequence);
    CHECK(fixture.route.phase() == RouteFollower::Phase::WaitingDoor);
    CHECK_EQ(fixture.route.goal(), 105);
    CHECK_EQ(FID_ANIM_TYPE(fixture.player.fid), ANIM_STAND);
}

TEST(anim_foreign_registration_cancels_an_owned_door_route_while_it_runs)
{
    DoorRoute fixture;
    fixture.requestDoor();
    fixture.handOff();

    // Another action asks for the player while the door sequence is running.
    // The registration is refused because the player is busy, but it yields
    // first, and that yield is not the engine's automatic stand.
    CHECK_EQ(register_begin(ANIMATION_REQUEST_RESERVED), 0);
    register_object_turn_towards(&fixture.player, 7);
    register_end();

    CHECK(fixture.route.phase() == RouteFollower::Phase::Idle);
    CHECK_EQ(fixture.route.goal(), -1);
    fixture.finish();
}

TEST(anim_foreign_registration_after_the_owned_stand_cancels_the_route)
{
    DoorRoute fixture;
    fixture.requestDoor();
    fixture.handOff();
    fixture.finish();
    CHECK(fixture.route.phase() == RouteFollower::Phase::WaitingDoor);

    // The player is idle again, so this registration succeeds, and it still
    // ends the route.
    CHECK_EQ(register_begin(ANIMATION_REQUEST_RESERVED), 0);
    CHECK_EQ(register_object_animate(&fixture.player, ANIM_STAND, 0), 0);
    CHECK_EQ(register_end(), 0);
    CHECK(fixture.route.phase() == RouteFollower::Phase::Idle);
    fixture.finish();
}

namespace {
int registerAgainCalls = 0;
int register_again(void* player, void*)
{
    registerAgainCalls++;
    // A callback of the owned sequence asks for new player work. The player is
    // still busy with this sequence, so the registration is refused, but it
    // yields first.
    register_begin(ANIMATION_REQUEST_RESERVED);
    register_object_turn_towards(static_cast<Object*>(player), 7);
    register_end();
    return 0;
}
}

// Owning the sequence does not make everything its callbacks do ours.
TEST(anim_registration_inside_an_owned_callback_cancels_the_route)
{
    DoorRoute fixture;
    fixture.requestDoor();

    const uint64_t mark = anim_serial_mark();
    CHECK(fixture.route.beginHandoff(fixture.registry, 1, &fixture.player, &fixture.door));
    CHECK_EQ(register_begin(ANIMATION_REQUEST_RESERVED), 0);
    CHECK_EQ(register_object_animate(&fixture.player, ANIM_STAND, 0), 0);
    CHECK_EQ(register_object_call(&fixture.player, nullptr, register_again, -1), 0);
    CHECK_EQ(register_end(), 0);
    uint64_t sequences[8];
    const int count = anim_actor_sequences_since(&fixture.player, mark, sequences, 8);
    CHECK_EQ(count, 1);
    CHECK(fixture.route.endHandoff(true, sequences, count));
    CHECK(fixture.route.phase() == RouteFollower::Phase::WaitingDoor);

    // The callback waits for the animation (delay -1), then registers. The route ends there,
    // before the sequence's own completion stand.
    registerAgainCalls = 0;
    CHECK(fixture.finish() > 0);
    CHECK_EQ(registerAgainCalls, 1);
    CHECK(fixture.route.phase() == RouteFollower::Phase::Idle);
}
