#include "sim/route_follower.h"

#include <cmath>

namespace fallout {

namespace {

    // The planner uses shared scratch buffers and must never run twice at once.
    class PlanningScope {
    public:
        explicit PlanningScope(bool& flag)
            : flag_(flag)
        {
            flag_ = true;
        }

        ~PlanningScope() { flag_ = false; }

        PlanningScope(const PlanningScope&) = delete;
        PlanningScope& operator=(const PlanningScope&) = delete;

    private:
        bool& flag_;
    };

    // The farthest a route may list. Longer than any path the engine builds.
    const size_t kMaxTiles = 800;
    const size_t kMaxDoors = 16;

} // namespace

RouteFollower::Start RouteFollower::start(const ObjectRegistry& registry, const void* actor, uint64_t epoch, int from, int goal, const Plan& planner)
{
    // A planner callback must not start a route. That refusal leaves the
    // route alone. Any other request is a new intent and ends the old route.
    if (planning_) {
        return Start::Rejected;
    }

    cancel();

    if (actor == nullptr || !planner || from < 0 || goal < 0) {
        return Start::Rejected;
    }

    ObjectRef ref = object_ref_make(registry, actor, epoch);
    if (!ref.handle.valid()) {
        return Start::Rejected;
    }

    if (from == goal) {
        return Start::Arrived;
    }

    std::vector<int> tiles;
    int count;
    {
        PlanningScope scope(planning_);
        count = planner(from, goal, tiles);
    }

    if (count <= 0 || tiles.empty() || tiles.size() > kMaxTiles || tiles.back() != goal) {
        return Start::Rejected;
    }

    generation_++;
    phase_ = Phase::Following;
    actor_ = ref;
    tiles_ = tiles;
    goal_ = goal;
    plannerCalls_ = 1;
    resetProgress();
    return Start::Started;
}

void RouteFollower::cancel()
{
    phase_ = Phase::Idle;
    actor_ = {};
    door_ = {};
    tiles_.clear();
    doors_.clear();
    index_ = 0;
    goal_ = -1;
    replans_ = 0;
    plannerCalls_ = 0;
    waitTicks_ = 0;
    owned_.clear();
    handoff_ = false;
    resetProgress();
}

const void* RouteFollower::actor(const ObjectRegistry& registry, uint64_t epoch) const
{
    if (phase_ == Phase::Idle) {
        return nullptr;
    }

    return object_ref_resolve(registry, actor_, epoch);
}

const void* RouteFollower::door(const ObjectRegistry& registry, uint64_t epoch) const
{
    if (phase_ != Phase::DoorRequested && phase_ != Phase::WaitingDoor) {
        return nullptr;
    }

    return object_ref_resolve(registry, door_, epoch);
}

int RouteFollower::plan(const Plan& planner, int from, int goal, std::vector<int>& tiles)
{
    if (planning_ || !planner || plannerCalls_ >= config_.plannerCalls) {
        return 0;
    }

    plannerCalls_++;
    int count;
    {
        PlanningScope scope(planning_);
        count = planner(from, goal, tiles);
    }

    if (count <= 0 || tiles.empty() || tiles.size() > kMaxTiles || tiles.back() != goal) {
        return 0;
    }

    return count;
}

RouteFollower::Steering RouteFollower::follow(const ObjectRegistry& registry, uint64_t epoch, const void* player, int tile, double x, double y,
    const Center& center, const Plan& planner)
{
    Steering result;

    if (phase_ != Phase::Following || player == nullptr || actor(registry, epoch) != player || !center) {
        cancel();
        return result;
    }

    for (size_t index = index_; index + 1 < tiles_.size(); index++) {
        if (tiles_[index] == tile) {
            index_ = index + 1;
            resetProgress();
            break;
        }
    }

    bool replanned = false;
    while (true) {
        const size_t last = tiles_.size() - 1;
        double cx;
        double cy;
        if (!center(tiles_[index_], cx, cy)) {
            cancel();
            return result;
        }

        const double dx = cx - x;
        const double dy = cy - y;
        const double distance = std::hypot(dx, dy);

        if (index_ == last && distance < config_.arrival) {
            cancel();
            result.act = Act::Arrived;
            return result;
        }

        if (!(distance > 1e-6)) {
            // Standing on a waypoint centre. Take the next one.
            index_++;
            resetProgress();
            continue;
        }

        if (!replanned) {
            if (distance < best_ - config_.progress) {
                best_ = distance;
                stall_ = 0;
            } else if (++stall_ >= config_.stallTicks) {
                if (replans_ >= config_.stallReplans) {
                    cancel();
                    return result;
                }

                std::vector<int> tiles;
                if (tile == goal_) {
                    tiles.push_back(goal_);
                } else if (plan(planner, tile, goal_, tiles) <= 0) {
                    cancel();
                    return result;
                }

                replans_++;
                tiles_ = tiles;
                index_ = 0;
                resetProgress();
                replanned = true;
                continue;
            }
        }

        result.act = Act::Steer;
        result.x = dx / distance;
        result.y = dy / distance;
        return result;
    }
}

int RouteFollower::attempts(ObjectHandle door) const
{
    for (const auto& entry : doors_) {
        if (entry.first == door) {
            return entry.second;
        }
    }

    return 0;
}

bool RouteFollower::requestDoor(const ObjectRegistry& registry, uint64_t epoch, const void* player, const void* doorPtr)
{
    if (phase_ != Phase::Following || player == nullptr || actor(registry, epoch) != player) {
        cancel();
        return false;
    }

    ObjectRef ref = object_ref_make(registry, doorPtr, epoch);
    if (!ref.handle.valid() || attempts(ref.handle) >= config_.doorAttempts) {
        cancel();
        return false;
    }

    bool known = false;
    for (auto& entry : doors_) {
        if (entry.first == ref.handle) {
            entry.second++;
            known = true;
            break;
        }
    }

    if (!known) {
        if (doors_.size() >= kMaxDoors) {
            cancel();
            return false;
        }

        doors_.push_back({ ref.handle, 1 });
    }

    door_ = ref;
    handoff_ = false;
    phase_ = Phase::DoorRequested;
    return true;
}

bool RouteFollower::beginHandoff(const ObjectRegistry& registry, uint64_t epoch, const void* player, const void* doorPtr)
{
    if (phase_ != Phase::DoorRequested || handoff_ || player == nullptr || doorPtr == nullptr
        || actor(registry, epoch) != player || door(registry, epoch) != doorPtr) {
        return false;
    }

    handoff_ = true;
    return true;
}

bool RouteFollower::noteYield(bool foreign, uint64_t sequence)
{
    if (phase_ == Phase::Idle) {
        return true;
    }

    if (!foreign) {
        if (handoff_) {
            return true;
        }

        if (phase_ == Phase::WaitingDoor && sequence != 0) {
            for (uint64_t owned : owned_) {
                if (owned == sequence) {
                    return true;
                }
            }
        }
    }

    cancel();
    return false;
}

bool RouteFollower::endHandoff(bool dispatched, const uint64_t* sequences, int count)
{
    if (!handoff_ || phase_ != Phase::DoorRequested) {
        handoff_ = false;
        return false;
    }

    handoff_ = false;
    if (!dispatched) {
        cancel();
        return false;
    }

    owned_.clear();
    for (int index = 0; sequences != nullptr && index < count && index < 8; index++) {
        owned_.push_back(sequences[index]);
    }

    phase_ = Phase::WaitingDoor;
    waitTicks_ = 0;
    return true;
}

RouteFollower::DoorWait RouteFollower::pollDoor(const ObjectRegistry& registry, uint64_t epoch, const void* player, bool legacyWork, bool doorOpen)
{
    if (phase_ != Phase::WaitingDoor || player == nullptr || actor(registry, epoch) != player || door(registry, epoch) == nullptr) {
        cancel();
        return DoorWait::Cancel;
    }

    if (++waitTicks_ > config_.doorWaitTicks) {
        cancel();
        return DoorWait::Cancel;
    }

    // Idle is not enough: an action that ran and left the door shut (locked,
    // out of reach) must not send the player into it again.
    if (!legacyWork && doorOpen) {
        return DoorWait::Resume;
    }

    return DoorWait::Waiting;
}

bool RouteFollower::resume(int from, const Plan& planner)
{
    if (phase_ != Phase::WaitingDoor) {
        cancel();
        return false;
    }

    std::vector<int> tiles;
    if (from == goal_) {
        tiles.push_back(goal_);
    } else if (plan(planner, from, goal_, tiles) <= 0) {
        cancel();
        return false;
    }

    tiles_ = tiles;
    index_ = 0;
    door_ = {};
    waitTicks_ = 0;
    owned_.clear();
    phase_ = Phase::Following;
    resetProgress();
    return true;
}

} // namespace fallout
