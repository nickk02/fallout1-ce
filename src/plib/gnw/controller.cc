#include "plib/gnw/controller.h"

#include <math.h>

namespace fallout {

// Cursor speed at full stick deflection, in mouse units per second.
#define CONTROLLER_CURSOR_SPEED 600.0f

// Radial dead zone, as a fraction of full deflection.
#define CONTROLLER_DEAD_ZONE 0.2f

static SDL_GameController* controller = nullptr;
static SDL_JoystickID controllerId = -1;
static bool controllerSuppressed = false;
static Uint32 lastPollTicks = 0;
static float remainderX = 0.0f;
static float remainderY = 0.0f;

static void controller_open(int deviceIndex);
static void controller_close();
static void controller_read_stick(float* x, float* y);

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
    }
}

void controller_focus_lost()
{
    controllerSuppressed = true;
}

bool controller_get_mouse_state(int* x, int* y, unsigned char* leftButton, unsigned char* rightButton)
{
    Uint32 now = SDL_GetTicks();
    float elapsed = (now - lastPollTicks) / 1000.0f;
    lastPollTicks = now;

    if (controller == nullptr) {
        return false;
    }

    bool a = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0;
    bool b = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B) != 0;

    float stickX;
    float stickY;
    controller_read_stick(&stickX, &stickY);

    if (controllerSuppressed) {
        // Wait for a neutral state so a button held across a focus change is
        // not reported as a fresh press.
        if (a || b || stickX != 0.0f || stickY != 0.0f) {
            return false;
        }

        controllerSuppressed = false;
    }

    // The first poll after a long gap would otherwise fling the cursor.
    if (elapsed > 0.1f) {
        elapsed = 0.1f;
    }

    remainderX += stickX * CONTROLLER_CURSOR_SPEED * elapsed;
    remainderY += stickY * CONTROLLER_CURSOR_SPEED * elapsed;

    int dx = (int)remainderX;
    int dy = (int)remainderY;
    remainderX -= dx;
    remainderY -= dy;

    *x += dx;
    *y += dy;

    if (a) {
        *leftButton = 1;
    }

    if (b) {
        *rightButton = 1;
    }

    return a || b || dx != 0 || dy != 0;
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
    remainderX = 0.0f;
    remainderY = 0.0f;
}

static void controller_close()
{
    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        controllerId = -1;
    }

    controllerSuppressed = false;
    remainderX = 0.0f;
    remainderY = 0.0f;
}

static void controller_read_stick(float* x, float* y)
{
    float axisX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    float axisY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;

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
