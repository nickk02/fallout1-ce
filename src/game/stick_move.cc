#include "game/stick_move.h"

#include <math.h>

#include "game/anim.h"
#include "game/combat.h"
#include "game/free_move.h"
#include "game/game.h"
#include "game/gmouse.h"
#include "game/object.h"
#include "game/soft_target.h"
#include "game/tile.h"
#include "plib/gnw/controller.h"
#include "plib/gnw/input.h"

namespace fallout {

// How many hexes ahead of the player each queued walk or run ends.
#define STICK_MOVE_LOOKAHEAD 4

// Deflection (0..1) at which the player runs instead of walking.
#define STICK_MOVE_RUN_THRESHOLD 0.85f

static unsigned int lastMoveTicks = 0;

static void stick_move_update_impl(bool inCombat, bool paused);
static bool stick_move_get_vector(float* x, float* y, bool* walkOnly);

void stick_move_update()
{
    stick_move_update_impl(false, false);
}

void stick_move_update_combat()
{
    stick_move_update_impl(true, false);
}

void stick_move_update_paused()
{
    stick_move_update_impl(true, true);
}

void camera_follow_update()
{
    // Edge scrolling would fight the lock, so it stays off while the map or
    // combat loop runs. Screens that need it enable it again themselves.
    gmouse_disable_scrolling();

    // Follow the player's pixel position, not just the tile, so the view moves
    // smoothly while walking instead of jumping a tile at a time.
    tile_follow_smooth(obj_dude->tile, obj_dude->x, obj_dude->y);
}

static void stick_move_update_impl(bool inCombat, bool paused)
{
    // The loop is running, so the controller drives the character and not a
    // cursor, even when a script has locked the interface for a moment.
    controller_world_tick();

    if (game_ui_is_disabled() || (!inCombat && isInCombat())) {
        free_move_stop();
        soft_target_clear();
        return;
    }

    soft_target_update(inCombat);

    // Moving between targets works any time. During the pause it is how the
    // player picks which enemy to shoot.
    if (controller_take_action(CONTROLLER_ACTION_NEXT_TARGET)) {
        soft_target_cycle(1);
    }

    if (controller_take_action(CONTROLLER_ACTION_PREVIOUS_TARGET)) {
        soft_target_cycle(-1);
    }

    bool interact = controller_take_action(CONTROLLER_ACTION_INTERACT);
    bool attack = controller_take_action(CONTROLLER_ACTION_ATTACK);
    bool inspect = controller_take_action(CONTROLLER_ACTION_INSPECT);
    if (interact || attack || inspect) {
        // Stop first so the action's own movement is not mixed with ours.
        free_move_stop();

        if (attack) {
            soft_target_attack();
        } else if (interact) {
            soft_target_interact();
        } else {
            soft_target_inspect();
        }
        return;
    }

    // Time is frozen, so nobody walks.
    if (paused) {
        return;
    }

    float x;
    float y;
    bool walkOnly;
    unsigned int now = get_time();
    float elapsed = (now - lastMoveTicks) / 1000.0f;
    lastMoveTicks = now;
    if (elapsed > 0.1f) {
        elapsed = 0.1f;
    }

    if (!stick_move_get_vector(&x, &y, &walkOnly)) {
        // A click on the ground walks there the same way the keys do.
        if (free_move_goto_active()) {
            free_move_goto_step(elapsed);
            return;
        }

        free_move_stop();
        return;
    }

    // The keys or the stick take over from a walk to a clicked tile.
    free_move_goto_cancel();

    // Glide in the direction asked for, as opposed to following hex steps.

    // Once running, the stick may drop a little before the character slows to a
    // walk, so the cycle does not flip back and forth at the threshold.
    static bool wasRunning = false;
    float deflection = sqrtf(x * x + y * y);
    bool running = !walkOnly && deflection >= (wasRunning ? STICK_MOVE_RUN_THRESHOLD - 0.15f : STICK_MOVE_RUN_THRESHOLD);
    wasRunning = running;
    free_move_step(x, y, running, elapsed);
}

// Combines the left stick and WASD into one screen direction. Holding Shift
// with the keyboard walks instead of running.
static bool stick_move_get_vector(float* x, float* y, bool* walkOnly)
{
    float stickX = 0.0f;
    float stickY = 0.0f;
    controller_get_move_vector(&stickX, &stickY);

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    bool up = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    bool down = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    bool left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    bool right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];

    float keyX = 0.0f;
    float keyY = 0.0f;
    if (up) {
        keyY -= 1.0f;
    }
    if (down) {
        keyY += 1.0f;
    }
    if (left) {
        keyX -= 1.0f;
    }
    if (right) {
        keyX += 1.0f;
    }

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

} // namespace fallout
