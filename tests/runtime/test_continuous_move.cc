#include "game/object_types.h"
#include "sim/continuous_move.h"
#include "sim/movement_owner.h"
#include "sim/sim_clock.h"
#include "test_harness.h"
#include <cmath>
using namespace fallout;

TEST(continuous_move_authored_speed_nonuniform_and_zero_frames)
{
    AuthoredStride stride;
    CHECK(stride.build({ { 0, 0 }, { 2, 0 }, { 0, 0 }, { 6, 0 } }, 8));
    CHECK(stride.distance() == 8);
    CHECK(stride.speed() == 16);
    CHECK(stride.frame(0) == 0);
    CHECK(stride.frame(0.25) == 2);
    CHECK(!stride.build({ { 0, 0 }, { 0, 0 } }, 8));
}

TEST(continuous_move_render_schedules_and_timestamped_stop)
{
    for (int schedule : { 30, 60, 144, 0 }) {
        AuthoredStride stride;
        CHECK(stride.build({ { 3, 0 }, { 0, 0 }, { 9, 0 } }, 20));
        ContinuousMove move;
        SimClock clock;
        int x = 0, y = 0;
        bool held = true;
        auto tick = [&] { if (held) CHECK(move.tick(stride, 1, 0, 1, [&](int dx, int dy) { x += dx; y += dy; return MoveStep {true, dx, dy}; })); };
        clock.advance(0, nullptr, tick);
        unsigned now = 0;
        for (unsigned event : { 1000u, 1500u, 2500u }) {
            int n = 0;
            while (now < event) {
                unsigned next = schedule == 0 ? now + (n++ % 3 == 0 ? 31 : 7) : static_cast<unsigned>((++n * 1000ULL) / schedule) + (event == 1000 ? 0 : event == 1500 ? 1000
                                                                                                                                                                       : 1500);
                if (next <= now) next = now + 1;
                if (next > event) next = event;
                now = next;
                clock.advance(now, nullptr, tick);
            }
            // Drain old intent before publishing the timestamped edge.
            held = event == 1500;
            if (!held) move.reset();
        }
        CHECK(x == 160);
        CHECK(y == 0);
    }
}

TEST(continuous_move_diagonal_slide_blocked_and_neutral)
{
    AuthoredStride stride;
    CHECK(stride.build({ { 60, 0 } }, 1));
    ContinuousMove move;
    int x = 0, y = 0;
    for (int i = 0; i < 60; i++)
        CHECK(move.tick(stride, 1, 1, 1, [&](int dx, int dy) {x+=dx;y+=dy;return MoveStep{true,dx,dy}; }));
    CHECK(x == 42 && y == 42);
    move.reset();
    for (int i = 0; i < 600; i++)
        CHECK(move.tick(stride, 1, 0, 1, [](int, int) { return MoveStep {}; }));
    CHECK(move.phase() == 0);
    CHECK(move.tick(stride, 1, 0, 1, [&](int dx, int dy) {x+=dx;y+=dy;return MoveStep{true,dx,dy}; }));
    CHECK(x == 43);
    move.reset();
    x = y = 0;
    for (int i = 0; i < 60; i++)
        CHECK(move.tick(stride, 1, 1, 1, [&](int dx, int dy) {if(dy)return MoveStep{};x+=dx;return MoveStep{true,dx,0}; }));
    CHECK(x == 42 && y == 0);
    CHECK(move.tick(stride, 0, 0, 1, [](int, int) { return MoveStep {}; }));
    CHECK(move.phase() == 0);
}

TEST(continuous_move_owner_invalidation_stops_remaining_pixel_substeps)
{
    ObjectRegistry registry;
    Object actor {};
    registry.create(&actor);
    MovementOwner owner;
    auto token = owner.acquire(registry, &actor, 4, false);
    AuthoredStride stride;
    CHECK(stride.build({ { 600, 0 } }, 1));
    ContinuousMove move;
    int calls = 0;
    CHECK(!move.tick(stride, 1, 0, 1, [&](int, int) {
        calls++;
        registry.destroy(&actor);
        registry.create(&actor);
        return MoveStep { owner.resolve(token, registry, 4, &actor) != nullptr, 0, 0 };
    }));
    CHECK(calls == 1);
}

TEST(continuous_move_preserves_phase_across_authored_profiles)
{
    AuthoredStride walk, run;
    CHECK(walk.build({ { 0, 0 }, { 2, 0 }, { 0, 0 }, { 6, 0 } }, 8));
    CHECK(run.build({ { 8, 0 }, { 0, 0 }, { 8, 0 }, { 0, 0 } }, 12));
    ContinuousMove move;
    for (int i = 0; i < 20; i++)
        CHECK(move.tick(walk, 1, 0, 1, [](int x, int y) { return MoveStep { true, x, y }; }));
    double phase = move.phase();
    CHECK(phase > 0);
    CHECK(move.tick(run, 1, 0, 0, [](int x, int y) { return MoveStep { true, x, y }; }));
    CHECK(move.phase() == phase);
    CHECK(run.frame(phase) >= 0 && run.frame(phase) < 4);
}

TEST(continuous_move_pause_and_epoch_player_transfer_stop_callback)
{
    for (int mutation = 0; mutation < 3; mutation++) {
        ObjectRegistry registry;
        Object actor {}, other {};
        registry.create(&actor);
        registry.create(&other);
        MovementOwner owner;
        auto token = owner.acquire(registry, &actor, 2, false);
        Object* player = &actor;
        uint64_t epoch = 2;
        AuthoredStride stride;
        CHECK(stride.build({ { 600, 0 } }, 1));
        ContinuousMove move;
        int calls = 0;
        CHECK(!move.tick(stride, 1, 0, 1, [&](int, int) {
            calls++;
            if (mutation == 0) epoch++;
            if (mutation == 1) player = &other;
            if (mutation == 2) {
                owner.yield(&actor);
                CHECK(owner.acquire(registry, &actor, epoch, false) != 0);
            }
            return MoveStep { owner.resolve(token, registry, epoch, player) != nullptr, 0, 0 };
        }));
        CHECK(calls == 1);
    }
    AuthoredStride stride;
    CHECK(stride.build({ { 60, 0 } }, 1));
    ContinuousMove move;
    SimClock clock;
    PauseSet pauses;
    int position = 0;
    auto tick = [&] { CHECK(move.tick(stride, 1, 0, 1, [&](int x, int y) {position+=x;return MoveStep{true,x,y}; })); };
    clock.advance(0, &pauses, tick);
    clock.advance(100, &pauses, tick);
    CHECK(position == 6);
    auto pause = pauses.acquire();
    clock.advance(200, &pauses, tick);
    CHECK(position == 6);
    pause.release();
    clock.advance(300, &pauses, tick);
    CHECK(position == 6);
    clock.advance(400, &pauses, tick);
    CHECK(position == 12);
}

TEST(continuous_move_door_hit_then_slide_clears_contact_only_final_block_queues)
{
    ObjectRegistry registry;
    Object door {};
    registry.create(&door);
    ObjectRef ref = object_ref_make(registry, &door, 1);
    AuthoredStride stride;
    CHECK(stride.build({ { 60, 0 } }, 1));
    for (bool slide : { false, true }) {
        ContinuousMove move;
        MovementContact contact;
        int progress = 0;
        CHECK(move.tick(stride, 1, 1, 1, [&](int x, int y) {
            bool moved = slide && y == 0;
            contact.record(moved, ref);
            if (moved) {
                progress += x;
                return MoveStep { true, x, 0 };
            }
            return MoveStep {};
        }));
        CHECK(contact.blockedDoor().handle.valid() == !slide);
        CHECK(progress == (slide ? 1 : 0));
        contact.record(false, {}); // Failed fallback preserves a prior door hit.
        CHECK(contact.blockedDoor().handle.valid() == !slide);
        contact.record(true, {});
        CHECK(!contact.blockedDoor().handle.valid());
    }
}

TEST(continuous_move_failed_no_door_slide_retains_diagonal_door_contact)
{
    ObjectRegistry registry;
    Object door {};
    registry.create(&door);
    ObjectRef ref = object_ref_make(registry, &door, 1);
    AuthoredStride stride;
    CHECK(stride.build({ { 60, 0 } }, 1));
    ContinuousMove move;
    MovementContact contact;
    int attempts = 0;
    CHECK(move.tick(stride, 1, 1, 1, [&](int x, int y) {
        attempts++;
        contact.record(false, x != 0 && y != 0 ? ref : ObjectRef {});
        return MoveStep {};
    }));
    CHECK(attempts == 3);
    CHECK(object_ref_resolve(registry, contact.blockedDoor(), 1) == &door);
    contact.record(true, {});
    CHECK(!contact.blockedDoor().handle.valid());
    contact.record(false, {});
    CHECK(!contact.blockedDoor().handle.valid());
}
