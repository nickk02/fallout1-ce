#ifndef FALLOUT_GAME_FREE_MOVE_H_
#define FALLOUT_GAME_FREE_MOVE_H_

namespace fallout {

// Moves the player a little in the given screen direction (unit length or
// shorter). Unlike a walk along hex steps, the player glides in any direction,
// crossing from one hex into the next as it goes. Returns `false` if the player
// cannot move right now, for example while another animation plays.
bool free_move_step(float dirX, float dirY, bool running, float elapsed);

// Stops walking and returns to the standing pose.
void free_move_stop();

// `true` between the first step and `free_move_stop`.
bool free_move_is_active();

} // namespace fallout

#endif /* FALLOUT_GAME_FREE_MOVE_H_ */
