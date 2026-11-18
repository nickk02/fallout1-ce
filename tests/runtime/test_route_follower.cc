#include "game/object_types.h"
#include "sim/route_follower.h"
#include "sim/sequence_serial.h"
#include "test_harness.h"

#include <cmath>
#include <queue>
#include <set>

using namespace fallout;

namespace {

constexpr int kWidth = 10;
constexpr int kHeight = 10;
constexpr double kCell = 32;

int cell(int x, int y)
{
    return y * kWidth + x;
}

// A square map with four-way moves. Walls block. Locked doors block. Other
// doors are planned through, as the game's route planner does.
struct Grid {
    std::set<int> walls;
    std::set<int> lockedDoors;
    int calls = 0;

    RouteFollower::Plan planner()
    {
        return [this](int from, int goal, std::vector<int>& tiles) {
            calls++;
            std::vector<int> parent(kWidth * kHeight, -2);
            std::queue<int> open;
            parent[from] = -1;
            open.push(from);
            while (!open.empty()) {
                int at = open.front();
                open.pop();
                if (at == goal) {
                    break;
                }
                const int dx[] = { 1, -1, 0, 0 };
                const int dy[] = { 0, 0, 1, -1 };
                for (int i = 0; i < 4; i++) {
                    int nx = at % kWidth + dx[i];
                    int ny = at / kWidth + dy[i];
                    if (nx < 0 || ny < 0 || nx >= kWidth || ny >= kHeight) {
                        continue;
                    }
                    int next = cell(nx, ny);
                    if (parent[next] != -2 || walls.count(next) != 0 || lockedDoors.count(next) != 0) {
                        continue;
                    }
                    parent[next] = at;
                    open.push(next);
                }
            }
            if (parent[goal] == -2) {
                return 0;
            }
            tiles.clear();
            for (int at = goal; at != from; at = parent[at]) {
                tiles.insert(tiles.begin(), at);
            }
            return static_cast<int>(tiles.size());
        };
    }
};

bool centerOf(int tile, double& x, double& y)
{
    x = (tile % kWidth) * kCell;
    y = (tile / kWidth) * kCell;
    return true;
}

// Moves a point along the heading and reports the cell it is nearest to.
struct Walker {
    double x;
    double y;

    explicit Walker(int tile)
    {
        centerOf(tile, x, y);
    }

    int tile() const
    {
        int cx = static_cast<int>(std::lround(x / kCell));
        int cy = static_cast<int>(std::lround(y / kCell));
        return cell(cx, cy);
    }

    void move(const RouteFollower::Steering& s, double speed)
    {
        x += s.x * speed;
        y += s.y * speed;
    }
};

struct World {
    ObjectRegistry registry;
    Object player {};
    Object doorA {};
    Object doorB {};
    uint64_t epoch = 1;

    World()
    {
        registry.create(&player);
        registry.create(&doorA);
        registry.create(&doorB);
    }
};

RouteFollower::Steering tick(RouteFollower& route, World& w, const Walker& walker, Grid& grid)
{
    return route.follow(w.registry, w.epoch, &w.player, walker.tile(), walker.x, walker.y, centerOf, grid.planner());
}

// Drives a route to a door request, through the handoff, into the wait.
bool reachWait(RouteFollower& route, World& w, Object* door, const std::vector<uint64_t>& sequences = {})
{
    return route.requestDoor(w.registry, w.epoch, &w.player, door)
        && route.beginHandoff(w.registry, w.epoch, &w.player, door)
        && route.endHandoff(true, sequences.data(), static_cast<int>(sequences.size()))
        && route.phase() == RouteFollower::Phase::WaitingDoor;
}

} // namespace

TEST(route_detour_around_a_wall_is_not_a_stall)
{
    World w;
    Grid grid;
    for (int y = 0; y < kHeight - 1; y++) {
        grid.walls.insert(cell(3, y));
    }

    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(6, 0), grid.planner()) == RouteFollower::Start::Started);

    Walker walker(cell(0, 0));
    double goalX, goalY;
    centerOf(cell(6, 0), goalX, goalY);
    const double startDistance = std::hypot(goalX - walker.x, goalY - walker.y);
    double farthest = 0;
    bool arrived = false;
    for (int i = 0; i < 600 && !arrived; i++) {
        RouteFollower::Steering s = tick(route, w, walker, grid);
        CHECK(s.act != RouteFollower::Act::Abandon);
        if (s.act == RouteFollower::Act::Arrived) {
            arrived = true;
            break;
        }
        walker.move(s, 4);
        farthest = std::fmax(farthest, std::hypot(goalX - walker.x, goalY - walker.y));
    }

    CHECK(arrived);
    CHECK(farthest > startDistance + 64); // The way out leads away from the goal.
    CHECK(route.replans() == 0);
    CHECK(route.phase() == RouteFollower::Phase::Idle);
}

TEST(route_stall_replans_three_times_then_abandons)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(5, 0), grid.planner()) == RouteFollower::Start::Started);

    Walker walker(cell(0, 0));
    int ticks = 0;
    RouteFollower::Act last = RouteFollower::Act::Steer;
    while (ticks < 400 && last == RouteFollower::Act::Steer) {
        last = tick(route, w, walker, grid).act;
        ticks++;
    }

    CHECK(last == RouteFollower::Act::Abandon);
    CHECK(ticks >= 4 * 42);
    CHECK(grid.calls == 4); // The first plan and three replans.
    CHECK(route.phase() == RouteFollower::Phase::Idle);
}

TEST(route_progress_resets_the_stall_clock)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);

    // Creeping 3 px a tick is progress: no replan in a long walk.
    Walker walker(cell(0, 0));
    for (int i = 0; i < 90; i++) {
        RouteFollower::Steering s = tick(route, w, walker, grid);
        CHECK(s.act == RouteFollower::Act::Steer);
        walker.move(s, 3);
    }

    CHECK(route.replans() == 0);
    CHECK(grid.calls == 1);
}

TEST(route_unreachable_rejected_and_same_tile_is_arrival)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(4, 0), grid.planner()) == RouteFollower::Start::Started);
    const uint64_t generation = route.generation();

    // A walled-in goal is a new intent: rejected, and the old route is gone.
    grid.walls = { cell(8, 8), cell(9, 8), cell(8, 9) };
    const int before = grid.calls;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 9), grid.planner()) == RouteFollower::Start::Rejected);
    CHECK(grid.calls == before + 1);
    CHECK(route.phase() == RouteFollower::Phase::Idle);
    CHECK(route.goal() == -1);
    Walker walker(cell(0, 0));
    CHECK(tick(route, w, walker, grid).act == RouteFollower::Act::Abandon);

    // The tile the player stands on ends the old route as an arrival, and the
    // planner is not asked.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(4, 0), grid.planner()) == RouteFollower::Start::Started);
    const int mid = grid.calls;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(4, 0), cell(4, 0), grid.planner()) == RouteFollower::Start::Arrived);
    CHECK(grid.calls == mid);
    CHECK(route.phase() == RouteFollower::Phase::Idle);
    CHECK(route.generation() > generation);

    // A pending door handoff of the old route cannot start after a new click.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(2, 2), cell(2, 2), grid.planner()) == RouteFollower::Start::Arrived);
    CHECK(!route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.door(w.registry, w.epoch) == nullptr);

    // A plan that does not end on the goal is no route.
    auto wrong = [](int, int, std::vector<int>& tiles) {
        tiles = { 1, 2 };
        return 2;
    };
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(5, 5), wrong) == RouteFollower::Start::Rejected);

    // Not a live actor.
    Object stranger {};
    CHECK(route.start(w.registry, &stranger, w.epoch, cell(0, 0), cell(5, 5), grid.planner()) == RouteFollower::Start::Rejected);
}

TEST(route_multi_door_allows_two_attempts_per_door)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);

    Object* doors[] = { &w.doorA, &w.doorA, &w.doorB, &w.doorB };
    for (Object* door : doors) {
        CHECK(reachWait(route, w, door));
        CHECK(route.goal() == cell(9, 0)); // The goal survives the handoff.
        CHECK(route.pollDoor(w.registry, w.epoch, &w.player, true, false) == RouteFollower::DoorWait::Waiting);
        CHECK(route.pollDoor(w.registry, w.epoch, &w.player, false, true) == RouteFollower::DoorWait::Resume);
        CHECK(route.resume(cell(1, 0), grid.planner()));
        CHECK(route.phase() == RouteFollower::Phase::Following);
    }

    CHECK(grid.calls == 5);

    // A third request on a door that was tried twice ends the route.
    CHECK(!route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.phase() == RouteFollower::Phase::Idle);
}

TEST(route_door_wait_resumes_only_when_idle_and_the_door_is_open)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA));

    // The action runs and the door is still shut: busy, then idle and closed.
    // The player does not walk into it again.
    for (int i = 0; i < 20; i++) {
        CHECK(route.pollDoor(w.registry, w.epoch, &w.player, true, false) == RouteFollower::DoorWait::Waiting);
    }
    for (int i = 0; i < 50; i++) {
        CHECK(route.pollDoor(w.registry, w.epoch, &w.player, false, false) == RouteFollower::DoorWait::Waiting);
    }

    // Open but the player still has work: wait.
    CHECK(route.pollDoor(w.registry, w.epoch, &w.player, true, true) == RouteFollower::DoorWait::Waiting);
    // Open and idle: go.
    CHECK(route.pollDoor(w.registry, w.epoch, &w.player, false, true) == RouteFollower::DoorWait::Resume);
    CHECK(route.resume(cell(1, 0), grid.planner()));
    CHECK(route.phase() == RouteFollower::Phase::Following);

    // A door that never opens ends at the deadline and never resumes.
    CHECK(reachWait(route, w, &w.doorB));
    int polls = 0;
    RouteFollower::DoorWait last = RouteFollower::DoorWait::Waiting;
    while (last == RouteFollower::DoorWait::Waiting && polls < 1000) {
        last = route.pollDoor(w.registry, w.epoch, &w.player, false, false);
        polls++;
    }
    CHECK(last == RouteFollower::DoorWait::Cancel);
    CHECK(polls == 241);
    CHECK(route.phase() == RouteFollower::Phase::Idle);
}

TEST(route_owned_door_yield_survives_and_foreign_yield_cancels)
{
    World w;
    Grid grid;
    RouteFollower route;

    // Own registration inside the handoff keeps the goal.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(!route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA)); // One handoff at a time.
    CHECK(route.noteYield(false));
    CHECK(route.noteYield(false));
    CHECK(route.endHandoff(true));
    CHECK(route.phase() == RouteFollower::Phase::WaitingDoor);
    CHECK(route.goal() == cell(9, 0));

    // The same yield after the handoff is somebody else's.
    CHECK(!route.noteYield(false));
    CHECK(route.phase() == RouteFollower::Phase::Idle);

    // A script or action that registers inside the window is foreign.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.noteYield(false));
    CHECK(!route.noteYield(true));
    CHECK(route.phase() == RouteFollower::Phase::Idle);
    CHECK(!route.endHandoff(true)); // The dispatch result cannot revive it.
    CHECK(route.phase() == RouteFollower::Phase::Idle);

    // A yield before any handoff is not ours either.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(!route.noteYield(false));

    // A door action that did not run ends the route.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(!route.endHandoff(false));
    CHECK(route.phase() == RouteFollower::Phase::Idle);
}

TEST(route_door_wait_times_out)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA));

    for (int i = 0; i < 240; i++) {
        CHECK(route.pollDoor(w.registry, w.epoch, &w.player, true, false) == RouteFollower::DoorWait::Waiting);
    }

    CHECK(route.pollDoor(w.registry, w.epoch, &w.player, true, false) == RouteFollower::DoorWait::Cancel);
    CHECK(route.phase() == RouteFollower::Phase::Idle);
}

TEST(route_stale_actor_door_epoch_and_generation_reject)
{
    // A different player.
    {
        World w;
        Grid grid;
        RouteFollower route;
        Object other {};
        w.registry.create(&other);
        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
        CHECK(reachWait(route, w, &w.doorA));
        CHECK(route.pollDoor(w.registry, w.epoch, &other, false, true) == RouteFollower::DoorWait::Cancel);
        CHECK(route.phase() == RouteFollower::Phase::Idle);
    }

    // The door is freed during the wait.
    {
        World w;
        Grid grid;
        RouteFollower route;
        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
        CHECK(reachWait(route, w, &w.doorA));
        CHECK(w.registry.destroy(&w.doorA));
        CHECK(route.pollDoor(w.registry, w.epoch, &w.player, false, true) == RouteFollower::DoorWait::Cancel);
    }

    // The actor is freed and its slot reused by a new object.
    {
        World w;
        Grid grid;
        RouteFollower route;
        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
        CHECK(w.registry.destroy(&w.player));
        Object reuse {};
        w.registry.create(&reuse);
        Walker walker(cell(0, 0));
        CHECK(route.follow(w.registry, w.epoch, &reuse, walker.tile(), walker.x, walker.y, centerOf, grid.planner()).act == RouteFollower::Act::Abandon);
    }

    // The epoch moves on in the wait, in a follow and at a door request.
    {
        World w;
        Grid grid;
        RouteFollower route;
        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
        CHECK(reachWait(route, w, &w.doorA));
        CHECK(route.pollDoor(w.registry, w.epoch + 1, &w.player, false, true) == RouteFollower::DoorWait::Cancel);

        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
        Walker walker(cell(0, 0));
        CHECK(route.follow(w.registry, w.epoch + 1, &w.player, walker.tile(), walker.x, walker.y, centerOf, grid.planner()).act == RouteFollower::Act::Abandon);

        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
        CHECK(!route.requestDoor(w.registry, w.epoch + 1, &w.player, &w.doorA));
    }

    // A handoff for a route that was replaced, for the wrong door, the wrong
    // actor or another epoch.
    {
        World w;
        Grid grid;
        RouteFollower route;
        Object other {};
        w.registry.create(&other);
        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
        CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
        CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(5, 5), grid.planner()) == RouteFollower::Start::Started);
        CHECK(!route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
        CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorB));
        CHECK(!route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
        CHECK(!route.beginHandoff(w.registry, w.epoch, &other, &w.doorB));
        CHECK(!route.beginHandoff(w.registry, w.epoch + 1, &w.player, &w.doorB));
        CHECK(!route.beginHandoff(w.registry, w.epoch, &w.player, nullptr));
        CHECK(route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorB));
    }
}

TEST(route_cancel_from_a_stick_ends_every_phase)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    const uint64_t generation = route.generation();
    route.cancel();
    CHECK(route.phase() == RouteFollower::Phase::Idle);
    CHECK(route.generation() == generation);
    Walker walker(cell(0, 0));
    CHECK(tick(route, w, walker, grid).act == RouteFollower::Act::Abandon);

    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA));
    route.cancel();
    CHECK(route.pollDoor(w.registry, w.epoch, &w.player, false, true) == RouteFollower::DoorWait::Cancel);
    CHECK(!route.resume(cell(0, 0), grid.planner()));

    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    route.cancel();
    CHECK(!route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
}

TEST(route_planner_is_not_reentered_and_calls_are_capped)
{
    World w;
    Grid grid;
    RouteFollower route;

    // A planner that starts another route from inside is refused.
    RouteFollower::Start inner = RouteFollower::Start::Started;
    auto reentrant = [&](int from, int goal, std::vector<int>& tiles) {
        inner = route.start(w.registry, &w.player, w.epoch, from, goal, grid.planner());
        return grid.planner()(from, goal, tiles);
    };
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), reentrant) == RouteFollower::Start::Started);
    CHECK(inner == RouteFollower::Start::Rejected);

    // The planner cap stops a route that keeps asking, whatever the door count.
    RouteFollower::Config config;
    config.plannerCalls = 3;
    RouteFollower capped(config);
    Grid counted;
    CHECK(capped.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), counted.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(capped, w, &w.doorA));
    CHECK(capped.resume(cell(1, 0), counted.planner()));
    CHECK(reachWait(capped, w, &w.doorB));
    CHECK(capped.resume(cell(1, 0), counted.planner()));
    CHECK(reachWait(capped, w, &w.doorA));
    CHECK(!capped.resume(cell(1, 0), counted.planner()));
    CHECK(capped.phase() == RouteFollower::Phase::Idle);
    CHECK(counted.calls == 3);
}

TEST(route_replan_goes_around_a_new_blocker)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(4, 0), grid.planner()) == RouteFollower::Start::Started);

    // The walker reaches cell 2 and a new wall closes cell 3, so progress stops.
    Walker walker(cell(0, 0));
    for (int i = 0; i < 40; i++) {
        RouteFollower::Steering s = tick(route, w, walker, grid);
        CHECK(s.act == RouteFollower::Act::Steer);
        walker.move(s, 4);
        if (walker.tile() == cell(2, 0)) {
            break;
        }
    }
    CHECK(walker.tile() == cell(2, 0));
    grid.walls.insert(cell(3, 0));

    bool steered = false;
    for (int i = 0; i < 60 && route.replans() == 0; i++) {
        RouteFollower::Steering s = tick(route, w, walker, grid);
        steered = s.act == RouteFollower::Act::Steer;
        CHECK(steered);
    }

    // The walker holds still against the wall. The replan routes around it.
    CHECK(route.replans() == 1);
    bool arrived = false;
    for (int i = 0; i < 600 && !arrived; i++) {
        RouteFollower::Steering s = tick(route, w, walker, grid);
        CHECK(s.act != RouteFollower::Act::Abandon);
        arrived = s.act == RouteFollower::Act::Arrived;
        walker.move(s, 4);
    }
    CHECK(arrived);
    CHECK(grid.calls == 2); // The first plan and one replan.
}

TEST(route_door_request_needs_nothing_carried_from_the_halt_to_the_dispatch)
{
    World w;
    Grid grid;
    RouteFollower route;
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);

    // The walk stops at the door. Everything the dispatch needs is in the route.
    CHECK(route.requestDoor(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.phase() == RouteFollower::Phase::DoorRequested);
    CHECK(route.door(w.registry, w.epoch) == &w.doorA);
    CHECK(route.actor(w.registry, w.epoch) == &w.player);

    CHECK(route.beginHandoff(w.registry, w.epoch, &w.player, &w.doorA));
    CHECK(route.endHandoff(true));
    CHECK(route.phase() == RouteFollower::Phase::WaitingDoor);
    CHECK(route.goal() == cell(9, 0));
}

TEST(route_owned_sequence_completion_survives_and_foreign_action_cancels)
{
    World w;
    Grid grid;
    RouteFollower route;
    const uint64_t first = 41, second = 42;

    // The door action's own sequences stand the player up when they finish.
    // That pose call comes after the handoff ended and must not end the route.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA, { first, second }));
    CHECK(route.noteYield(false, first));
    CHECK(route.noteYield(false, second));
    CHECK(route.noteYield(false, first));
    CHECK(route.phase() == RouteFollower::Phase::WaitingDoor);
    CHECK(route.goal() == cell(9, 0));

    // A step of some other sequence is a foreign action.
    CHECK(!route.noteYield(false, 43));
    CHECK(route.phase() == RouteFollower::Phase::Idle);

    // A registration outside any sequence step is foreign.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA, { first }));
    CHECK(!route.noteYield(false, 0));

    // A script running inside the owned sequence's callback is foreign.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA, { first }));
    CHECK(!route.noteYield(true, first));

    // A door action that left no sequence owns nothing.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA));
    CHECK(!route.noteYield(false, first));

    // A serial from an earlier route names nothing in the next one.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA, { 50 }));
    CHECK(!route.noteYield(false, first));

    // Before the handoff and outside the wait nothing is owned.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(!route.noteYield(false, first));
}

// The engine's side of the same rule, with the real serial helper: only the
// automatic stand pose reads a sequence serial. A registration made by the
// owned sequence's callback reads zero, whatever sequence is being stepped.
TEST(route_owned_callback_registration_cancels_while_automatic_stand_survives)
{
    World w;
    Grid grid;
    RouteFollower route;
    SequenceSerials serials;
    const uint64_t owned = serials.issue();
    const uint64_t other = serials.issue();
    CHECK(owned == 1 && other == 2);

    // The completion stand of the owned sequence keeps the goal, then the
    // scope ends and the reading drops back to none.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA, { owned }));
    CHECK(serials.standing() == 0);
    {
        SequenceSerials::StandScope standing(serials, owned);
        CHECK(serials.standing() == owned);
        CHECK(route.noteYield(false, serials.standing()));
        {
            SequenceSerials::StandScope inner(serials, other);
            CHECK(serials.standing() == other);
        }
        CHECK(serials.standing() == owned);
    }
    CHECK(serials.standing() == 0);
    CHECK(route.phase() == RouteFollower::Phase::WaitingDoor);

    // The owned callback registers new player movement. The register call
    // yields outside any stand scope, so it reads zero and ends the route.
    CHECK(!route.noteYield(false, serials.standing()));
    CHECK(route.phase() == RouteFollower::Phase::Idle);

    // A clear made by that callback is the same.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA, { owned }));
    CHECK(!route.noteYield(false, serials.standing()));

    // The automatic stand of some other sequence is not ours either.
    CHECK(route.start(w.registry, &w.player, w.epoch, cell(0, 0), cell(9, 0), grid.planner()) == RouteFollower::Start::Started);
    CHECK(reachWait(route, w, &w.doorA, { owned }));
    {
        SequenceSerials::StandScope standing(serials, other);
        CHECK(!route.noteYield(false, serials.standing()));
    }
}

TEST(sequence_serials_never_issue_zero_or_wrap)
{
    SequenceSerials fresh;
    CHECK(fresh.canIssue());
    CHECK(fresh.issue() == 1);

    SequenceSerials nearEnd(UINT64_MAX - 2);
    CHECK(nearEnd.issue() == UINT64_MAX - 1);
    CHECK(nearEnd.canIssue());
    CHECK(nearEnd.issue() == UINT64_MAX);
    CHECK(!nearEnd.canIssue());
    CHECK(nearEnd.issue() == 0);
    CHECK(nearEnd.issue() == 0);
    CHECK(nearEnd.last() == UINT64_MAX);
}
