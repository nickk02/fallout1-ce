#ifndef FALLOUT_PLIB_GNW_CONTROLLER_H_
#define FALLOUT_PLIB_GNW_CONTROLLER_H_

#include <SDL.h>

namespace fallout {

bool controller_init();
void controller_exit();

// Handles `SDL_CONTROLLERDEVICEADDED` and `SDL_CONTROLLERDEVICEREMOVED`.
void controller_handle_event(SDL_Event* event);

// Ignores the controller until every button is released and the stick is
// centered again. Called when the window loses focus.
void controller_focus_lost();

// The game calls this every frame while the map view is active and the player
// is free to move. In that state the left stick moves the character and the
// right stick moves the cursor. Everywhere else the left stick moves the
// cursor.
void controller_world_tick();

// Returns `true` and the left stick deflection (screen axes, length 0..1)
// while the world is active and the stick is outside the dead zone.
bool controller_get_move_vector(float* x, float* y);

// While the map is active and an object is targeted, A interacts with it
// instead of clicking at the cursor. Moving the right stick hands A back to the
// cursor for a short time. The game sets availability every frame and takes the
// pending press.
void controller_set_interact_available(bool available);
bool controller_take_interact();

// Adds controller movement and button state on top of the mouse state.
// Returns `true` if the controller changed anything.
bool controller_get_mouse_state(int* x, int* y, unsigned char* leftButton, unsigned char* rightButton);

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_CONTROLLER_H_ */
