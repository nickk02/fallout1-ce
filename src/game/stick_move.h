#ifndef FALLOUT_GAME_STICK_MOVE_H_
#define FALLOUT_GAME_STICK_MOVE_H_

namespace fallout {

// Moves the player in real time from the controller's left stick. Called once
// per frame from the map loop.
void stick_move_update();

} // namespace fallout

#endif /* FALLOUT_GAME_STICK_MOVE_H_ */
