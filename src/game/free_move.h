#ifndef FALLOUT_GAME_FREE_MOVE_H_
#define FALLOUT_GAME_FREE_MOVE_H_

namespace fallout {

struct Object;
// Legacy registration/clear relinquishes movement without writing a pose.
void free_move_yield(Object* actor);
// Discard stale ownership at shared movement entry, without writing a pose.
void free_move_validate_owner();

bool free_move_uses_sim_ticks();
void free_move_sim_sample(float x, float y, bool running);
void free_move_sim_tick();
void free_move_sim_cancel();
// Dispatches a door interaction requested by collision, outside the tick.
bool free_move_dispatch_pending();

// Moves the player a little in the given screen direction (unit length or
// shorter). Unlike a walk along hex steps, the player glides in any direction,
// crossing from one hex into the next as it goes. Returns `false` if the player
// cannot move right now, for example while another animation plays.
bool free_move_step(float dirX, float dirY, bool running, float elapsed);

// Stops walking and returns to the standing pose.
void free_move_stop();

// A pause of `elapsed` ms just ended. Moves the timers of a walk in progress.
void free_move_world_resumed(unsigned int elapsed);

// Walks toward `tile`: a click on the ground. A click is a new intent and ends
// any earlier walk first. With owned simulation ticks the walk follows the hex
// pathfinder's route, plans through unlocked doors, replans a few times when
// blocked, and a click on an unreachable or the current tile leaves the player
// standing. Otherwise it steers straight at the tile. The walk ends on arrival,
// when it is blocked for good, or when anything calls `free_move_stop` or
// `free_move_goto_cancel`.
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
