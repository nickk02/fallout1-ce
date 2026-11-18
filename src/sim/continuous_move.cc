#include "sim/continuous_move.h"
#include <algorithm>
#include <cmath>
namespace fallout {
bool AuthoredStride::build(const std::vector<StrideOffset>& offsets, int fps)
{
    ends_.clear();
    distance_ = speed_ = 0;
    if (offsets.empty() || fps <= 0) return false;
    // Entering frame i consumes hot[i], matching the legacy increment-then-hot path.
    for (size_t i = 1; i <= offsets.size(); i++) {
        auto hot = offsets[i % offsets.size()];
        distance_ += std::hypot(static_cast<double>(hot.x), static_cast<double>(hot.y));
        ends_.push_back(distance_);
    }
    if (!(distance_ > 0) || !std::isfinite(distance_)) return false;
    speed_ = distance_ * fps / offsets.size();
    return std::isfinite(speed_);
}
int AuthoredStride::frame(double phase) const
{
    if (ends_.empty() || distance_ <= 0) return 0;
    double at = (phase - std::floor(phase)) * distance_;
    auto it = std::upper_bound(ends_.begin(), ends_.end(), at);
    return it == ends_.end() ? 0 : static_cast<int>(it - ends_.begin());
}
bool ContinuousMove::tick(const AuthoredStride& stride, double x, double y, double rate, const Step& step)
{
    double magnitude = std::hypot(x, y);
    if (!(stride.distance() > 0) || !std::isfinite(magnitude) || !std::isfinite(rate) || rate < 0) return false;
    if (magnitude == 0) {
        reset();
        return true;
    }
    double travel = stride.speed() * rate / 60.0;
    if (!std::isfinite(travel) || travel > 4096) return false;
    x_ += x / magnitude * travel;
    y_ += y / magnitude * travel;
    int dx = static_cast<int>(std::round(x_));
    int dy = static_cast<int>(std::round(y_));
    x_ -= dx;
    y_ -= dy;
    int count = std::max(std::abs(dx), std::abs(dy));
    int previousX = 0, previousY = 0;
    for (int i = 1; i <= count; i++) {
        int atX = dx * i / count, atY = dy * i / count;
        int sx = atX - previousX, sy = atY - previousY;
        previousX = atX;
        previousY = atY;
        MoveStep moved = step(sx, sy);
        if (!moved.valid) return false;
        if (moved.x == 0 && moved.y == 0 && sx != 0 && sy != 0) {
            moved = step(sx, 0);
            if (!moved.valid) return false;
            if (moved.x == 0 && moved.y == 0) moved = step(0, sy);
            if (!moved.valid) return false;
        }
        phase_ += std::hypot(static_cast<double>(moved.x), static_cast<double>(moved.y)) / stride.distance();
        phase_ -= std::floor(phase_);
        if (moved.x == 0 && moved.y == 0) {
            x_ = y_ = 0;
            break;
        }
    }
    return true;
}
} // namespace fallout
