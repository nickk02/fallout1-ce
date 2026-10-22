#include "game/stick_move.h"

#include <math.h>

#include "game/anim.h"
#include "game/combat.h"
#include "game/game.h"
#include "game/gmouse.h"
#include "game/object.h"
#include "game/soft_target.h"
#include "game/tile.h"
#include "plib/gnw/controller.h"

namespace fallout {

// How many hexes ahead of the player each queued walk or run ends.
#define STICK_MOVE_LOOKAHEAD 4

// Deflection (0..1) at which the player runs instead of walking.
#define STICK_MOVE_RUN_THRESHOLD 0.85f

static bool stickMoving = false;
static int stickHeading = -1;
static int stickTarget = -1;
static bool stickRunning = false;

static void stick_move_update_impl(bool inCombat);
static bool stick_move_get_vector(float* x, float* y, bool* walkOnly);
static int stick_move_plan(float x, float y, int* heading);

void stick_move_update()
{
    stick_move_update_impl(false);
}

void stick_move_update_combat()
{
    stick_move_update_impl(true);
}

void camera_follow_update()
{
    // Edge scrolling would fight the lock, so it stays off while the map or
    // combat loop runs. Screens that need it enable it again themselves.
    gmouse_disable_scrolling();

    // Follow the player's pixel position, not just the tile, so the view moves
    // smoothly while walking instead of jumping a tile at a time.
    tile_follow(obj_dude->tile, obj_dude->x, obj_dude->y);
}

static void stick_move_update_impl(bool inCombat)
{
    // The loop is running, so the controller drives the character and not a
    // cursor, even when a script has locked the interface for a moment.
    controller_world_tick();

    if (game_ui_is_disabled() || (!inCombat && isInCombat())) {
        stickMoving = false;
        soft_target_clear();
        return;
    }

    soft_target_update(inCombat);

    bool interact = controller_take_action(CONTROLLER_ACTION_INTERACT);
    bool attack = controller_take_action(CONTROLLER_ACTION_ATTACK);
    bool inspect = controller_take_action(CONTROLLER_ACTION_INSPECT);
    if (interact || attack || inspect) {
        // Stop first so the action's own movement is not mixed with ours.
        if (stickMoving) {
            register_clear(obj_dude);
            stickMoving = false;
        }

        if (attack) {
            soft_target_attack();
        } else if (interact) {
            soft_target_interact();
        } else {
            soft_target_inspect();
        }
        return;
    }

    float x;
    float y;
    bool walkOnly;
    if (!stick_move_get_vector(&x, &y, &walkOnly)) {
        if (stickMoving) {
            register_clear(obj_dude);
            stickMoving = false;
        }
        return;
    }

    int heading;
    int target = stick_move_plan(x, y, &heading);
    if (target == -1) {
        return;
    }

    bool running = !walkOnly && sqrtf(x * x + y * y) >= STICK_MOVE_RUN_THRESHOLD;

    // Queue a new walk only when the heading or gait changed, or the previous
    // one is about to end, so the player does not stutter between segments.
    bool arriving = stickTarget == -1 || tile_dist(obj_dude->tile, stickTarget) <= 1;
    if (stickMoving && heading == stickHeading && running == stickRunning && !arriving) {
        return;
    }

    if (stickMoving) {
        register_clear(obj_dude);
    }

    if (dude_move_to_tile(target, running, -1) == 0) {
        stickMoving = true;
        stickHeading = heading;
        stickTarget = target;
        stickRunning = running;
    }
}

// Combines the left stick and WASD into one screen direction. Holding Shift
// with the keyboard walks instead of running.
static bool stick_move_get_vector(float* x, float* y, bool* walkOnly)
{
    float stickX = 0.0f;
    float stickY = 0.0f;
    controller_get_move_vector(&stickX, &stickY);

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float keyX = 0.0f;
    float keyY = 0.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) keyX -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) keyX += 1.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) keyY -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) keyY += 1.0f;

    bool keyboard = keyX != 0.0f || keyY != 0.0f;
    if (keyboard) {
        float length = sqrtf(keyX * keyX + keyY * keyY);
        keyX /= length;
        keyY /= length;
    }

    *x = stickX + keyX;
    *y = stickY + keyY;
    *walkOnly = keyboard && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);

    float length = sqrtf(*x * *x + *y * *y);
    if (length > 1.0f) {
        *x /= length;
        *y /= length;
    }

    return length > 0.0f;
}

// The map has six hex directions, but the input can point anywhere. Take the
// two hex directions on either side of the input and mix them in the right
// proportion over the lookahead, so the player moves along the heading the
// input asks for. Returns the target tile, or -1. `heading` is the input angle
// in steps of about 15 degrees.
static int stick_move_plan(float x, float y, int* heading)
{
    int originX;
    int originY;
    if (tile_coord(obj_dude->tile, &originX, &originY, obj_dude->elevation) == -1) {
        return -1;
    }

    float dirX[ROTATION_COUNT];
    float dirY[ROTATION_COUNT];
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        dirX[rotation] = 0.0f;
        dirY[rotation] = 0.0f;

        int neighbor = tile_num_in_direction(obj_dude->tile, rotation, 1);
        int neighborX;
        int neighborY;
        if (neighbor == -1 || tile_coord(neighbor, &neighborX, &neighborY, obj_dude->elevation) == -1) {
            continue;
        }

        float dx = (float)(neighborX - originX);
        float dy = (float)(neighborY - originY);
        float length = sqrtf(dx * dx + dy * dy);
        dirX[rotation] = dx / length;
        dirY[rotation] = dy / length;
    }

    *heading = (int)lroundf(atan2f(y, x) / 0.26f);

    // Find the pair of neighboring directions that encloses the input.
    int first = -1;
    int second = -1;
    float firstWeight = 0.0f;
    float secondWeight = 0.0f;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int next = (rotation + 1) % ROTATION_COUNT;

        float determinant = dirX[rotation] * dirY[next] - dirY[rotation] * dirX[next];
        if (fabsf(determinant) < 0.01f) {
            continue;
        }

        float a = (x * dirY[next] - y * dirX[next]) / determinant;
        float b = (dirX[rotation] * y - dirY[rotation] * x) / determinant;
        if (a >= -0.001f && b >= -0.001f) {
            first = rotation;
            second = next;
            firstWeight = a > 0.0f ? a : 0.0f;
            secondWeight = b > 0.0f ? b : 0.0f;
            break;
        }
    }

    if (first == -1) {
        return -1;
    }

    // Spread the two directions evenly over the steps.
    float total = firstWeight + secondWeight;
    int firstSteps = (int)lroundf(STICK_MOVE_LOOKAHEAD * firstWeight / total);

    int tile = obj_dude->tile;
    int accumulated = 0;
    for (int step = 0; step < STICK_MOVE_LOOKAHEAD; step++) {
        accumulated += firstSteps;

        int rotation = second;
        if (accumulated >= STICK_MOVE_LOOKAHEAD) {
            accumulated -= STICK_MOVE_LOOKAHEAD;
            rotation = first;
        }

        int next = tile_num_in_direction(tile, rotation, 1);
        if (next == -1) {
            break;
        }

        tile = next;
    }

    return tile != obj_dude->tile ? tile : -1;
}

} // namespace fallout
