#include "game/soft_target.h"

#include <stdio.h>
#include <string.h>

#include "game/actions.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/intface.h"
#include "game/map.h"
#include "game/object.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/tile.h"
#include "plib/color/color.h"
#include "plib/gnw/button.h"
#include "plib/gnw/controller.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/input.h"
#include "plib/gnw/intrface.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

// Hexes within which an object can become the target.
#define SOFT_TARGET_RANGE 5
#define SOFT_TARGET_SCENERY_RANGE 3

// Hexes within which the attack button finds a creature.
#define SOFT_TARGET_ATTACK_RANGE 8

static Object* softTarget = nullptr;
static Object* softOutlined = nullptr;
static bool softOutlineBorrowed = false;
static bool softWide = false;

#define SOFT_TARGET_PROMPT_WIDTH 420
#define SOFT_TARGET_PROMPT_HEIGHT 18

static int promptWindow = -1;
static bool promptVisible = false;
static char promptShown[128] = "";

typedef enum PromptActionKind {
    PROMPT_ACTION_USE,
    PROMPT_ACTION_ATTACK,
    PROMPT_ACTION_INSPECT,
} PromptActionKind;

typedef struct PromptAction {
    PromptActionKind kind;
    char label[48];
} PromptAction;

#define SOFT_TARGET_MAX_ACTIONS 3

static Object* selectedTarget = nullptr;
static int promptButtons[SOFT_TARGET_MAX_ACTIONS] = { -1, -1, -1 };
static PromptActionKind promptButtonKinds[SOFT_TARGET_MAX_ACTIONS];

static Object* soft_target_pick(bool creaturesOnly, bool enemiesOnly, bool* outlinedObjectExists, bool* selectedExists);
static void soft_target_show_prompt(const char* title, const PromptAction* actions, int actionCount);
static void soft_target_hide_prompt();
static int soft_target_describe(Object* target, bool inCombat, char* title, size_t titleSize, PromptAction* actions);
static void soft_target_prompt_process();
static void soft_target_prompt_clicked(int button, int keyCode);
static int soft_target_score(Object* object, int range);
static void soft_target_set_outline(Object* object);

bool soft_target_update(bool inCombat)
{
    bool outlinedObjectExists = false;
    bool selectedExists = false;
    Object* best = soft_target_pick(inCombat, inCombat, &outlinedObjectExists, &selectedExists);

    // The outlined or selected object may be gone after a map change. Do not
    // touch it then.
    if (!outlinedObjectExists) {
        softOutlined = nullptr;
    }

    if (!selectedExists) {
        selectedTarget = nullptr;
    }

    // A selection from the mouse wins over the automatic pick, unless the
    // controller is playing. In combat only enemies can be selected. With the
    // mouse, only what was clicked is highlighted. The automatic pick is shown
    // only for the keyboard and the controller.
    if (selectedTarget != nullptr && (!input_controller_in_use() || softWide)) {
        bool allowed = !inCombat
            || (FID_TYPE(selectedTarget->fid) == OBJ_TYPE_CRITTER && soft_target_is_hostile(selectedTarget));
        if (allowed) {
            best = selectedTarget;
        } else if (input_mouse_is_active()) {
            best = nullptr;
        }
    } else if (input_mouse_is_active()) {
        best = nullptr;
    }

    softTarget = best;
    soft_target_set_outline(best);

    if (best != nullptr) {
        char title[64];
        PromptAction actions[SOFT_TARGET_MAX_ACTIONS];
        int actionCount = soft_target_describe(best, inCombat, title, sizeof(title), actions);
        soft_target_show_prompt(title, actions, actionCount);
    } else {
        soft_target_hide_prompt();
    }

    return best != nullptr;
}

void soft_target_select(Object* object)
{
    selectedTarget = object;
}

// Finds the best object in front of the player. With `creaturesOnly` it looks
// for any living creature within attack range instead.
static Object* soft_target_pick(bool creaturesOnly, bool enemiesOnly, bool* outlinedObjectExists, bool* selectedExists)
{
    static const int allTypes[] = { OBJ_TYPE_CRITTER, OBJ_TYPE_ITEM, OBJ_TYPE_SCENERY };
    static const int creatureTypes[] = { OBJ_TYPE_CRITTER };

    const int* types = creaturesOnly ? creatureTypes : allTypes;
    int typeCount = creaturesOnly ? 1 : 3;

    Object* best = nullptr;
    int bestScore = 0;

    for (int typeIndex = 0; typeIndex < typeCount; typeIndex++) {
        int type = types[typeIndex];

        Object** objects;
        int count = obj_create_list(-1, obj_dude->elevation, type, &objects);
        if (count <= 0) {
            continue;
        }

        for (int index = 0; index < count; index++) {
            Object* object = objects[index];
            if (outlinedObjectExists != nullptr && object == softOutlined) {
                *outlinedObjectExists = true;
            }

            if (selectedExists != nullptr && object == selectedTarget) {
                *selectedExists = true;
            }

            if (object == obj_dude) {
                continue;
            }

            if (type == OBJ_TYPE_SCENERY && !proto_action_can_use(object->pid)) {
                continue;
            }

            if (creaturesOnly && critter_is_dead(object)) {
                continue;
            }

            if (enemiesOnly && !soft_target_is_hostile(object)) {
                continue;
            }

            int range = SOFT_TARGET_RANGE;
            if (creaturesOnly) {
                range = SOFT_TARGET_ATTACK_RANGE;
            } else if (type == OBJ_TYPE_SCENERY) {
                range = SOFT_TARGET_SCENERY_RANGE;
            }

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

    return best;
}

bool soft_target_attack()
{
    Object* target = nullptr;
    if (softTarget != nullptr && FID_TYPE(softTarget->fid) == OBJ_TYPE_CRITTER && !critter_is_dead(softTarget)) {
        target = softTarget;
    } else {
        target = soft_target_pick(true, false, nullptr, nullptr);
    }

    if (target == nullptr) {
        return false;
    }

    combat_attack_this(target);
    return true;
}

bool soft_target_inspect()
{
    Object* target = softTarget;
    if (target == nullptr) {
        return false;
    }

    if (obj_examine(obj_dude, target) == -1) {
        obj_look_at(obj_dude, target);
    }
    return true;
}

bool soft_target_interact()
{
    Object* target = softTarget;
    if (target == nullptr) {
        return false;
    }

    // The prompt belongs to the map. Screens opened by an action must not
    // show it.
    soft_target_hide_prompt();

    if (isInCombat()) {
        // In a fight the only thing to do with an enemy is attack it, and that
        // has its own button.
        return false;
    }

    switch (FID_TYPE(target->fid)) {
    case OBJ_TYPE_ITEM:
        action_get_an_object(obj_dude, target);
        return true;
    case OBJ_TYPE_CRITTER:
        if (soft_target_is_hostile(target)) {
            // Attacking has its own button.
            return false;
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

void soft_target_set_wide(bool wide)
{
    softWide = wide;
}

void soft_target_cycle(int direction)
{
    // Collect the living enemies and order them by distance from the player.
    Object** list;
    int count = obj_create_list(-1, obj_dude->elevation, OBJ_TYPE_CRITTER, &list);
    if (count <= 0) {
        return;
    }

    Object* enemies[64];
    int distances[64];
    int enemyCount = 0;
    for (int index = 0; index < count && enemyCount < 64; index++) {
        if (list[index] != obj_dude && soft_target_is_hostile(list[index])) {
            enemies[enemyCount] = list[index];
            distances[enemyCount] = obj_dist(obj_dude, list[index]);
            enemyCount++;
        }
    }
    obj_delete_list(list);

    if (enemyCount == 0) {
        return;
    }

    for (int first = 0; first < enemyCount; first++) {
        for (int second = first + 1; second < enemyCount; second++) {
            if (distances[second] < distances[first]) {
                Object* object = enemies[first];
                enemies[first] = enemies[second];
                enemies[second] = object;

                int distance = distances[first];
                distances[first] = distances[second];
                distances[second] = distance;
            }
        }
    }

    int current = -1;
    for (int index = 0; index < enemyCount; index++) {
        if (enemies[index] == selectedTarget || (selectedTarget == nullptr && enemies[index] == softTarget)) {
            current = index;
            break;
        }
    }

    int next = current == -1 ? 0 : (current + direction + enemyCount) % enemyCount;
    selectedTarget = enemies[next];
}

Object* soft_target_enemy_near(int screenX, int screenY)
{
    int center = tile_num(screenX, screenY, obj_dude->elevation);
    if (center == -1) {
        return nullptr;
    }

    // The clicked hex first, then its neighbors.
    for (int ring = 0; ring <= ROTATION_COUNT; ring++) {
        int tile = ring == 0 ? center : tile_num_in_direction(center, ring - 1, 1);
        if (tile == -1) {
            continue;
        }

        Object** objects;
        int count = obj_create_list(tile, obj_dude->elevation, OBJ_TYPE_CRITTER, &objects);
        if (count <= 0) {
            continue;
        }

        Object* found = nullptr;
        for (int index = 0; index < count; index++) {
            if (objects[index] != obj_dude && soft_target_is_hostile(objects[index])) {
                found = objects[index];
                break;
            }
        }

        obj_delete_list(objects);

        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

void soft_target_clear()
{
    softTarget = nullptr;
    soft_target_set_outline(nullptr);
    soft_target_hide_prompt();
}

// Describes what the target offers. Returns the number of actions. The key
// names follow the device in use.
static int soft_target_describe(Object* target, bool inCombat, char* title, size_t titleSize, PromptAction* actions)
{
    bool pad = input_controller_in_use();
    const char* use = pad ? "A" : "E";
    const char* attack = pad ? "RT" : "F";
    const char* inspect = pad ? "Y" : "Q";

    snprintf(title, titleSize, "%s:", object_name(target));

    int count = 0;
    switch (FID_TYPE(target->fid)) {
    case OBJ_TYPE_CRITTER:
        if (critter_is_dead(target)) {
            actions[count].kind = PROMPT_ACTION_USE;
            snprintf(actions[count++].label, sizeof(actions[0].label), "[%s] Search", use);
        } else if (soft_target_is_hostile(target)) {
            actions[count].kind = PROMPT_ACTION_ATTACK;
            snprintf(actions[count++].label, sizeof(actions[0].label), "[%s] Attack", attack);
        } else {
            actions[count].kind = PROMPT_ACTION_USE;
            snprintf(actions[count++].label, sizeof(actions[0].label), "[%s] %s", use, obj_action_can_talk_to(target) ? "Talk" : "Search");
            actions[count].kind = PROMPT_ACTION_ATTACK;
            snprintf(actions[count++].label, sizeof(actions[0].label), "[%s] Attack", attack);
        }
        break;
    case OBJ_TYPE_ITEM:
        actions[count].kind = PROMPT_ACTION_USE;
        snprintf(actions[count++].label, sizeof(actions[0].label), "[%s] Pick up", use);
        break;
    default:
        actions[count].kind = PROMPT_ACTION_USE;
        snprintf(actions[count++].label, sizeof(actions[0].label), "[%s] Use", use);
        break;
    }

    actions[count].kind = PROMPT_ACTION_INSPECT;
    snprintf(actions[count++].label, sizeof(actions[0].label), "[%s] Inspect", inspect);

    (void)inCombat;
    return count;
}

// A one-line window above the interface bar: the target's name and its
// actions. Each action is a button, so the mouse can click it. The window is
// separate so the map never has to redraw it.
static void soft_target_show_prompt(const char* title, const PromptAction* actions, int actionCount)
{
    char key[256];
    int length = snprintf(key, sizeof(key), "%s", title);
    for (int index = 0; index < actionCount; index++) {
        length += snprintf(key + length, sizeof(key) - length, "|%s", actions[index].label);
    }

    if (promptVisible && strcmp(key, promptShown) == 0) {
        return;
    }

    if (promptWindow == -1) {
        int x = (screenGetWidth() - SOFT_TARGET_PROMPT_WIDTH) / 2;
        int y = screenGetHeight() - INTERFACE_BAR_HEIGHT - SOFT_TARGET_PROMPT_HEIGHT - 6;
        promptWindow = win_add(x, y, SOFT_TARGET_PROMPT_WIDTH, SOFT_TARGET_PROMPT_HEIGHT, colorTable[0], WINDOW_HIDDEN);
        if (promptWindow == -1) {
            return;
        }

        add_bk_process(soft_target_prompt_process);
    }

    strncpy(promptShown, key, sizeof(promptShown) - 1);
    promptShown[sizeof(promptShown) - 1] = '\0';

    for (int index = 0; index < SOFT_TARGET_MAX_ACTIONS; index++) {
        if (promptButtons[index] != -1) {
            win_delete_button(promptButtons[index]);
            promptButtons[index] = -1;
        }
    }

    int oldFont = text_curr();
    text_font(101);

    const int gap = 14;
    int totalWidth = text_width(title);
    for (int index = 0; index < actionCount; index++) {
        totalWidth += gap + text_width(actions[index].label);
    }

    win_fill(promptWindow, 0, 0, SOFT_TARGET_PROMPT_WIDTH, SOFT_TARGET_PROMPT_HEIGHT, colorTable[0]);

    int x = (SOFT_TARGET_PROMPT_WIDTH - totalWidth) / 2;
    win_print(promptWindow, title, 0, x, 2, colorTable[992]);
    x += text_width(title);

    for (int index = 0; index < actionCount; index++) {
        x += gap;

        int width = text_width(actions[index].label);
        win_print(promptWindow, actions[index].label, 0, x, 2, colorTable[32767]);

        promptButtonKinds[index] = actions[index].kind;
        promptButtons[index] = win_register_button(promptWindow, x, 0, width, SOFT_TARGET_PROMPT_HEIGHT, -1, -1, -1, -1, nullptr, nullptr, nullptr, BUTTON_FLAG_TRANSPARENT);
        if (promptButtons[index] != -1) {
            win_register_button_func(promptButtons[index], nullptr, nullptr, nullptr, soft_target_prompt_clicked);
        }

        x += width;
    }

    text_font(oldFont);

    win_draw(promptWindow);
    if (!promptVisible) {
        win_show(promptWindow);
        promptVisible = true;
    }
}

static void soft_target_hide_prompt()
{
    if (promptVisible && promptWindow != -1) {
        win_hide(promptWindow);
    }

    promptVisible = false;
    promptShown[0] = '\0';
}

static void soft_target_prompt_clicked(int button, int keyCode)
{
    (void)keyCode;

    for (int index = 0; index < SOFT_TARGET_MAX_ACTIONS; index++) {
        if (promptButtons[index] != button) {
            continue;
        }

        switch (promptButtonKinds[index]) {
        case PROMPT_ACTION_USE:
            soft_target_interact();
            break;
        case PROMPT_ACTION_ATTACK:
            soft_target_attack();
            break;
        case PROMPT_ACTION_INSPECT:
            soft_target_inspect();
            break;
        }
        return;
    }
}

// Runs in every loop, including menus, so the prompt never stays up over a
// screen that opened on top of the map.
static void soft_target_prompt_process()
{
    if (promptVisible && !controller_world_is_active()) {
        soft_target_hide_prompt();
    }
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
    if (softWide) {
        return distance <= 60 ? distance : -1;
    }

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
    Rect rect;

    if (object == softOutlined) {
        // Combat switches creature outlines off when it starts. Put ours back.
        if (object != nullptr && (object->outline & OUTLINE_DISABLED) != 0) {
            if (obj_turn_on_outline(object, &rect) == 0) {
                tile_refresh_rect(&rect, map_elevation);
            }
        }
        return;
    }

    if (softOutlined != nullptr) {
        // An outline that combat already owned is only switched off.
        int result = softOutlineBorrowed ? obj_turn_off_outline(softOutlined, &rect) : obj_remove_outline(softOutlined, &rect);
        if (result == 0) {
            tile_refresh_rect(&rect, map_elevation);
        }
        softOutlined = nullptr;
        softOutlineBorrowed = false;
    }

    if (object != nullptr) {
        int outlineType = OUTLINE_TYPE_ITEM;
        if (FID_TYPE(object->fid) == OBJ_TYPE_CRITTER) {
            outlineType = soft_target_is_hostile(object) ? OUTLINE_TYPE_HOSTILE : OUTLINE_TYPE_FRIENDLY;
        }

        if (obj_outline_object(object, outlineType, &rect) == 0) {
            tile_refresh_rect(&rect, map_elevation);
            softOutlined = object;
        } else if ((object->outline & OUTLINE_TYPE_MASK) != 0 && obj_turn_on_outline(object, &rect) == 0) {
            // Combat gave this creature an outline and switched it off.
            tile_refresh_rect(&rect, map_elevation);
            softOutlined = object;
            softOutlineBorrowed = true;
        }
    }
}

} // namespace fallout
