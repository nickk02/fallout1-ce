#ifndef FALLOUT_GAME_SOFT_TARGET_H_
#define FALLOUT_GAME_SOFT_TARGET_H_

namespace fallout {

struct Object;

// Picks the best interactable object in front of the player, outlines it, and
// returns `true` if there is one. Called once per frame from the map loop.
bool soft_target_update();

// Runs the default action on the current target: talk, pick up, loot, use or
// attack. Returns `true` if an action was started.
bool soft_target_interact();

// Returns `true` for a living creature from another team that should be
// attacked on sight. Humans, ghouls and brahmin are not treated as enemies.
bool soft_target_is_hostile(Object* critter);

// Removes the outline. Called when the map loop is not active.
void soft_target_clear();

} // namespace fallout

#endif /* FALLOUT_GAME_SOFT_TARGET_H_ */
