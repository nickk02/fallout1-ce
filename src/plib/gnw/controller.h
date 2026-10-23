#ifndef FALLOUT_PLIB_GNW_CONTROLLER_H_
#define FALLOUT_PLIB_GNW_CONTROLLER_H_

#include <SDL.h>

namespace fallout {

typedef enum ControllerAction {
    CONTROLLER_ACTION_INTERACT,
    CONTROLLER_ACTION_ATTACK,
    CONTROLLER_ACTION_INSPECT,
    CONTROLLER_ACTION_NEXT_TARGET,
    CONTROLLER_ACTION_PREVIOUS_TARGET,
    CONTROLLER_ACTION_COUNT,
} ControllerAction;

bool controller_init();

// `true` while a controller is open.
bool controller_is_connected();
void controller_exit();

// Handles device connect and disconnect events, and button presses.
void controller_handle_event(SDL_Event* event);

// Ignores the controller until every button is released and the stick is
// centered again. Called when the window loses focus.
void controller_focus_lost();

// The game calls this every frame while the map or combat loop runs and the
// player controls the character. While it runs, the controller drives the
// character directly: the left stick moves, A interacts, the right trigger
// attacks, Y inspects, and the other buttons open game screens. Everywhere else
// the controller moves the cursor with the left stick and A clicks.
void controller_world_tick();

// `true` while the world loop is running. The keyboard uses the same state to
// give WASD to movement.
bool controller_world_is_active();

// Returns `true` and the left stick deflection (screen axes, length 0..1)
// while the world is active and the stick is outside the dead zone.
bool controller_get_move_vector(float* x, float* y);

// Keyboard keys that do what the controller's buttons do while the world loop
// runs: E interacts, F attacks, Q inspects, K opens the skilldex (S moves the
// character). Returns `true` if the key was one of them, so the game does not
// see it. `pressed` is `false` for releases and key repeats.
bool controller_handle_key(int scancode, bool pressed);

// Returns `true` once per press of the button behind `action`.
bool controller_take_action(ControllerAction action);

// Adds controller movement and button state on top of the mouse state. Only
// used outside the world loop.
// Returns `true` if the controller changed anything.
bool controller_get_mouse_state(int* x, int* y, unsigned char* leftButton, unsigned char* rightButton);

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_CONTROLLER_H_ */
