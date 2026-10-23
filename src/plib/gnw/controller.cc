#include "plib/gnw/controller.h"

#include <math.h>

#include "plib/gnw/input.h"
#include "plib/gnw/menu_focus.h"

namespace fallout {

// Radial dead zone, as a fraction of full deflection.
#define CONTROLLER_DEAD_ZONE 0.2f

// Cursor speed at full right stick deflection, in game pixels per second.
#define CONTROLLER_CURSOR_SPEED 600.0f

// Trigger deflection that counts as a press.
#define CONTROLLER_TRIGGER_THRESHOLD 16000

static SDL_GameController* controller = nullptr;
static SDL_JoystickID controllerId = -1;
static bool controllerSuppressed = false;
static Uint32 lastWorldTicks = 0;
static bool worldTicked = false;
static bool actionPending[CONTROLLER_ACTION_COUNT];
static bool rightTriggerDown = false;
static bool leftTriggerDown = false;
static Uint32 lastCursorTicks = 0;
static float cursorRemainderX = 0.0f;
static float cursorRemainderY = 0.0f;

static void controller_open(int deviceIndex);
static void controller_close();
static void controller_handle_button(SDL_GameControllerButton button);
static void controller_read_stick(SDL_GameControllerAxis axisX, SDL_GameControllerAxis axisY, float* x, float* y);

bool controller_init()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        // Not fatal, the game stays playable with mouse and keyboard.
        SDL_Log("Controller support unavailable: %s", SDL_GetError());
        return false;
    }

    // SDL queues `SDL_CONTROLLERDEVICEADDED` for devices that are already
    // connected, so there is no need to open anything here.
    return true;
}

bool controller_is_connected()
{
    return controller != nullptr;
}

void controller_exit()
{
    controller_close();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void controller_handle_event(SDL_Event* event)
{
    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        // Only one controller is used at a time.
        if (controller == nullptr) {
            controller_open(event->cdevice.which);
        }
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (controller != nullptr && event->cdevice.which == controllerId) {
            controller_close();
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        if (controller != nullptr && event->cbutton.which == controllerId && !controllerSuppressed) {
            input_note_controller();
            controller_handle_button((SDL_GameControllerButton)event->cbutton.button);
        }
        break;
    }
}

void controller_focus_lost()
{
    controllerSuppressed = true;
}

void controller_world_tick()
{
    lastWorldTicks = SDL_GetTicks();
    worldTicked = true;

    if (controller == nullptr || controllerSuppressed) {
        return;
    }

    bool rightTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > CONTROLLER_TRIGGER_THRESHOLD;
    if (rightTrigger && !rightTriggerDown) {
        actionPending[CONTROLLER_ACTION_ATTACK] = true;
    }
    rightTriggerDown = rightTrigger;

    // The left trigger ends the turn in combat. Space does nothing elsewhere.
    bool leftTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > CONTROLLER_TRIGGER_THRESHOLD;
    if (leftTrigger && !leftTriggerDown) {
        input_inject_key(SDL_SCANCODE_SPACE);
    }
    leftTriggerDown = leftTrigger;
}

bool controller_world_is_active()
{
    return worldTicked && SDL_GetTicks() - lastWorldTicks < 150;
}

bool controller_get_move_vector(float* x, float* y)
{
    if (controller == nullptr || controllerSuppressed || !controller_world_is_active()) {
        return false;
    }

    controller_read_stick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, x, y);
    if (*x != 0.0f || *y != 0.0f) {
        input_note_controller();
        return true;
    }

    return false;
}

bool controller_handle_key(int scancode, bool pressed)
{
    ControllerAction action;
    switch (scancode) {
    case SDL_SCANCODE_E:
        action = CONTROLLER_ACTION_INTERACT;
        break;
    case SDL_SCANCODE_F:
        action = CONTROLLER_ACTION_ATTACK;
        break;
    case SDL_SCANCODE_Q:
        action = CONTROLLER_ACTION_INSPECT;
        break;
    case SDL_SCANCODE_K:
        if (pressed) {
            input_inject_key(SDL_SCANCODE_S); // Skilldex
        }
        return true;
    default:
        return false;
    }

    if (pressed) {
        actionPending[action] = true;
    }
    return true;
}

bool controller_take_action(ControllerAction action)
{
    bool pending = actionPending[action];
    actionPending[action] = false;
    return pending;
}

bool controller_get_mouse_state(int* x, int* y, unsigned char* leftButton, unsigned char* rightButton)
{
    // In the world loop there is no cursor.
    if (controller == nullptr || controller_world_is_active()) {
        if (menu_focus_is_active()) {
            menu_focus_clear();
        }
        return false;
    }

    Uint32 now = SDL_GetTicks();

    bool a = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0;

    // The d-pad or the left stick moves the focus between buttons.
    int dirX = 0;
    int dirY = 0;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) dirX = -1;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) dirX = 1;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) dirY = -1;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) dirY = 1;

    float stickX;
    float stickY;
    controller_read_stick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, &stickX, &stickY);
    if (dirX == 0 && dirY == 0 && (fabsf(stickX) > 0.5f || fabsf(stickY) > 0.5f)) {
        if (fabsf(stickX) > fabsf(stickY)) {
            dirX = stickX < 0.0f ? -1 : 1;
        } else {
            dirY = stickY < 0.0f ? -1 : 1;
        }
    }

    if (controllerSuppressed) {
        // Wait for a neutral state so a button held across a focus change is
        // not reported as a fresh press.
        if (a || dirX != 0 || dirY != 0) {
            return false;
        }

        controllerSuppressed = false;
    }

    if (dirX != 0 || dirY != 0) {
        input_note_controller();
    }

    static int lastDirX = 0;
    static int lastDirY = 0;
    static Uint32 nextNavTicks = 0;
    if (dirX != 0 || dirY != 0) {
        bool changed = dirX != lastDirX || dirY != lastDirY;
        if (changed || now >= nextNavTicks) {
            menu_focus_move(dirX, dirY);
            nextNavTicks = now + (changed ? 350 : 130);
        }
    }
    lastDirX = dirX;
    lastDirY = dirY;

    menu_focus_update();

    // The first press of A only shows the focus. After that it clicks the
    // focused button, because the cursor sits on it.
    static bool aWasDown = false;
    bool click = a;
    if (a && !aWasDown && !menu_focus_is_active()) {
        menu_focus_move(0, 0);
        click = false;
    }
    aWasDown = a;

    // The right stick is a free cursor, for screens that are not made of
    // buttons, such as the world map.
    float cursorX;
    float cursorY;
    controller_read_stick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, &cursorX, &cursorY);

    float elapsed = (now - lastCursorTicks) / 1000.0f;
    lastCursorTicks = now;
    if (elapsed > 0.1f) {
        elapsed = 0.1f;
    }

    bool moved = false;
    if (cursorX != 0.0f || cursorY != 0.0f) {
        input_show_cursor();

        cursorRemainderX += cursorX * CONTROLLER_CURSOR_SPEED * elapsed;
        cursorRemainderY += cursorY * CONTROLLER_CURSOR_SPEED * elapsed;

        int dx = (int)cursorRemainderX;
        int dy = (int)cursorRemainderY;
        cursorRemainderX -= dx;
        cursorRemainderY -= dy;

        *x += dx;
        *y += dy;
        moved = dx != 0 || dy != 0;
    }

    if (click) {
        *leftButton = 1;
    }

    return click || moved;
}

static void controller_open(int deviceIndex)
{
    controller = SDL_GameControllerOpen(deviceIndex);
    if (controller == nullptr) {
        SDL_Log("Failed to open controller: %s", SDL_GetError());
        return;
    }

    controllerId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    controllerSuppressed = true;
}

static void controller_close()
{
    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        controllerId = -1;
    }

    controllerSuppressed = false;
    for (bool& pending : actionPending) {
        pending = false;
    }
}

// Button presses become game actions or the keyboard shortcuts the game
// already has, so the controller never duplicates game logic.
static void controller_handle_button(SDL_GameControllerButton button)
{
    if (controller_world_is_active()) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            actionPending[CONTROLLER_ACTION_INTERACT] = true;
            break;
        case SDL_CONTROLLER_BUTTON_Y:
            actionPending[CONTROLLER_ACTION_INSPECT] = true;
            break;
        case SDL_CONTROLLER_BUTTON_X:
            input_inject_key(SDL_SCANCODE_I); // Inventory
            break;
        case SDL_CONTROLLER_BUTTON_B:
            input_inject_key(SDL_SCANCODE_B); // Switch active hand
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            input_inject_key(SDL_SCANCODE_S); // Skilldex
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            input_inject_key(SDL_SCANCODE_P); // Pip-Boy
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            input_inject_key(SDL_SCANCODE_C); // Character
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            input_inject_key(SDL_SCANCODE_TAB); // Automap
            break;
        case SDL_CONTROLLER_BUTTON_START:
            input_inject_key(SDL_SCANCODE_ESCAPE); // Options
            break;
        default:
            break;
        }
        return;
    }

    // Screens that run their own loop: B and Start go back.
    if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_START) {
        input_inject_key(SDL_SCANCODE_ESCAPE);
    }
}

static void controller_read_stick(SDL_GameControllerAxis axisXId, SDL_GameControllerAxis axisYId, float* x, float* y)
{
    float axisX = SDL_GameControllerGetAxis(controller, axisXId) / 32767.0f;
    float axisY = SDL_GameControllerGetAxis(controller, axisYId) / 32767.0f;

    float rawMagnitude = sqrtf(axisX * axisX + axisY * axisY);
    if (rawMagnitude < CONTROLLER_DEAD_ZONE) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    float magnitude = rawMagnitude > 1.0f ? 1.0f : rawMagnitude;

    // Rescale so the cursor starts moving slowly at the edge of the dead zone
    // instead of jumping.
    float scale = (magnitude - CONTROLLER_DEAD_ZONE) / (1.0f - CONTROLLER_DEAD_ZONE) / rawMagnitude;
    *x = axisX * scale;
    *y = axisY * scale;
}

} // namespace fallout
