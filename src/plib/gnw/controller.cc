#include "plib/gnw/controller.h"

#include <math.h>

#include "plib/gnw/game_commands.h"
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

static SDL_GameController* controller = nullptr;
static SDL_JoystickID controllerId = -1;
static ControllerRecovery controllerRecovery;
static bool menuAPending = false;
static WorldHeartbeat worldHeartbeat;
static AxisEvents axisEvents;
static bool leftTriggerDown = false;
static Uint32 lastCursorTicks = 0;
static bool textPadPolling = false;
static bool listMode = false;
static bool inventoryKeys = false;
static bool listPolling = false;

static ControllerRecoveryContext recovery_context()
{
    return { game_commands(), axisEvents, leftTriggerDown };
}

static ControllerPhysicalState controller_physical_state()
{
    ControllerPhysicalState state;
    if (controller != nullptr) {
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
            state.buttons[i] = SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(i)) != 0;
        for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
            state.axes[i] = SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(i));
    }
    return state;
}

static void controller_recover_poll()
{
    auto context = recovery_context();
    controllerRecovery.poll(context, controller_physical_state(), controllerDeadZone);
}

void controller_begin_pump()
{
    controllerRecovery.beginPump();
}

void controller_end_pump()
{
    auto context = recovery_context();
    controllerRecovery.endPump(context, controller_physical_state(), controllerDeadZone);
}

void controller_focus_gained()
{
    auto context = recovery_context();
    controllerRecovery.focusGained(context, controllerDeadZone);
}

static float cursorRemainderX = 0.0f;
static float cursorRemainderY = 0.0f;

static void controller_open(int deviceIndex);
static void controller_close();
static void controller_handle_button(SDL_GameControllerButton button);
static void controller_handle_button_up(SDL_GameControllerButton button);
static void controller_read_stick(SDL_GameControllerAxis axisX, SDL_GameControllerAxis axisY, float* x, float* y);
static void controller_text_pad_poll(Uint32 now);
static bool controller_is_neutral();
static void controller_list_poll(Uint32 now);

static_assert(static_cast<int>(Command::Interact) == CONTROLLER_ACTION_INTERACT
        && static_cast<int>(Command::Attack) == CONTROLLER_ACTION_ATTACK
        && static_cast<int>(Command::Inspect) == CONTROLLER_ACTION_INSPECT
        && static_cast<int>(Command::NextTarget) == CONTROLLER_ACTION_NEXT_TARGET
        && static_cast<int>(Command::PreviousTarget) == CONTROLLER_ACTION_PREVIOUS_TARGET
        && static_cast<int>(Command::Count) == CONTROLLER_ACTION_COUNT,
    "ControllerAction and Command must have the same values");

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
        if (controller != nullptr && event->cbutton.which == controllerId) {
            auto context = recovery_context();
            if (!controllerRecovery.button(context, event->cbutton.button, true, controllerDeadZone)) break;
            input_note_controller();
            controller_handle_button((SDL_GameControllerButton)event->cbutton.button);
        }
        break;
    case SDL_CONTROLLERAXISMOTION:
        // The right trigger and the right stick are tracked from events too, in
        // queue order, so a release that happens while the world is not active
        // is seen at once and a quick press and release is not lost. Every other
        // axis costs nothing.
        if (controller != nullptr && event->caxis.which == controllerId) {
            auto context = recovery_context();
            if (!controllerRecovery.axis(context, event->caxis.axis, event->caxis.value, controllerDeadZone)) break;
            command_handle_axis_event(game_commands(), axisEvents, event->caxis.axis, event->caxis.value, event->caxis.timestamp, controllerDeadZone);
        }
        break;
    case SDL_CONTROLLERBUTTONUP:
        // Releases count whatever the suppression or the context.
        if (controller != nullptr && event->cbutton.which == controllerId) {
            auto context = recovery_context();
            bool dispatch = controllerRecovery.button(context, event->cbutton.button, false, controllerDeadZone);
            if (dispatch) controller_handle_button_up((SDL_GameControllerButton)event->cbutton.button);
        }
        break;
    }
}

void controller_flush_axis_events()
{
    if (controller == nullptr || controllerRecovery.suppressed()) {
        return;
    }

    axisEvents.flush(game_commands(), controllerDeadZone);
}

void controller_focus_lost()
{
    auto context = recovery_context();
    controllerRecovery.focusLost(context, controller_physical_state());
    menuAPending = false;
    game_commands().sourceLost(CommandSource::Keyboard);
}

void controller_world_tick()
{
    menuAPending = false;
    command_enter_world(game_commands(), worldHeartbeat, SDL_GetTicks());

    if (controller == nullptr) {
        return;
    }

    controller_recover_poll();
    if (controllerRecovery.suppressed()) return;

    axisEvents.pollTrigger(game_commands(), SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    // A flick of the right stick left or right picks the previous or next
    // target.
    axisEvents.pollStick(game_commands(), SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX), SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY), controllerDeadZone);

    // The left trigger pauses combat. The V key does nothing elsewhere.
    bool leftTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > kTriggerThreshold;
    if (leftTrigger && !leftTriggerDown) {
        input_inject_key(SDL_SCANCODE_V);
    }
    leftTriggerDown = leftTrigger;
}

bool controller_world_is_active()
{
    return worldHeartbeat.active(SDL_GetTicks());
}

bool controller_get_move_vector(float* x, float* y)
{
    if (controller == nullptr || controllerRecovery.suppressed() || !controller_world_is_active()) {
        return false;
    }

    controller_read_stick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, x, y);
    if (*x != 0.0f || *y != 0.0f) {
        input_note_controller();
        return true;
    }

    return false;
}

bool controller_handle_key(int scancode, bool down, bool repeat)
{
    bool worldActive = game_commands().context() == CommandContext::World;
    bool swallowed = command_process_key(game_commands(), scancode, down, repeat, worldActive);

    if (scancode == SDL_SCANCODE_K && down && !repeat && worldActive) {
        input_inject_key(SDL_SCANCODE_S); // Skilldex
    }

    return swallowed;
}

bool controller_a_is_down()
{
    return controller != nullptr && !controllerRecovery.suppressed() && controller_button_down(SDL_CONTROLLER_BUTTON_A) != 0;
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
    return game_commands().take(static_cast<Command>(action));
}

bool controller_get_mouse_state(int* x, int* y, unsigned char* leftButton, unsigned char* rightButton)
{
    controller_recover_poll();
    // In an explicit world context there is no controller cursor.
    if (controller == nullptr || game_commands().context() == CommandContext::World) {
        menuAPending = false;
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

    bool a = controller_button_down(SDL_CONTROLLER_BUTTON_A) != 0 || menuAPending;
    menuAPending = false;

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

    if (controllerRecovery.suppressed()) return false;

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
    auto context = recovery_context();
    controllerRecovery.suppress(context, controller_physical_state());
}

static void controller_close()
{
    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        controllerId = -1;
    }

    auto context = recovery_context();
    controllerRecovery.disconnect(context);
    menuAPending = false;
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

    bool worldButton = command_controller_button(game_commands(), button, menuAPending);
    if (worldButton) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_Y: {
            break;
        }
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
            input_inject_key(SDL_SCANCODE_N); // Next attack mode
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

static void controller_handle_button_up(SDL_GameControllerButton button)
{
    if (controllerSwapAB) {
        if (button == SDL_CONTROLLER_BUTTON_A) {
            button = SDL_CONTROLLER_BUTTON_B;
        } else if (button == SDL_CONTROLLER_BUTTON_B) {
            button = SDL_CONTROLLER_BUTTON_A;
        }
    }

    Command command;
    if (command_for_button(button, &command)) {
        game_commands().post(CommandSource::Controller, command, CommandEdge::Released);
    }
}

static bool controller_is_neutral()
{
    for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
        if (SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)button)) {
            return false;
        }
    }

    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > kTriggerThreshold
        || SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerThreshold) {
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

    // Rescaled so the cursor starts moving slowly at the edge of the dead zone
    // instead of jumping.
    command_stick_normalize(axisX, axisY, CONTROLLER_DEAD_ZONE, x, y);
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

    if (controllerRecovery.suppressed()) return;

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

    if (menuAPending || (a && !aWasDown)) {
        input_note_controller();
        text_pad_press();
    }
    menuAPending = false;
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

    if (controllerRecovery.suppressed()) return;

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

    if (menuAPending || (a && !aWasDown)) {
        input_note_controller();
        input_inject_key(SDL_SCANCODE_RETURN);
    }
    menuAPending = false;
    aWasDown = a;
}

} // namespace fallout
