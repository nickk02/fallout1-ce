#ifndef FALLOUT_SIM_ROUTE_FOLLOWER_H_
#define FALLOUT_SIM_ROUTE_FOLLOWER_H_

#include "object_handle.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace fallout {

// A click-to-move route for the continuous mover: the waypoint tiles from the
// hex pathfinder, steering toward them one at a time, bounded replanning when
// progress stalls, and a door handoff that keeps the goal while the legacy door
// action runs. It owns no engine state. The planner, the tile centres and the
// actor identity come from the caller, so the production class runs in tests.
class RouteFollower {
public:
    enum class Phase { Idle,
        Following,
        DoorRequested,
        WaitingDoor };
    enum class Start { Started,
        Arrived,
        Rejected };
    enum class Act { Steer,
        Arrived,
        Abandon };
    enum class DoorWait { Waiting,
        Resume,
        Cancel };

    struct Config {
        // Ticks without progress toward the current waypoint before a replan.
        int stallTicks = 42;
        int stallReplans = 3;
        // Ticks a door action may take before the route is dropped.
        int doorWaitTicks = 240;
        // Door requests allowed per door, not per route.
        int doorAttempts = 2;
        // Hard cap on planner calls per route, whatever the door count.
        int plannerCalls = 24;
        // Pixels from the last waypoint centre that count as arrival.
        double arrival = 6;
        // Pixels a tick must gain toward the waypoint to count as progress.
        double progress = 2;
    };

    struct Steering {
        Act act = Act::Abandon;
        double x = 0;
        double y = 0;
    };

    // Fills `tiles` with the waypoint tiles after `from`, ending on `goal`, and
    // returns their count. Zero means no route.
    using Plan = std::function<int(int from, int goal, std::vector<int>& tiles)>;
    using Center = std::function<bool(int tile, double& x, double& y)>;

    RouteFollower() = default;
    explicit RouteFollower(const Config& config)
        : config_(config)
    {
    }

    // Plans a route. Every request is a new intent: the old route ends first,
    // whatever the result. Only a call from inside the planner is refused
    // without touching the old route.
    Start start(const ObjectRegistry& registry, const void* actor, uint64_t epoch, int from, int goal, const Plan& plan);
    void cancel();

    Phase phase() const { return phase_; }
    bool active() const { return phase_ != Phase::Idle; }
    uint64_t generation() const { return generation_; }
    int goal() const { return goal_; }
    int replans() const { return replans_; }
    int plannerCalls() const { return plannerCalls_; }
    const void* actor(const ObjectRegistry& registry, uint64_t epoch) const;
    const void* door(const ObjectRegistry& registry, uint64_t epoch) const;

    // One tick of steering. `x` and `y` are the actor's screen position. The
    // result is a heading to steer along, arrival, or abandonment. Arrival and
    // abandonment end the route.
    Steering follow(const ObjectRegistry& registry, uint64_t epoch, const void* player, int tile, double x, double y,
        const Center& center, const Plan& plan);

    // The final collision of a tick hit `door`. Moves to DoorRequested while the
    // door has attempts left. Otherwise the route ends and this returns false.
    bool requestDoor(const ObjectRegistry& registry, uint64_t epoch, const void* player, const void* door);

    // The door action is about to run for `door`. The route must be asking for
    // exactly that door, for exactly that actor, in this epoch. Nothing outside
    // the route has to be carried from the request to here.
    bool beginHandoff(const ObjectRegistry& registry, uint64_t epoch, const void* player, const void* door);
    // A legacy registration or pose call touched the actor. `foreign` is true
    // when a script is running. `sequence` names the animation sequence being
    // stepped (zero outside one). A yield survives only when it is the door
    // action's own: a registration made inside the handoff, or a step of one of
    // the sequences that action created. Returns true when the route survives.
    bool noteYield(bool foreign, uint64_t sequence = 0);
    // The door action returned with the sequences it left running. A false
    // `dispatched` or an ended route cancels.
    bool endHandoff(bool dispatched, const uint64_t* sequences = nullptr, int count = 0);

    // One tick of the door wait. Resume needs the actor idle and the door
    // confirmed open; a door that stays closed keeps waiting until the deadline.
    // Resume asks the caller to reacquire movement and call resume(). Cancel has
    // already ended the route.
    DoorWait pollDoor(const ObjectRegistry& registry, uint64_t epoch, const void* player, bool legacyWork, bool doorOpen);
    // Replans from `from` after a door wait. Failure ends the route.
    bool resume(int from, const Plan& plan);

private:
    int plan(const Plan& plan, int from, int goal, std::vector<int>& tiles);
    int attempts(ObjectHandle door) const;
    void resetProgress()
    {
        best_ = 1e18;
        stall_ = 0;
    }

    Config config_;
    Phase phase_ = Phase::Idle;
    uint64_t generation_ = 0;
    ObjectRef actor_;
    ObjectRef door_;
    std::vector<int> tiles_;
    std::vector<std::pair<ObjectHandle, int>> doors_;
    size_t index_ = 0;
    int goal_ = -1;
    double best_ = 1e18;
    int stall_ = 0;
    int replans_ = 0;
    int plannerCalls_ = 0;
    int waitTicks_ = 0;
    std::vector<uint64_t> owned_;
    bool handoff_ = false;
    bool planning_ = false;
};

} // namespace fallout

#endif /* FALLOUT_SIM_ROUTE_FOLLOWER_H_ */
