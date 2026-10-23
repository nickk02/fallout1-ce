#ifndef FALLOUT_GAME_SOFT_TARGET_H_
#define FALLOUT_GAME_SOFT_TARGET_H_

#include "game/object_types.h"

namespace fallout {

// Picks the best interactable object in front of the player, outlines it, and
// returns `true` if there is one. In combat only enemy creatures qualify. Called
// once per frame from the map and combat loops.
bool soft_target_update(bool inCombat);

// Runs the default action on the current target: talk, pick up, loot, use or
// attack. Returns `true` if an action was started.
bool soft_target_interact();

// Attacks the closest creature in front of the player, friendly or not.
bool soft_target_attack();

// Describes the current target in the message log.
bool soft_target_inspect();

// Returns `true` for a living creature from another team that should be
// attacked on sight. Humans, ghouls and brahmin are not treated as enemies.
bool soft_target_is_hostile(Object* critter);

// Makes `object` the target until the player selects something else or walks
// away. Used for mouse clicks, which only select. The actions then come from
// the keys or the buttons of the prompt.
void soft_target_select(Object* object);

// Finds an enemy at a screen position. Clicking a small creature exactly is
// hard, so this also accepts an enemy on a neighboring hex. Returns `NULL` when
// there is none.
Object* soft_target_enemy_near(int screenX, int screenY);

// Removes the outline. Called when the map loop is not active.
void soft_target_clear();

} // namespace fallout

#endif /* FALLOUT_GAME_SOFT_TARGET_H_ */
