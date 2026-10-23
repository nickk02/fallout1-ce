#ifndef FALLOUT_GAME_STICK_MOVE_H_
#define FALLOUT_GAME_STICK_MOVE_H_

namespace fallout {

// Moves the player in real time from the left stick or WASD and runs the
// controller's action buttons. Called once per frame from the map loop.
void stick_move_update();

// Same as `stick_move_update`, for the player's turn in combat. Movement is
// limited by action points.
void stick_move_update_combat();

// The combat pause. Runs the action buttons and target picking, but does not
// move the player, because time is frozen.
void stick_move_update_paused();

// Keeps the player centered on screen. Called once per frame from the map and
// combat loops.
void camera_follow_update();

} // namespace fallout

#endif /* FALLOUT_GAME_STICK_MOVE_H_ */
