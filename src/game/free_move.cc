#include "game/free_move.h"

#include <math.h>

#include "game/actions.h"
#include "game/anim.h"
#include "game/art.h"
#include "game/combat_defs.h"
#include "game/critter.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/proto.h"
#include "game/scripts.h"
#include "game/tile.h"
#include "plib/gnw/input.h"
#include "plib/gnw/rect.h"

namespace fallout {

// Slowest the walk cycle plays, as a share of its normal rate, so a light
// push on the stick still shows steps.
#define FREE_MOVE_MIN_RATE 0.35f

static bool active = false;
static bool wasRunning = false;
static float remainderX = 0.0f;
static float remainderY = 0.0f;

// Frames of the walk or run cycle that are due but not yet shown.
static float frameClock = 0.0f;

// A closed door the player is pushing against, and when the player last asked
// to open one.
static Object* blockedDoor = nullptr;
static unsigned int lastDoorAttempt = 0;

// Pause between attempts to open the same door.
#define FREE_MOVE_DOOR_COOLDOWN 1200

typedef struct Neighbor {
    int tile;
    int x;
    int y;
} Neighbor;

static int free_move_collect(Neighbor* neighbors);
static bool free_move_apply(int stepX, int stepY, Rect* dirty);
static bool free_move_face(float dirX, float dirY, Rect* dirty);
static bool free_move_set_pose(bool running, Rect* dirty);

// Collects the screen areas that changed so they can be redrawn together.
typedef struct Dirty {
    bool valid;
    Rect rect;
} Dirty;

static void dirty_add(Dirty* dirty, const Rect* rect)
{
    if (dirty->valid) {
        rect_min_bound(&(dirty->rect), rect, &(dirty->rect));
    } else {
        dirty->rect = *rect;
        dirty->valid = true;
    }
}

bool free_move_step(float dirX, float dirY, bool running, float elapsed)
{
    if ((obj_dude->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0 || critter_is_prone(obj_dude)) {
        return false;
    }

    // Crippled legs cannot run.
    if ((obj_dude->data.critter.combat.results & DAM_CRIP_LEG_ANY) != 0) {
        running = false;
    }

    if (!active) {
        // Another animation (an attack, a door, a talk) has the player.
        if (anim_busy(obj_dude) != 0) {
            return false;
        }

        active = true;
        remainderX = 0.0f;
        remainderY = 0.0f;
        frameClock = 0.0f;
        wasRunning = !running;
    }

    Dirty dirty = { false, { 0, 0, 0, 0 } };
    Rect part;

    int anim = FID_ANIM_TYPE(obj_dude->fid);
    if (running != wasRunning || (anim != ANIM_WALK && anim != ANIM_RUNNING)) {
        wasRunning = running;
        if (free_move_set_pose(running, &part)) {
            dirty_add(&dirty, &part);
        }
    }

    if (free_move_face(dirX, dirY, &part)) {
        dirty_add(&dirty, &part);
    }

    // Running ends sneaking unless the player has Silent Running.
    if (running && !perk_level(PERK_SILENT_RUNNING)) {
        pc_flag_off(PC_FLAG_SNEAKING);
    }

    float magnitude = sqrtf(dirX * dirX + dirY * dirY);
    if (magnitude <= 0.0f) {
        return true;
    }

    float headingX = dirX / magnitude;
    float headingY = dirY / magnitude;

    // The cycle plays at the rate its art was drawn for, and each frame moves
    // the player by the distance that frame was drawn with, as in the original
    // animations. The feet then stay on the ground. A light push on the stick
    // plays the cycle more slowly.
    float rate = running ? 1.0f : (magnitude < FREE_MOVE_MIN_RATE ? FREE_MOVE_MIN_RATE : (magnitude > 1.0f ? 1.0f : magnitude));

    CacheEntry* artHandle;
    Art* art = art_ptr_lock(obj_dude->fid, &artHandle);
    if (art == nullptr) {
        return true;
    }

    int fps = art_frame_fps(art);
    if (fps <= 0) {
        fps = 10;
    }

    frameClock += elapsed * fps * rate;
    int frames = (int)frameClock;
    frameClock -= frames;

    // After a long stall, do not run ahead to catch up.
    if (frames > 3) {
        frames = 3;
    }

    for (int index = 0; index < frames; index++) {
        if (obj_inc_frame(obj_dude, &part) == 0) {
            dirty_add(&dirty, &part);
        }

        int hotX;
        int hotY;
        if (art_frame_hot(art, obj_dude->frame, obj_dude->rotation, &hotX, &hotY) != 0) {
            continue;
        }

        float distance = sqrtf((float)(hotX * hotX + hotY * hotY));
        remainderX += headingX * distance;
        remainderY += headingY * distance;

        int stepX = (int)remainderX;
        int stepY = (int)remainderY;
        remainderX -= stepX;
        remainderY -= stepY;

        if (stepX == 0 && stepY == 0) {
            continue;
        }

        // If the full step is blocked, slide along the obstacle.
        bool moved = free_move_apply(stepX, stepY, &part)
            || (stepX != 0 && free_move_apply(stepX, 0, &part))
            || (stepY != 0 && free_move_apply(0, stepY, &part));

        if (!moved && blockedDoor != nullptr) {
            // Walking into a closed door opens it, as clicking it would.
            unsigned int now = get_time();
            if (now - lastDoorAttempt > FREE_MOVE_DOOR_COOLDOWN) {
                lastDoorAttempt = now;
                Object* door = blockedDoor;
                blockedDoor = nullptr;
                art_ptr_unlock(artHandle);
                free_move_stop();
                action_use_an_object(obj_dude, door);
                return true;
            }
        }

        blockedDoor = nullptr;

        if (moved) {
            dirty_add(&dirty, &part);
        }
    }

    art_ptr_unlock(artHandle);

    if (dirty.valid) {
        tile_refresh_rect(&(dirty.rect), obj_dude->elevation);
    }

    return true;
}

// The tile of a click-to-move walk and how it is going.
static int gotoTile = -1;
static bool gotoRunning = false;
static float gotoBest = 0.0f;
static unsigned int gotoBestAt = 0;

void free_move_goto(int tile, bool running)
{
    gotoTile = tile;
    gotoRunning = running;
    gotoBest = 1e9f;
    gotoBestAt = get_time();
}

void free_move_world_resumed(unsigned int elapsed)
{
    // The "no progress" timer must not count the pause.
    gotoBestAt += elapsed;
}

bool free_move_goto_active()
{
    return gotoTile != -1;
}

void free_move_goto_cancel()
{
    gotoTile = -1;
}

bool free_move_goto_step(float elapsed)
{
    if (gotoTile == -1) {
        return false;
    }

    int originX;
    int originY;
    int targetX;
    int targetY;
    if (tile_coord(obj_dude->tile, &originX, &originY, obj_dude->elevation) == -1
        || tile_coord(gotoTile, &targetX, &targetY, obj_dude->elevation) == -1) {
        free_move_stop();
        return false;
    }

    float dx = (float)(targetX - (originX + obj_dude->x));
    float dy = (float)(targetY - (originY + obj_dude->y));
    float distance = sqrtf(dx * dx + dy * dy);

    // Arrived.
    if (distance < 6.0f) {
        free_move_stop();
        return false;
    }

    // Give up when the way is blocked and no progress is made.
    unsigned int now = get_time();
    if (distance < gotoBest - 2.0f) {
        gotoBest = distance;
        gotoBestAt = now;
    } else if (now - gotoBestAt > 700) {
        free_move_stop();
        return false;
    }

    free_move_step(dx / distance, dy / distance, gotoRunning, elapsed);
    return true;
}

void free_move_stop()
{
    gotoTile = -1;

    if (!active) {
        return;
    }

    active = false;
    dude_stand(obj_dude, obj_dude->rotation, -1);
}

bool free_move_is_active()
{
    return active;
}

// Lists the hexes around the player with the screen offset of each one from
// the player's own hex, and returns how many there are.
static int free_move_collect(Neighbor* neighbors)
{
    int originX;
    int originY;
    if (tile_coord(obj_dude->tile, &originX, &originY, obj_dude->elevation) == -1) {
        return 0;
    }

    int count = 0;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int tile = tile_num_in_direction(obj_dude->tile, rotation, 1);
        int x;
        int y;
        if (tile == -1 || tile_coord(tile, &x, &y, obj_dude->elevation) == -1) {
            continue;
        }

        neighbors[count].tile = tile;
        neighbors[count].x = x - originX;
        neighbors[count].y = y - originY;
        count++;
    }

    return count;
}

// Moves by the given pixels. When that puts the player in another hex, it
// changes hexes if that hex is free. Returns `false` when the way is blocked.
static bool free_move_apply(int stepX, int stepY, Rect* dirty)
{
    Neighbor neighbors[ROTATION_COUNT];
    int count = free_move_collect(neighbors);

    int newX = obj_dude->x + stepX;
    int newY = obj_dude->y + stepY;

    // The hex whose center the new position is closest to.
    int best = -1;
    int bestDistance = newX * newX + newY * newY;
    for (int index = 0; index < count; index++) {
        int dx = newX - neighbors[index].x;
        int dy = newY - neighbors[index].y;
        int distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }

    if (best == -1) {
        return obj_offset(obj_dude, stepX, stepY, dirty) == 0;
    }

    Object* blocker = obj_blocking_at(obj_dude, neighbors[best].tile, obj_dude->elevation);
    if (blocker != NULL) {
        if (FID_TYPE(blocker->fid) == OBJ_TYPE_SCENERY) {
            Proto* proto;
            if (proto_ptr(blocker->pid, &proto) != -1 && proto->scenery.type == SCENERY_TYPE_DOOR) {
                blockedDoor = blocker;
            }
        }
        return false;
    }

    Rect moveRect;
    if (obj_move_to_tile(obj_dude, neighbors[best].tile, obj_dude->elevation, &moveRect) != 0) {
        return false;
    }

    // Area triggers, traps and the like check each hex the player enters, as
    // they do when the player walks along a path.
    scr_chk_spatials_in(obj_dude, neighbors[best].tile, obj_dude->elevation);

    // Changing hexes puts the offset back to zero. Restore the position
    // relative to the new hex.
    Rect offsetRect;
    if (obj_offset(obj_dude, newX - neighbors[best].x, newY - neighbors[best].y, &offsetRect) == 0) {
        rect_min_bound(&moveRect, &offsetRect, dirty);
    } else {
        *dirty = moveRect;
    }

    return true;
}

// Turns toward the hex direction closest to the heading. Returns `true` and the
// changed area if the player turned.
static bool free_move_face(float dirX, float dirY, Rect* dirty)
{
    int originX;
    int originY;
    if (tile_coord(obj_dude->tile, &originX, &originY, obj_dude->elevation) == -1) {
        return false;
    }

    int bestRotation = obj_dude->rotation;
    float bestDot = -2.0f;
    float currentDot = -2.0f;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int tile = tile_num_in_direction(obj_dude->tile, rotation, 1);
        int x;
        int y;
        if (tile == -1 || tile_coord(tile, &x, &y, obj_dude->elevation) == -1) {
            continue;
        }

        float vx = (float)(x - originX);
        float vy = (float)(y - originY);
        float dot = (vx * dirX + vy * dirY) / sqrtf(vx * vx + vy * vy);
        if (rotation == obj_dude->rotation) {
            currentDot = dot;
        }

        if (dot > bestDot) {
            bestDot = dot;
            bestRotation = rotation;
        }
    }

    // Keep the current facing unless another one is clearly better, so the
    // player does not flip back and forth between two facings.
    if (bestRotation == obj_dude->rotation || bestDot - currentDot < 0.12f) {
        return false;
    }

    if (obj_set_rotation(obj_dude, bestRotation, dirty) != 0) {
        return false;
    }

    int fid = art_id(FID_TYPE(obj_dude->fid), obj_dude->fid & 0xFFF, FID_ANIM_TYPE(obj_dude->fid), (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
    Rect fidRect;
    if (obj_change_fid(obj_dude, fid, &fidRect) == 0) {
        rect_min_bound(dirty, &fidRect, dirty);
    }

    return true;
}

// Switches to the walking or running pictures. The cycle carries on from the
// same point, so changing speed does not restart the stride.
static bool free_move_set_pose(bool running, Rect* dirty)
{
    int oldAnim = FID_ANIM_TYPE(obj_dude->fid);
    int oldFrame = obj_dude->frame;
    int oldCount = 0;
    if (oldAnim == ANIM_WALK || oldAnim == ANIM_RUNNING) {
        CacheEntry* oldHandle;
        Art* oldArt = art_ptr_lock(obj_dude->fid, &oldHandle);
        if (oldArt != nullptr) {
            oldCount = art_frame_max_frame(oldArt);
            art_ptr_unlock(oldHandle);
        }
    }

    int anim = running ? ANIM_RUNNING : ANIM_WALK;
    int fid = art_id(FID_TYPE(obj_dude->fid), obj_dude->fid & 0xFFF, anim, (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
    if (!art_exists(fid)) {
        fid = art_id(FID_TYPE(obj_dude->fid), obj_dude->fid & 0xFFF, ANIM_WALK, (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
    }

    if (obj_change_fid(obj_dude, fid, dirty) != 0) {
        return false;
    }

    int frame = 0;
    if (oldCount > 0) {
        CacheEntry* handle;
        Art* art = art_ptr_lock(fid, &handle);
        if (art != nullptr) {
            frame = oldFrame * art_frame_max_frame(art) / oldCount;
            art_ptr_unlock(handle);
        }
    }

    Rect frameRect;
    if (obj_set_frame(obj_dude, frame, &frameRect) == 0) {
        rect_min_bound(dirty, &frameRect, dirty);
    }

    return true;
}

} // namespace fallout
