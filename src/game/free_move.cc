#include "game/free_move.h"
#include "sim/continuous_move.h"
#include "sim/movement_owner.h"
#include "sim/route_follower.h"
#include "sim/world_sim.h"

#include <algorithm>
#include <cmath>
#include <math.h>
#include <vector>

#include "game/actions.h"
#include "game/anim.h"
#include "game/art.h"
#include "game/combat.h"
#include "game/combat_defs.h"
#include "game/critter.h"
#include "game/game.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/scripts.h"
#include "game/tile.h"
#include "int/intrpret.h"
#include "plib/gnw/game_commands.h"
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
static ObjectRef blockedDoor;
static unsigned int lastDoorAttempt = 0;

// The tile of a click-to-move walk and how it is going.
static int gotoTile = -1;
static bool gotoRunning = false;
static float gotoBest = 0.0f;
static unsigned int gotoBestAt = 0;

static MovementOwner movementOwner;
static MovementStandRecovery standRecovery;
static uint64_t movementToken = 0;
static ContinuousMove continuousMove;
static AuthoredStride authoredStride;
static int strideFid = -1;
static bool tickIntent = false;
static bool tickGoto = false;
static float tickX = 0, tickY = 0;
static bool tickRunning = false;
static uint64_t intentToken = 0, intentEpoch = 0;
static RouteFollower route;
// Set around the pose call that ends our own walk, which yields to itself.
static bool routeStanding = false;
static bool pathBuilding = false;
static unsigned int doorCooldownTicks = 0;
static uint64_t movementClockEpoch = 0;
static uint64_t sampleEpoch = 0;
static bool needsNeutralSample = false;
static ObjectRef pendingDoor, pendingActor;

static void free_move_clear_state()
{
    active = false;
    movementToken = 0;
    gotoTile = -1;
    blockedDoor = {};
    remainderX = remainderY = frameClock = 0.0f;
    wasRunning = false;
    continuousMove.reset();
    tickIntent = tickGoto = false;
    intentToken = intentEpoch = 0;
    strideFid = -1;
    pendingDoor = pendingActor = {};
}

// A legacy registration or pose call touched the player. The route survives
// only when it is the door action's own: made from C++ inside the handoff, or
// the automatic stand pose of a sequence that action created. A registration
// or clear made by that sequence's callbacks, or by a script, is never ours.
static void free_move_route_yield(Object* actor)
{
    if (!route.active() || routeStanding) return;
    if (route.actor(object_registry(), world_sim_epoch()) != actor) return;
    route.noteYield(interpretIsRunning(), anim_auto_stand_serial());
}

static void free_move_stand(Object* actor)
{
    routeStanding = true;
    dude_stand(actor, actor->rotation, -1);
    routeStanding = false;
}

void free_move_yield(Object* actor)
{
    free_move_route_yield(actor);
    standRecovery.cancelFor(actor);
    if (movementOwner.yield(actor)) free_move_clear_state();
}

static void free_move_discard(uint64_t token)
{
    if (movementToken == token && movementOwner.current(token)) {
        movementOwner.release(token, object_registry(), world_sim_epoch(), obj_dude);
        free_move_clear_state();
        route.cancel();
    }
}

static Object* free_move_resolve(uint64_t token)
{
    Object* actor = movementOwner.resolve(token, object_registry(), world_sim_epoch(), obj_dude);
    if (actor != NULL && !anim_actor_has_work(actor)) return actor;
    // An old continuation must not clear state acquired by a reentrant caller.
    if (movementOwner.current(token)) {
        movementOwner.release(token, object_registry(), world_sim_epoch(), obj_dude);
        free_move_clear_state();
        route.cancel();
    }
    return NULL;
}

void free_move_validate_owner()
{
    if (movementToken != 0) free_move_resolve(movementToken);
}

static Object* free_move_acquire()
{
    if (movementToken != 0) return free_move_resolve(movementToken);
    movementToken = movementOwner.acquire(object_registry(), obj_dude, world_sim_epoch(), anim_actor_has_work(obj_dude));
    if (movementToken != 0) standRecovery.cancel();
    return free_move_resolve(movementToken);
}

// Pause between attempts to open the same door.
#define FREE_MOVE_DOOR_COOLDOWN 1200

typedef struct Neighbor {
    int tile;
    int x;
    int y;
} Neighbor;

static int free_move_collect(Object* actor, Neighbor* neighbors);
static bool free_move_apply(uint64_t token, int stepX, int stepY, Rect* dirty);
static bool free_move_face(uint64_t token, float dirX, float dirY, Rect* dirty);
static bool free_move_set_pose(uint64_t token, bool running, Rect* dirty);

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
    if (free_move_uses_sim_ticks()) {
        free_move_sim_sample(dirX, dirY, running);
        return tickIntent;
    }
    Object* actor = free_move_acquire();
    if (actor == NULL) return false;
    const uint64_t token = movementToken;
    if ((actor->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0 || critter_is_prone(actor)) {
        free_move_yield(actor);
        return false;
    }

    // Crippled legs cannot run.
    if ((actor->data.critter.combat.results & DAM_CRIP_LEG_ANY) != 0) {
        running = false;
    }

    if (!active) {
        // Another animation (an attack, a door, a talk) has the player.
        if (anim_actor_has_work(actor)) {
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

    int anim = FID_ANIM_TYPE(actor->fid);
    if (running != wasRunning || (anim != ANIM_WALK && anim != ANIM_RUNNING)) {
        wasRunning = running;
        if (free_move_set_pose(token, running, &part)) {
            dirty_add(&dirty, &part);
        }
    }

    if (free_move_face(token, dirX, dirY, &part)) {
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
    Art* art = art_ptr_lock(actor->fid, &artHandle);
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
        actor = free_move_resolve(token);
        if (actor == NULL) {
            art_ptr_unlock(artHandle);
            return false;
        }
        if (obj_inc_frame(actor, &part) == 0) {
            dirty_add(&dirty, &part);
        }

        int hotX;
        int hotY;
        if (art_frame_hot(art, actor->frame, actor->rotation, &hotX, &hotY) != 0) {
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
        bool moved = free_move_apply(token, stepX, stepY, &part)
            || (stepX != 0 && free_move_apply(token, stepX, 0, &part))
            || (stepY != 0 && free_move_apply(token, 0, stepY, &part));

        actor = free_move_resolve(token);
        if (actor == NULL) {
            art_ptr_unlock(artHandle);
            return false;
        }
        if (!moved && blockedDoor.handle.valid()) {
            // Walking into a closed door opens it, as clicking it would.
            unsigned int now = get_time();
            if (now - lastDoorAttempt > FREE_MOVE_DOOR_COOLDOWN) {
                lastDoorAttempt = now;
                Object* door = static_cast<Object*>(object_ref_resolve(object_registry(), blockedDoor, world_sim_epoch()));
                blockedDoor = {};
                art_ptr_unlock(artHandle);
                free_move_stop();
                if (door != NULL) action_use_an_object(actor, door);
                return true;
            }
        }

        blockedDoor = {};

        if (moved) {
            dirty_add(&dirty, &part);
        }
    }

    art_ptr_unlock(artHandle);

    if (dirty.valid) {
        tile_refresh_rect(&(dirty.rect), actor->elevation);
    }

    return true;
}

static bool free_move_is_plannable_door(Object* blocker)
{
    if (FID_TYPE(blocker->fid) != OBJ_TYPE_SCENERY) return false;
    Proto* proto;
    if (proto_ptr(blocker->pid, &proto) == -1 || proto->scenery.type != SCENERY_TYPE_DOOR) return false;
    return !obj_is_locked(blocker);
}

// The path builder asks this about every hex. The player's own builder treats
// every door as a wall, so a route could never lead through one. An unlocked
// door is planned through here, and walking into it asks for it to be opened.
static Object* free_move_route_blocker(Object* object, int tile, int elevation)
{
    Object* blocker = obj_blocking_at(object, tile, elevation);
    if (blocker != NULL && free_move_is_plannable_door(blocker)) return NULL;
    return blocker;
}

// The hexes after `from`, ending on `goal`. The engine's builder keeps its
// search state in file-level buffers, so it must not run twice at once.
static int free_move_plan(int from, int goal, std::vector<int>& tiles)
{
    if (pathBuilding || obj_dude == NULL) return 0;
    pathBuilding = true;
    unsigned char rotations[800];
    int count = make_path_func(obj_dude, from, goal, rotations, 1, free_move_route_blocker);
    pathBuilding = false;
    // The builder stops at 800 steps. A full list may be a cut-off path.
    if (count <= 0 || count >= 800) return 0;
    tiles.clear();
    int tile = from;
    for (int index = 0; index < count; index++) {
        tile = tile_num_in_direction(tile, rotations[index], 1);
        if (tile == -1) {
            tiles.clear();
            return 0;
        }
        tiles.push_back(tile);
    }
    return count;
}

static bool free_move_route_center(int tile, double& x, double& y)
{
    int ix, iy;
    if (obj_dude == NULL || tile_coord(tile, &ix, &iy, obj_dude->elevation) == -1) return false;
    x = ix;
    y = iy;
    return true;
}

// A click is a new intent. The old route, its door request and a door wait
// end first, whatever the new click turns out to be. A click that cannot start
// a route (the player's own hex, or no path) leaves the player standing.
static void free_move_route_click(int tile, bool running)
{
    pendingDoor = pendingActor = {};
    Object* player = obj_dude;
    if (player == NULL || anim_actor_has_work(player)) {
        route.cancel();
        return;
    }
    if (route.start(object_registry(), player, world_sim_epoch(), player->tile, tile, free_move_plan) != RouteFollower::Start::Started) {
        free_move_stop();
        return;
    }
    if (free_move_acquire() == NULL) {
        route.cancel();
        return;
    }
    gotoRunning = running;
    tickIntent = tickGoto = true;
    intentToken = movementToken;
    intentEpoch = world_sim_epoch();
    sampleEpoch = intentEpoch;
}

void free_move_goto(int tile, bool running)
{
    if (free_move_uses_sim_ticks()) {
        if (!world_sim_drain()) return;
        if (game_commands().context() != CommandContext::World || game_ui_is_disabled() || world_sim_world_paused()) return;
        free_move_route_click(tile, running);
        return;
    }
    if (free_move_acquire() == NULL) return;
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
    if (free_move_uses_sim_ticks()) return route.active();
    return gotoTile != -1 && free_move_resolve(movementToken) != NULL;
}

void free_move_goto_cancel()
{
    gotoTile = -1;
    tickGoto = false;
    route.cancel();
    // An unstarted route has no moving pose to preserve for a stick transfer.
    if (!active) free_move_stop();
}

bool free_move_goto_step(float elapsed)
{
    if (free_move_uses_sim_ticks()) return free_move_goto_active();
    if (gotoTile == -1) {
        return false;
    }

    Object* actor = free_move_resolve(movementToken);
    if (actor == NULL) return false;
    int originX;
    int originY;
    int targetX;
    int targetY;
    if (tile_coord(actor->tile, &originX, &originY, actor->elevation) == -1
        || tile_coord(gotoTile, &targetX, &targetY, actor->elevation) == -1) {
        free_move_stop();
        return false;
    }

    float dx = (float)(targetX - (originX + actor->x));
    float dy = (float)(targetY - (originY + actor->y));
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

// Ends the walk and stands the player up, and leaves any route alone. A door
// request uses this to stop in front of the door and keep its goal.
static void free_move_halt()
{
    const uint64_t token = movementToken;
    Object* actor = free_move_resolve(token);
    bool stand = active && actor != NULL;
    if (movementToken == token && movementOwner.mayClear(token)) {
        movementOwner.release(token, object_registry(), world_sim_epoch(), obj_dude);
        free_move_clear_state();
    }
    if (stand) free_move_stand(actor);
}

void free_move_stop()
{
    route.cancel();
    free_move_halt();
}

bool free_move_is_active()
{
    return active && free_move_resolve(movementToken) != NULL;
}

// Lists the hexes around the player with the screen offset of each one from
// the player's own hex, and returns how many there are.
static int free_move_collect(Object* actor, Neighbor* neighbors)
{
    int originX;
    int originY;
    if (tile_coord(actor->tile, &originX, &originY, actor->elevation) == -1) {
        return 0;
    }

    int count = 0;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int tile = tile_num_in_direction(actor->tile, rotation, 1);
        int x;
        int y;
        if (tile == -1 || tile_coord(tile, &x, &y, actor->elevation) == -1) {
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
static bool free_move_apply(uint64_t token, int stepX, int stepY, Rect* dirty)
{
    Object* actor = free_move_resolve(token);
    if (actor == NULL) return false;
    Neighbor neighbors[ROTATION_COUNT];
    int count = free_move_collect(actor, neighbors);

    int newX = actor->x + stepX;
    int newY = actor->y + stepY;

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
        return obj_offset(actor, stepX, stepY, dirty) == 0;
    }

    Object* blocker = obj_blocking_at(actor, neighbors[best].tile, actor->elevation);
    if (blocker != NULL) {
        if (FID_TYPE(blocker->fid) == OBJ_TYPE_SCENERY) {
            Proto* proto;
            if (proto_ptr(blocker->pid, &proto) != -1 && proto->scenery.type == SCENERY_TYPE_DOOR) {
                blockedDoor = object_ref_make(object_registry(), blocker, world_sim_epoch());
            }
        }
        return false;
    }

    Rect moveRect;
    if (obj_move_to_tile(actor, neighbors[best].tile, actor->elevation, &moveRect) != 0) {
        return false;
    }

    // Area triggers, traps and the like check each hex the player enters, as
    // they do when the player walks along a path.
    scr_chk_spatials_in(actor, neighbors[best].tile, actor->elevation);
    actor = free_move_resolve(token);
    if (actor == NULL) return false;

    // Changing hexes puts the offset back to zero. Restore the position
    // relative to the new hex.
    Rect offsetRect;
    if (obj_offset(actor, newX - neighbors[best].x, newY - neighbors[best].y, &offsetRect) == 0) {
        rect_min_bound(&moveRect, &offsetRect, dirty);
    } else {
        *dirty = moveRect;
    }

    return true;
}

// Turns toward the hex direction closest to the heading. Returns `true` and the
// changed area if the player turned.
static bool free_move_face(uint64_t token, float dirX, float dirY, Rect* dirty)
{
    Object* actor = free_move_resolve(token);
    if (actor == NULL) return false;
    int originX;
    int originY;
    if (tile_coord(actor->tile, &originX, &originY, actor->elevation) == -1) {
        return false;
    }

    int bestRotation = actor->rotation;
    float bestDot = -2.0f;
    float currentDot = -2.0f;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int tile = tile_num_in_direction(actor->tile, rotation, 1);
        int x;
        int y;
        if (tile == -1 || tile_coord(tile, &x, &y, actor->elevation) == -1) {
            continue;
        }

        float vx = (float)(x - originX);
        float vy = (float)(y - originY);
        float dot = (vx * dirX + vy * dirY) / sqrtf(vx * vx + vy * vy);
        if (rotation == actor->rotation) {
            currentDot = dot;
        }

        if (dot > bestDot) {
            bestDot = dot;
            bestRotation = rotation;
        }
    }

    // Keep the current facing unless another one is clearly better, so the
    // player does not flip back and forth between two facings.
    if (bestRotation == actor->rotation || bestDot - currentDot < 0.12f) {
        return false;
    }

    if (obj_set_rotation(actor, bestRotation, dirty) != 0) {
        return false;
    }

    int fid = art_id(FID_TYPE(actor->fid), actor->fid & 0xFFF, FID_ANIM_TYPE(actor->fid), (actor->fid & 0xF000) >> 12, actor->rotation + 1);
    Rect fidRect;
    if (obj_change_fid(actor, fid, &fidRect) == 0) {
        rect_min_bound(dirty, &fidRect, dirty);
    }

    return true;
}

// Switches to the walking or running pictures. The cycle carries on from the
// same point, so changing speed does not restart the stride.
static bool free_move_set_pose(uint64_t token, bool running, Rect* dirty)
{
    Object* actor = free_move_resolve(token);
    if (actor == NULL) return false;
    int oldAnim = FID_ANIM_TYPE(actor->fid);
    int oldFrame = actor->frame;
    int oldCount = 0;
    if (oldAnim == ANIM_WALK || oldAnim == ANIM_RUNNING) {
        CacheEntry* oldHandle;
        Art* oldArt = art_ptr_lock(actor->fid, &oldHandle);
        if (oldArt != nullptr) {
            oldCount = art_frame_max_frame(oldArt);
            art_ptr_unlock(oldHandle);
        }
    }

    int anim = running ? ANIM_RUNNING : ANIM_WALK;
    int fid = art_id(FID_TYPE(actor->fid), actor->fid & 0xFFF, anim, (actor->fid & 0xF000) >> 12, actor->rotation + 1);
    if (!art_exists(fid)) {
        fid = art_id(FID_TYPE(actor->fid), actor->fid & 0xFFF, ANIM_WALK, (actor->fid & 0xF000) >> 12, actor->rotation + 1);
    }

    if (obj_change_fid(actor, fid, dirty) != 0) {
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
    if (obj_set_frame(actor, frame, &frameRect) == 0) {
        rect_min_bound(dirty, &frameRect, dirty);
    }

    return true;
}

bool free_move_uses_sim_ticks()
{
    return world_sim_mode() == WORLD_SIM_OWNED && (!isInCombat() || combat_is_realtime());
}

static bool free_move_tick_eligible()
{
    return free_move_uses_sim_ticks() && !world_sim_world_paused()
        && !game_ui_is_disabled() && game_commands().context() == CommandContext::World;
}

void free_move_sim_cancel()
{
    needsNeutralSample = true;
    pendingDoor = pendingActor = {};
    route.cancel();
    if (!tickIntent) return;
    const uint64_t token = intentToken;
    if (movementOwner.current(token)) {
        Object* actor = movementOwner.resolve(token, object_registry(), world_sim_epoch(), obj_dude);
        if (active && actor != NULL) standRecovery.request(object_registry(), actor, world_sim_epoch(), token);
        movementOwner.release(token, object_registry(), world_sim_epoch(), obj_dude);
        free_move_clear_state();
    } else {
        tickIntent = false;
        tickGoto = false;
    }
}

void free_move_sim_sample(float x, float y, bool running)
{
    // Drain the previous sample, never the freshly polled one, through now.
    if (!world_sim_drain()) return;
    if (sampleEpoch != world_sim_epoch()) {
        if (sampleEpoch != 0) free_move_sim_cancel();
        sampleEpoch = world_sim_epoch();
    }
    if (!free_move_tick_eligible()) {
        free_move_sim_cancel();
        return;
    }
    if (pendingDoor.handle.valid()) return;
    if (needsNeutralSample) {
        if (x != 0 || y != 0) return;
        needsNeutralSample = false;
    }
    if (x == 0 && y == 0) {
        if (route.active()) {
            // A neutral stick does not end a click route or its door wait.
            if (route.phase() != RouteFollower::Phase::Following || free_move_resolve(movementToken) != NULL) return;
            route.cancel();
        }
        free_move_stop();
        return;
    }
    // The stick takes over from a click route.
    route.cancel();
    Object* actor = free_move_acquire();
    if (actor == NULL) return;
    tickGoto = false;
    tickX = x;
    tickY = y;
    tickRunning = running;
    intentToken = movementToken;
    intentEpoch = world_sim_epoch();
    tickIntent = true;
}

bool free_move_dispatch_pending()
{
    if (!free_move_tick_eligible()) return false;
    Object* recoveryActor = standRecovery.candidate(object_registry(), world_sim_epoch(), obj_dude);
    Object* stand = standRecovery.take(object_registry(), world_sim_epoch(), obj_dude,
        movementOwner.generation(), !movementOwner.mayClear(0),
        recoveryActor != NULL && anim_actor_has_work(recoveryActor));
    if (stand != NULL && (stand->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) == 0
        && !critter_is_prone(stand)) {
        free_move_stand(stand);
    }
    if (!pendingDoor.handle.valid()) return false;
    ObjectRef doorRef = pendingDoor, actorRef = pendingActor;
    pendingDoor = pendingActor = {};
    if (!free_move_tick_eligible()) {
        route.cancel();
        return false;
    }
    auto* actor = static_cast<Object*>(object_ref_resolve(object_registry(), actorRef, world_sim_epoch()));
    auto* door = static_cast<Object*>(object_ref_resolve(object_registry(), doorRef, world_sim_epoch()));
    if (actor != NULL && actor == obj_dude && door != NULL) {
        // The route's goal survives the door action's own registrations. Any
        // other registration inside it, such as a script's, ends the route.
        const uint64_t mark = anim_serial_mark();
        const bool owned = route.beginHandoff(object_registry(), world_sim_epoch(), actor, door);
        const int result = action_use_an_object(actor, door);
        if (owned) {
            uint64_t sequences[8];
            const int count = anim_actor_sequences_since(actor, mark, sequences, 8);
            route.endHandoff(result == 0, sequences, count);
        }
        return true;
    }
    route.cancel();
    return false;
}

// One tick of a route waiting for a door action to finish. When the action is
// over, movement is taken again and the route is planned afresh from where the
// player stands.
static void free_move_route_poll()
{
    Object* player = obj_dude;
    const uint64_t epoch = world_sim_epoch();
    if (player == NULL || !free_move_tick_eligible()
        || (player->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0
        || critter_is_prone(player)) {
        route.cancel();
        return;
    }
    Object* door = const_cast<Object*>(static_cast<const Object*>(route.door(object_registry(), epoch)));
    const bool busy = anim_actor_has_work(player);
    const bool open = door != NULL && obj_is_open(door) != 0;
    if (route.pollDoor(object_registry(), epoch, player, busy, open) != RouteFollower::DoorWait::Resume) return;
    Object* actor = free_move_acquire();
    if (actor == NULL) {
        route.cancel();
        return;
    }
    if (!route.resume(actor->tile, free_move_plan)) {
        free_move_halt();
        return;
    }
    tickIntent = tickGoto = true;
    intentToken = movementToken;
    intentEpoch = epoch;
    sampleEpoch = epoch;
}

void free_move_sim_tick()
{
    if (movementClockEpoch != world_sim_epoch()) {
        doorCooldownTicks = 0;
        movementClockEpoch = world_sim_epoch();
    }
    if (doorCooldownTicks != 0) --doorCooldownTicks;
    if (route.phase() == RouteFollower::Phase::WaitingDoor) free_move_route_poll();
    if (!tickIntent) return;
    if (!free_move_tick_eligible() || intentEpoch != world_sim_epoch()) {
        free_move_sim_cancel();
        return;
    }
    const uint64_t token = intentToken;
    Object* actor = free_move_resolve(token);
    if (actor == NULL) return;
    if ((actor->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0 || critter_is_prone(actor)) {
        free_move_yield(actor);
        return;
    }
    float x = tickX, y = tickY;
    bool running = tickRunning;
    if (tickGoto) {
        int ox, oy;
        if (tile_coord(actor->tile, &ox, &oy, actor->elevation) == -1) {
            free_move_stop();
            return;
        }
        RouteFollower::Steering steering = route.follow(object_registry(), world_sim_epoch(), actor, actor->tile,
            ox + actor->x, oy + actor->y, free_move_route_center, free_move_plan);
        if (steering.act != RouteFollower::Act::Steer) {
            // Arrived, or the route gave up: it has already ended.
            free_move_stop();
            return;
        }
        x = static_cast<float>(steering.x);
        y = static_cast<float>(steering.y);
        running = gotoRunning;
    }
    if ((actor->data.critter.combat.results & DAM_CRIP_LEG_ANY) != 0) running = false;
    Dirty dirty = { false, { 0, 0, 0, 0 } };
    Rect part;
    if (!active) {
        active = true;
        wasRunning = !running;
    }
    int anim = FID_ANIM_TYPE(actor->fid);
    if (running != wasRunning || (anim != ANIM_WALK && anim != ANIM_RUNNING)) {
        wasRunning = running;
        if (free_move_set_pose(token, running, &part)) dirty_add(&dirty, &part);
    }
    if (free_move_face(token, x, y, &part)) dirty_add(&dirty, &part);
    actor = free_move_resolve(token);
    if (actor == NULL) return;
    if (running && !perk_level(PERK_SILENT_RUNNING)) pc_flag_off(PC_FLAG_SNEAKING);
    if (strideFid != actor->fid) {
        CacheEntry* handle;
        Art* art = art_ptr_lock(actor->fid, &handle);
        if (art == NULL) {
            free_move_yield(actor);
            return;
        }
        std::vector<StrideOffset> offsets;
        int frames = art_frame_max_frame(art);
        bool valid = frames > 0;
        for (int i = 0; i < frames && valid; i++) {
            int hx, hy;
            valid = art_frame_hot(art, i, actor->rotation, &hx, &hy) == 0;
            if (valid) offsets.push_back({ hx, hy });
        }
        int fps = art_frame_fps(art);
        art_ptr_unlock(handle);
        if (!valid || !authoredStride.build(offsets, fps)) {
            free_move_yield(actor);
            return;
        }
        strideFid = actor->fid;
    }
    double magnitude = std::hypot(x, y);
    double rate = running ? 1.0 : std::max(0.35, std::min(1.0, magnitude));
    // Keep a local math pass: a spatial callback may reset global movement and
    // acquire a new owner while this pass is on the stack.
    ContinuousMove pass = continuousMove;
    const AuthoredStride stride = authoredStride;
    MovementContact contact;
    bool valid = pass.tick(stride, x, y, rate, [&](int sx, int sy) {
        Object* current = free_move_resolve(token);
        if (current == NULL) return MoveStep { false };
        int ox, oy;
        if (tile_coord(current->tile, &ox, &oy, current->elevation) == -1) return MoveStep { false };
        ox += current->x;
        oy += current->y;
        blockedDoor = {};
        bool moved = free_move_apply(token, sx, sy, &part);
        contact.record(moved, blockedDoor);
        if (moved) blockedDoor = {};
        current = free_move_resolve(token);
        if (current == NULL) return MoveStep { false };
        if (!free_move_tick_eligible()) {
            free_move_sim_cancel();
            return MoveStep { false };
        }
        if (!moved) return MoveStep {};
        dirty_add(&dirty, &part);
        int nx, ny;
        if (tile_coord(current->tile, &nx, &ny, current->elevation) == -1) return MoveStep { false };
        return MoveStep { true, nx + current->x - ox, ny + current->y - oy };
    });
    actor = free_move_resolve(token);
    if (!valid || actor == NULL) {
        free_move_discard(token);
        return;
    }
    continuousMove = pass;
    if (obj_set_frame(actor, stride.frame(pass.phase()), &part) == 0) dirty_add(&dirty, &part);
    if (dirty.valid) tile_refresh_rect(&dirty.rect, actor->elevation);
    ObjectRef finalDoor = contact.blockedDoor();
    // A route has its own door budget, so the cooldown only gates plain walks.
    if (finalDoor.handle.valid() && (tickGoto || doorCooldownTicks == 0)) {
        ObjectRef door = finalDoor;
        ObjectRef player = object_ref_make(object_registry(), actor, world_sim_epoch());
        if (tickGoto) {
            auto* doorObject = static_cast<Object*>(object_ref_resolve(object_registry(), door, world_sim_epoch()));
            if (doorObject == NULL || !route.requestDoor(object_registry(), world_sim_epoch(), actor, doorObject)) {
                // The door is gone or has used its attempts. The route is over.
                blockedDoor = {};
                free_move_stop();
                return;
            }
        }
        doorCooldownTicks = FREE_MOVE_DOOR_COOLDOWN * 60 / 1000;
        free_move_halt();
        pendingDoor = door;
        pendingActor = player;
    }
    blockedDoor = {};
}

} // namespace fallout
