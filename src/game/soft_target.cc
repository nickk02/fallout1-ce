#include "game/soft_target.h"

#include "game/actions.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/map.h"
#include "game/object.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/tile.h"

namespace fallout {

// Hexes within which an object can become the target.
#define SOFT_TARGET_RANGE 5
#define SOFT_TARGET_SCENERY_RANGE 3

static Object* softTarget = nullptr;
static Object* softOutlined = nullptr;

static int soft_target_score(Object* object, int range);
static void soft_target_set_outline(Object* object);

bool soft_target_update()
{
    static const int types[] = { OBJ_TYPE_CRITTER, OBJ_TYPE_ITEM, OBJ_TYPE_SCENERY };

    Object* best = nullptr;
    int bestScore = 0;
    bool outlinedObjectExists = false;

    for (int type : types) {
        Object** objects;
        int count = obj_create_list(-1, obj_dude->elevation, type, &objects);
        if (count <= 0) {
            continue;
        }

        for (int index = 0; index < count; index++) {
            Object* object = objects[index];
            if (object == softOutlined) {
                outlinedObjectExists = true;
            }

            if (object == obj_dude) {
                continue;
            }

            if (type == OBJ_TYPE_SCENERY && !proto_action_can_use(object->pid)) {
                continue;
            }

            int range = type == OBJ_TYPE_SCENERY ? SOFT_TARGET_SCENERY_RANGE : SOFT_TARGET_RANGE;
            int score = soft_target_score(object, range);
            if (score == -1) {
                continue;
            }

            if (type == OBJ_TYPE_CRITTER && soft_target_is_hostile(object)) {
                // Prefer enemies over loot and doors at similar distances.
                score -= 3;
            }

            if (best == nullptr || score < bestScore) {
                best = object;
                bestScore = score;
            }
        }

        obj_delete_list(objects);
    }

    // The outlined object may be gone after a map change. Do not touch it then.
    if (!outlinedObjectExists) {
        softOutlined = nullptr;
    }

    softTarget = best;
    soft_target_set_outline(best);
    return best != nullptr;
}

bool soft_target_interact()
{
    Object* target = softTarget;
    if (target == nullptr) {
        return false;
    }

    switch (FID_TYPE(target->fid)) {
    case OBJ_TYPE_ITEM:
        action_get_an_object(obj_dude, target);
        return true;
    case OBJ_TYPE_CRITTER:
        if (soft_target_is_hostile(target)) {
            combat_attack_this(target);
        } else if (obj_action_can_talk_to(target)) {
            action_talk_to(obj_dude, target);
        } else {
            action_loot_container(obj_dude, target);
        }
        return true;
    case OBJ_TYPE_SCENERY:
        action_use_an_object(obj_dude, target);
        return true;
    }

    return false;
}

void soft_target_clear()
{
    softTarget = nullptr;
    soft_target_set_outline(nullptr);
}

// Most critters can be talked to in the data, including rats, so the talk flag
// does not tell friend from enemy. Humans and ghouls from another team are
// treated as people to talk to. Other creatures from another team are enemies.
bool soft_target_is_hostile(Object* critter)
{
    if (critter_is_dead(critter) || critter->data.critter.combat.team == obj_dude->data.critter.combat.team) {
        return false;
    }

    switch (critter_kill_count_type(critter)) {
    case KILL_TYPE_MAN:
    case KILL_TYPE_WOMAN:
    case KILL_TYPE_CHILD:
    case KILL_TYPE_GHOUL:
    case KILL_TYPE_BRAHMIN:
        return false;
    }

    return true;
}

// Lower is better. Returns -1 when the object is out of range or behind the
// player. Distance counts most, and the angle away from the facing direction
// adds to it.
static int soft_target_score(Object* object, int range)
{
    int distance = obj_dist(obj_dude, object);
    if (distance > range) {
        return -1;
    }

    int turn = abs(tile_dir(obj_dude->tile, object->tile) - obj_dude->rotation);
    if (turn > ROTATION_COUNT / 2) {
        turn = ROTATION_COUNT - turn;
    }

    // Objects on the player's own tile or neighbors are always reachable.
    if (distance > 1 && turn > 1) {
        return -1;
    }

    return distance * 2 + turn;
}

static void soft_target_set_outline(Object* object)
{
    if (object == softOutlined) {
        return;
    }

    Rect rect;
    if (softOutlined != nullptr) {
        if (obj_remove_outline(softOutlined, &rect) == 0) {
            tile_refresh_rect(&rect, map_elevation);
        }
        softOutlined = nullptr;
    }

    if (object != nullptr) {
        int outlineType = OUTLINE_TYPE_ITEM;
        if (FID_TYPE(object->fid) == OBJ_TYPE_CRITTER) {
            outlineType = soft_target_is_hostile(object) ? OUTLINE_TYPE_HOSTILE : OUTLINE_TYPE_FRIENDLY;
        }

        int outlineResult = obj_outline_object(object, outlineType, &rect);
        if (outlineResult == 0) {
            tile_refresh_rect(&rect, map_elevation);
            softOutlined = object;
        }
    }
}

} // namespace fallout
