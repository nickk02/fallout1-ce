#include "game/stick_move.h"

#include <math.h>

#include "game/anim.h"
#include "game/combat.h"
#include "game/game.h"
#include "game/object.h"
#include "game/tile.h"
#include "plib/gnw/controller.h"
#include "plib/gnw/mouse.h"

namespace fallout {

// How many hexes ahead of the player each queued walk or run ends.
#define STICK_MOVE_LOOKAHEAD 4

// Stick deflection (0..1) at which the player runs instead of walking.
#define STICK_MOVE_RUN_THRESHOLD 0.85f

static bool stickMoving = false;
static int stickRotation = -1;
static int stickTarget = -1;
static bool stickRunning = false;

static int stick_move_pick_rotation(float x, float y);
static int stick_move_pick_target(int rotation);

void stick_move_update()
{
    if (game_ui_is_disabled() || isInCombat() || mouse_hidden()) {
        stickMoving = false;
        return;
    }

    controller_world_tick();

    float x;
    float y;
    if (!controller_get_move_vector(&x, &y)) {
        if (stickMoving) {
            register_clear(obj_dude);
            stickMoving = false;
        }
        return;
    }

    int rotation = stick_move_pick_rotation(x, y);
    if (rotation == -1) {
        return;
    }

    bool running = sqrtf(x * x + y * y) >= STICK_MOVE_RUN_THRESHOLD;

    // Queue a new walk only when the heading or gait changed, or the previous
    // one is about to end, so the player does not stutter between segments.
    bool arriving = stickTarget == -1 || tile_dist(obj_dude->tile, stickTarget) <= 1;
    if (stickMoving && rotation == stickRotation && running == stickRunning && !arriving) {
        return;
    }

    int target = stick_move_pick_target(rotation);
    if (target == -1) {
        return;
    }

    if (stickMoving) {
        register_clear(obj_dude);
    }

    if (dude_move_to_tile(target, running) == 0) {
        stickMoving = true;
        stickRotation = rotation;
        stickTarget = target;
        stickRunning = running;
    }
}

// Picks the hex direction whose on-screen heading is closest to the stick.
static int stick_move_pick_rotation(float x, float y)
{
    int originX;
    int originY;
    if (tile_coord(obj_dude->tile, &originX, &originY, obj_dude->elevation) == -1) {
        return -1;
    }

    float length = sqrtf(x * x + y * y);
    if (length == 0.0f) {
        return -1;
    }

    int best = -1;
    float bestDot = -2.0f;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int neighbor = tile_num_in_direction(obj_dude->tile, rotation, 1);
        if (neighbor == -1) {
            continue;
        }

        int neighborX;
        int neighborY;
        if (tile_coord(neighbor, &neighborX, &neighborY, obj_dude->elevation) == -1) {
            continue;
        }

        float dx = (float)(neighborX - originX);
        float dy = (float)(neighborY - originY);
        float dot = (dx * x + dy * y) / (sqrtf(dx * dx + dy * dy) * length);
        if (dot > bestDot) {
            bestDot = dot;
            best = rotation;
        }
    }

    return best;
}

// Returns the farthest tile in `rotation` within the lookahead that is on the
// map, or -1.
static int stick_move_pick_target(int rotation)
{
    for (int distance = STICK_MOVE_LOOKAHEAD; distance >= 1; distance--) {
        int tile = tile_num_in_direction(obj_dude->tile, rotation, distance);
        if (tile != -1) {
            return tile;
        }
    }

    return -1;
}

} // namespace fallout
