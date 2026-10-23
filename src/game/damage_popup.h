#ifndef FALLOUT_GAME_DAMAGE_POPUP_H_
#define FALLOUT_GAME_DAMAGE_POPUP_H_

#include "game/object_types.h"

namespace fallout {

// Shows the damage dealt to `target`, or "Miss", above it after `delayMs`.
// The number is a short-lived text object. `critical` makes it stand out.
void damage_popup_schedule(Object* target, int damage, bool hit, bool critical, unsigned int delayMs);

// Shows the popups that are due. Call it every frame during combat.
void damage_popup_update();

} // namespace fallout

#endif /* FALLOUT_GAME_DAMAGE_POPUP_H_ */
