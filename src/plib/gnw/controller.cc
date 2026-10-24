#include "plib/gnw/controller.h"

#include <math.h>

#include "plib/gnw/input.h"
#include "plib/gnw/menu_focus.h"
#include "plib/gnw/text_pad.h"

namespace fallout {

// Radial dead zone, as a fraction of full deflection.
static float controllerDeadZone = 0.2f;
#define CONTROLLER_DEAD_ZONE controllerDeadZone

// Cursor speed at full right stick deflection, in game pixels per second.
static float controllerCursorSpeed = 600.0f;
#define CONTROLLER_CURSOR_SPEED controllerCursorSpeed
static bool controllerSwapAB = false;

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
static bool textPadPolling = false;
static bool listMode = false;
static bool inventoryKeys = false;
static bool rightStickFlicked = false;
static bool listPolling = false;
static float cursorRemainderX = 0.0f;
static float cursorRemainderY = 0.0f;

static void controller_open(int deviceIndex);
static void controller_close();
static void controller_handle_button(SDL_GameControllerButton button);
static void controller_read_stick(SDL_GameControllerAxis axisX, SDL_GameControllerAxis axisY, float* x, float* y);
static void controller_text_pad_poll(Uint32 now);
static bool controller_is_neutral();
static void controller_list_poll(Uint32 now);

// Reads a button, with A and B swapped when the player asked for that.
static bool controller_button_down(SDL_GameControllerButton button)
{
    if (controllerSwapAB) {
        if (button == SDL_CONTROLLER_BUTTON_A) {
            button = SDL_CONTROLLER_BUTTON_B;
        } else if (button == SDL_CONTROLLER_BUTTON_B) {
            button = SDL_CONTROLLER_BUTTON_A;
        }
    }

    return SDL_GameControllerGetButton(controller, button) != 0;
}

void controller_set_options(int deadZonePercent, int cursorSpeed, bool swapAB)
{
    controllerDeadZone = (deadZonePercent < 5 ? 5 : (deadZonePercent > 60 ? 60 : deadZonePercent)) / 100.0f;
    controllerCursorSpeed = (float)(cursorSpeed < 100 ? 100 : (cursorSpeed > 2000 ? 2000 : cursorSpeed));
    controllerSwapAB = swapAB;
}

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

    if (controller == nullptr) {
        return;
    }

    // After the window lost focus, wait for every button, trigger and stick to
    // rest before the controller drives the character again. The menus do the
    // same check, but they do not run while the world loop does.
    if (controllerSuppressed) {
        if (!controller_is_neutral()) {
            return;
        }

        controllerSuppressed = false;
        rightTriggerDown = false;
        leftTriggerDown = false;
    }

    bool rightTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > CONTROLLER_TRIGGER_THRESHOLD;
    if (rightTrigger && !rightTriggerDown) {
        actionPending[CONTROLLER_ACTION_ATTACK] = true;
    }
    rightTriggerDown = rightTrigger;

    // A flick of the right stick left or right picks the previous or next
    // target.
    float flickX;
    float flickY;
    controller_read_stick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, &flickX, &flickY);
    if (!rightStickFlicked && fabsf(flickX) > 0.6f && fabsf(flickX) > fabsf(flickY)) {
        rightStickFlicked = true;
        actionPending[flickX > 0.0f ? CONTROLLER_ACTION_NEXT_TARGET : CONTROLLER_ACTION_PREVIOUS_TARGET] = true;
    } else if (fabsf(flickX) < 0.3f) {
        rightStickFlicked = false;
    }

    // The left trigger pauses combat. The V key does nothing elsewhere.
    bool leftTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > CONTROLLER_TRIGGER_THRESHOLD;
    if (leftTrigger && !leftTriggerDown) {
        input_inject_key(SDL_SCANCODE_V);
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
    case SDL_SCANCODE_LEFTBRACKET:
        action = CONTROLLER_ACTION_PREVIOUS_TARGET;
        break;
    case SDL_SCANCODE_RIGHTBRACKET:
        action = CONTROLLER_ACTION_NEXT_TARGET;
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

bool controller_a_is_down()
{
    return controller != nullptr && !controllerSuppressed && controller_button_down(SDL_CONTROLLER_BUTTON_A) != 0;
}

void controller_set_inventory_keys(bool enabled)
{
    inventoryKeys = enabled;
}

void controller_set_list_mode(bool enabled)
{
    listMode = enabled;
    listPolling = false;
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

    if (text_pad_is_active()) {
        controller_text_pad_poll(now);
        return false;
    }
    textPadPolling = false;

    if (listMode) {
        if (menu_focus_is_active()) {
            menu_focus_clear();
        }

        controller_list_poll(now);
        return false;
    }
    listPolling = false;

    bool a = controller_button_down(SDL_CONTROLLER_BUTTON_A) != 0;

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

    // In the inventory Y is the right mouse button, which switches between
    // grabbing items and the item menu.
    bool rightClick = false;
    static bool yWasDown = false;
    bool yDown = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y) != 0;
    if (inventoryKeys && yDown && !yWasDown) {
        *rightButton = 1;
        rightClick = true;
    }
    yWasDown = yDown;

    return click || moved || rightClick;
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
    if (controllerSwapAB) {
        if (button == SDL_CONTROLLER_BUTTON_A) {
            button = SDL_CONTROLLER_BUTTON_B;
        } else if (button == SDL_CONTROLLER_BUTTON_B) {
            button = SDL_CONTROLLER_BUTTON_A;
        }
    }

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
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            input_inject_key(SDL_SCANCODE_1); // Item 1
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            input_inject_key(SDL_SCANCODE_2); // Item 2
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            input_inject_key(SDL_SCANCODE_R); // Reload
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            input_inject_key(SDL_SCANCODE_M); // Next attack mode
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
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

    // The character picker takes the face buttons while a text field is open.
    if (text_pad_is_active()) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_B:
            input_inject_key(SDL_SCANCODE_BACKSPACE);
            break;
        case SDL_CONTROLLER_BUTTON_X:
            input_inject_key(SDL_SCANCODE_SPACE);
            break;
        case SDL_CONTROLLER_BUTTON_Y:
            text_pad_toggle_case();
            break;
        case SDL_CONTROLLER_BUTTON_START:
            input_inject_key(SDL_SCANCODE_RETURN);
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            input_inject_key(SDL_SCANCODE_ESCAPE);
            break;
        default:
            break;
        }
        return;
    }

    if (inventoryKeys) {
        if (button == SDL_CONTROLLER_BUTTON_X) {
            input_inject_key(SDL_SCANCODE_E);
            return;
        }

    }

    // Screens that run their own loop: B and Start go back.
    if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_START) {
        input_inject_key(SDL_SCANCODE_ESCAPE);
    }
}

static bool controller_is_neutral()
{
    for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
        if (SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)button)) {
            return false;
        }
    }

    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > CONTROLLER_TRIGGER_THRESHOLD
        || SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > CONTROLLER_TRIGGER_THRESHOLD) {
        return false;
    }

    float x;
    float y;
    controller_read_stick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, &x, &y);
    if (x != 0.0f || y != 0.0f) {
        return false;
    }

    controller_read_stick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, &x, &y);
    return x == 0.0f && y == 0.0f;
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

// The d-pad or left stick moves over the character picker and A types.
static void controller_text_pad_poll(Uint32 now)
{
    bool a = controller_button_down(SDL_CONTROLLER_BUTTON_A) != 0;

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
        if (a || dirX != 0 || dirY != 0) {
            return;
        }

        controllerSuppressed = false;
    }

    static int lastDirX = 0;
    static int lastDirY = 0;
    static Uint32 nextMoveTicks = 0;
    if (dirX != 0 || dirY != 0) {
        input_note_controller();

        bool changed = dirX != lastDirX || dirY != lastDirY;
        if (changed || now >= nextMoveTicks) {
            text_pad_move(dirX, dirY);
            nextMoveTicks = now + (changed ? 300 : 120);
        }
    }
    lastDirX = dirX;
    lastDirY = dirY;

    // A is still down from opening the text field: wait for the release.
    static bool aWasDown = false;
    if (!textPadPolling) {
        textPadPolling = true;
        aWasDown = a;
    }

    if (a && !aWasDown) {
        input_note_controller();
        text_pad_press();
    }
    aWasDown = a;
}

// The d-pad or left stick steps through a list and A confirms.
static void controller_list_poll(Uint32 now)
{
    bool a = controller_button_down(SDL_CONTROLLER_BUTTON_A) != 0;

    int dirY = 0;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) dirY = -1;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) dirY = 1;

    float stickX;
    float stickY;
    controller_read_stick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, &stickX, &stickY);
    if (dirY == 0 && fabsf(stickY) > 0.5f && fabsf(stickY) > fabsf(stickX)) {
        dirY = stickY < 0.0f ? -1 : 1;
    }

    if (controllerSuppressed) {
        if (a || dirY != 0) {
            return;
        }

        controllerSuppressed = false;
    }

    static int lastDirY = 0;
    static Uint32 nextMoveTicks = 0;
    if (!listPolling) {
        lastDirY = dirY;
    }

    if (dirY != 0) {
        input_note_controller();

        bool changed = dirY != lastDirY;
        if (changed || now >= nextMoveTicks) {
            input_inject_key(dirY < 0 ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN);
            nextMoveTicks = now + (changed ? 300 : 110);
        }
    }
    lastDirY = dirY;

    // A is still down from opening the screen: wait for the release.
    static bool aWasDown = false;
    if (!listPolling) {
        listPolling = true;
        aWasDown = a;
    }

    if (a && !aWasDown) {
        input_note_controller();
        input_inject_key(SDL_SCANCODE_RETURN);
    }
    aWasDown = a;
}

} // namespace fallout
