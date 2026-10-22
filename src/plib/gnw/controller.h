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

// Adds controller movement and button state on top of the mouse state.
// Returns `true` if the controller changed anything.
bool controller_get_mouse_state(int* x, int* y, unsigned char* leftButton, unsigned char* rightButton);

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_CONTROLLER_H_ */
