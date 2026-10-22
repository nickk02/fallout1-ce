#ifndef FALLOUT_GAME_STICK_MOVE_H_
#define FALLOUT_GAME_STICK_MOVE_H_

namespace fallout {

// Moves the player in real time from the controller's left stick. Called once
// per frame from the map loop.
void stick_move_update();

// Keeps the player centered on screen. Manual scrolling stays in effect until
// the player moves to another tile. Called once per frame from the map and
// combat loops.
void camera_follow_update();

} // namespace fallout

#endif /* FALLOUT_GAME_STICK_MOVE_H_ */
