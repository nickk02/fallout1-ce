#ifndef FALLOUT_SIM_CONTINUOUS_MOVE_H_
#define FALLOUT_SIM_CONTINUOUS_MOVE_H_
#include "object_handle.h"
#include <functional>
#include <vector>
namespace fallout {
struct StrideOffset {
    int x;
    int y;
};
class AuthoredStride {
public:
    bool build(const std::vector<StrideOffset>& offsets, int fps);
    double speed() const { return speed_; }
    double distance() const { return distance_; }
    int frame(double phase) const;

private:
    std::vector<double> ends_;
    double distance_ = 0;
    double speed_ = 0;
};
struct MoveStep {
    bool valid = true;
    int x = 0;
    int y = 0;
};
// Failed diagonal/x/y attempts retain a door hit until accepted progress
// clears it. A failed no-door fallback does not erase the earlier contact.
class MovementContact {
public:
    void record(bool moved, ObjectRef door)
    {
        if (moved)
            door_ = {};
        else if (door.handle.valid())
            door_ = door;
    }
    ObjectRef blockedDoor() const { return door_; }

private:
    ObjectRef door_;
};
class ContinuousMove {
public:
    using Step = std::function<MoveStep(int, int)>;
    // One 60 Hz tick, with at most 4096 unit-pixel collision attempts per axis.
    // Returns false on invalidation or an excessive/malformed authored stride.
    bool tick(const AuthoredStride& stride, double x, double y, double rate, const Step& step);
    void reset() { x_ = y_ = phase_ = 0; }
    double phase() const { return phase_; }

private:
    double x_ = 0;
    double y_ = 0;
    double phase_ = 0;
};
} // namespace fallout
#endif
