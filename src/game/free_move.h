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

// Walks toward `tile` the way the keys do: a click on the ground. The walk ends
// on arrival, when the way is blocked for a moment, or when anything calls
// `free_move_stop` or `free_move_goto_cancel`.
void free_move_goto(int tile, bool running);

// `true` while a walk to a tile is under way.
bool free_move_goto_active();

// Forgets the walk to a tile without touching the pose.
void free_move_goto_cancel();

// Takes the next step of the walk to a tile. Returns `false` when there is none.
bool free_move_goto_step(float elapsed);

// `true` between the first step and `free_move_stop`.
bool free_move_is_active();

} // namespace fallout

#endif /* FALLOUT_GAME_FREE_MOVE_H_ */
