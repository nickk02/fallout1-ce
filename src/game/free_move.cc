#include "game/free_move.h"

#include <math.h>

#include "game/anim.h"
#include "game/art.h"
#include "game/critter.h"
#include "game/object.h"
#include "game/scripts.h"
#include "game/tile.h"
#include "plib/gnw/rect.h"

namespace fallout {

// Speed in screen pixels per second. A hex is 32 pixels wide.
#define FREE_MOVE_WALK_SPEED 64.0f
#define FREE_MOVE_RUN_SPEED 150.0f

// Limits for the speed taken from the art.
#define FREE_MOVE_MIN_SPEED 40.0f
#define FREE_MOVE_MAX_SPEED 220.0f


static bool active = false;
static bool wasRunning = false;
static float remainderX = 0.0f;
static float remainderY = 0.0f;
static float frameDistance = 0.0f;

// Pixels of travel that one frame of the current walk or run cycle covers. It
// comes from the art, so the feet keep up with the ground at any speed.
static float pixelsPerFrame = 5.0f;

// Ground speed at which the current cycle plays at its normal frame rate. The
// player moves at this speed, so the feet never outrun or lag the ground.
static float poseSpeed = 0.0f;

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

    if (!active) {
        // Another animation (an attack, a door, a talk) has the player.
        if (anim_busy(obj_dude) != 0) {
            return false;
        }

        active = true;
        remainderX = 0.0f;
        remainderY = 0.0f;
        frameDistance = 0.0f;
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

    float speed = poseSpeed > 0.0f ? poseSpeed : (running ? FREE_MOVE_RUN_SPEED : FREE_MOVE_WALK_SPEED);
    if (speed < FREE_MOVE_MIN_SPEED) {
        speed = FREE_MOVE_MIN_SPEED;
    } else if (speed > FREE_MOVE_MAX_SPEED) {
        speed = FREE_MOVE_MAX_SPEED;
    }
    remainderX += dirX * speed * elapsed;
    remainderY += dirY * speed * elapsed;

    int stepX = (int)remainderX;
    int stepY = (int)remainderY;
    remainderX -= stepX;
    remainderY -= stepY;

    if (stepX != 0 || stepY != 0) {
        // If the full step is blocked, slide along the obstacle.
        bool moved = free_move_apply(stepX, stepY, &part)
            || (stepX != 0 && free_move_apply(stepX, 0, &part))
            || (stepY != 0 && free_move_apply(0, stepY, &part));

        if (moved) {
            dirty_add(&dirty, &part);

            frameDistance += sqrtf((float)(stepX * stepX + stepY * stepY));
            while (frameDistance >= pixelsPerFrame) {
                frameDistance -= pixelsPerFrame;
                if (obj_inc_frame(obj_dude, &part) == 0) {
                    dirty_add(&dirty, &part);
                }
            }
        }
    }

    if (dirty.valid) {
        tile_refresh_rect(&(dirty.rect), obj_dude->elevation);
    }

    return true;
}

void free_move_stop()
{
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

    if (obj_blocking_at(obj_dude, neighbors[best].tile, obj_dude->elevation) != NULL) {
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

// Switches to the walking or running pictures.
static bool free_move_set_pose(bool running, Rect* dirty)
{
    int anim = running ? ANIM_RUNNING : ANIM_WALK;
    int fid = art_id(FID_TYPE(obj_dude->fid), obj_dude->fid & 0xFFF, anim, (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
    if (!art_exists(fid)) {
        fid = art_id(FID_TYPE(obj_dude->fid), obj_dude->fid & 0xFFF, ANIM_WALK, (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
    }

    if (obj_change_fid(obj_dude, fid, dirty) != 0) {
        return false;
    }

    pixelsPerFrame = 5.0f;
    poseSpeed = 0.0f;
    CacheEntry* artHandle;
    Art* art = art_ptr_lock(fid, &artHandle);
    if (art != nullptr) {
        int frameCount = art_frame_max_frame(art);
        float travelX = 0.0f;
        float travelY = 0.0f;
        for (int frame = 0; frame < frameCount; frame++) {
            int x;
            int y;
            if (art_frame_hot(art, frame, obj_dude->rotation, &x, &y) == 0) {
                travelX += (float)x;
                travelY += (float)y;
            }
        }
        art_ptr_unlock(artHandle);

        float travel = sqrtf(travelX * travelX + travelY * travelY);
        if (frameCount > 0 && travel > 1.0f) {
            pixelsPerFrame = travel / frameCount;
        }

        int fps = art_frame_fps(art);
        poseSpeed = fps > 0 ? pixelsPerFrame * (float)fps : 0.0f;
    }

    Rect frameRect;
    if (obj_set_frame(obj_dude, 0, &frameRect) == 0) {
        rect_min_bound(dirty, &frameRect, dirty);
    }

    return true;
}

} // namespace fallout
