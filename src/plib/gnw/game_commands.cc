#include "plib/gnw/game_commands.h"

#include <SDL_gamecontroller.h>
#include <SDL_scancode.h>

#include <math.h>

namespace fallout {

CommandEdge command_edge_for_key(bool down, bool repeat)
{
    if (!down) {
        return CommandEdge::Released;
    }

    return repeat ? CommandEdge::None : CommandEdge::Pressed;
}

bool command_for_scancode(int scancode, Command* out)
{
    switch (scancode) {
    case SDL_SCANCODE_E:
        *out = Command::Interact;
        return true;
    case SDL_SCANCODE_F:
        *out = Command::Attack;
        return true;
    case SDL_SCANCODE_Q:
        *out = Command::Inspect;
        return true;
    case SDL_SCANCODE_LEFTBRACKET:
        *out = Command::PreviousTarget;
        return true;
    case SDL_SCANCODE_RIGHTBRACKET:
        *out = Command::NextTarget;
        return true;
    default:
        return false;
    }
}

bool command_for_button(int sdlButton, Command* out)
{
    switch (sdlButton) {
    case SDL_CONTROLLER_BUTTON_A:
        *out = Command::Interact;
        return true;
    case SDL_CONTROLLER_BUTTON_Y:
        *out = Command::Inspect;
        return true;
    default:
        return false;
    }
}

bool command_key_swallowed(int scancode, bool worldActive)
{
    if (!worldActive) {
        return false;
    }

    Command command;
    return scancode == SDL_SCANCODE_K || command_for_scancode(scancode, &command);
}

CommandRouter::State& CommandRouter::state(CommandSource source, Command command)
{
    return _state[static_cast<int>(source)][static_cast<int>(command)];
}

const CommandRouter::State& CommandRouter::state(CommandSource source, Command command) const
{
    return _state[static_cast<int>(source)][static_cast<int>(command)];
}

void CommandRouter::setContext(CommandContext context)
{
    if (context == _context) {
        return;
    }

    for (auto& row : _state) {
        for (State& entry : row) {
            if (entry.held) {
                entry.latched = true;
            }
            entry.pending = false;
            entry.held = false;
        }
    }

    _context = context;
}

CommandContext CommandRouter::context() const
{
    return _context;
}

void CommandRouter::post(CommandSource source, Command command, CommandEdge edge)
{
    State& entry = state(source, command);

    switch (edge) {
    case CommandEdge::Pressed:
        if (_context != CommandContext::World || entry.latched || entry.held) {
            return;
        }
        entry.held = true;
        entry.pending = true;
        break;
    case CommandEdge::Released:
        entry.held = false;
        entry.latched = false;
        break;
    case CommandEdge::None:
        break;
    }
}

bool CommandRouter::take(Command command)
{
    bool any = false;
    for (int source = 0; source < static_cast<int>(CommandSource::Count); source++) {
        State& entry = _state[source][static_cast<int>(command)];
        any = any || entry.pending;
        entry.pending = false;
    }
    return any;
}

bool CommandRouter::pending(Command command) const
{
    for (int source = 0; source < static_cast<int>(CommandSource::Count); source++) {
        if (_state[source][static_cast<int>(command)].pending) {
            return true;
        }
    }
    return false;
}

bool CommandRouter::held(Command command) const
{
    for (int source = 0; source < static_cast<int>(CommandSource::Count); source++) {
        const State& entry = _state[source][static_cast<int>(command)];
        if (entry.held && !entry.latched) {
            return true;
        }
    }
    return false;
}

bool CommandRouter::latched(CommandSource source, Command command) const
{
    return state(source, command).latched;
}

void CommandRouter::sourceLost(CommandSource source)
{
    for (State& entry : _state[static_cast<int>(source)]) {
        entry = State();
    }
}

void CommandRouter::reset()
{
    for (auto& row : _state) {
        for (State& entry : row) {
            entry = State();
        }
    }
    _context = CommandContext::World;
}

void WorldHeartbeat::tick(uint32_t now)
{
    _ticked = true;
    _last = now;
}

bool WorldHeartbeat::active(uint32_t now) const
{
    return _ticked && now - _last < kWindowMs;
}

void AnalogTrigger::sample(CommandRouter& router, CommandSource source, Command command, bool down)
{
    if (down && !_down) {
        _down = true;
        router.post(source, command, CommandEdge::Pressed);
    } else if (!down && _down) {
        _down = false;
        router.post(source, command, CommandEdge::Released);
    }
}

bool AnalogTrigger::down() const
{
    return _down;
}

void AnalogTrigger::reset()
{
    _down = false;
}

void FlickDetector::sample(CommandRouter& router, float x, float y)
{
    if (!_flicked && fabsf(x) > 0.6f && fabsf(x) > fabsf(y)) {
        _flicked = true;
        _command = x > 0.0f ? Command::NextTarget : Command::PreviousTarget;
        router.post(CommandSource::Controller, _command, CommandEdge::Pressed);
    } else if (fabsf(x) < 0.3f) {
        if (_flicked) {
            router.post(CommandSource::Controller, _command, CommandEdge::Released);
        }
        _flicked = false;
    }
}

void FlickDetector::reset()
{
    _flicked = false;
}

void command_stick_normalize(float rawX, float rawY, float deadZone, float* x, float* y)
{
    float rawMagnitude = sqrtf(rawX * rawX + rawY * rawY);
    if (rawMagnitude < deadZone) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    float magnitude = rawMagnitude > 1.0f ? 1.0f : rawMagnitude;
    float scale = (magnitude - deadZone) / (1.0f - deadZone) / rawMagnitude;
    *x = rawX * scale;
    *y = rawY * scale;
}

void AxisEvents::trigger(CommandRouter& router, int16_t value)
{
    _trigger.sample(router, CommandSource::Controller, Command::Attack, value > kTriggerThreshold);
}

void AxisEvents::evaluate(CommandRouter& router, float deadZone)
{
    float x;
    float y;
    command_stick_normalize(_rawX / 32767.0f, _rawY / 32767.0f, deadZone, &x, &y);
    _flick.sample(router, x, y);
    _groupOpen = false;
    _xSeen = false;
    _ySeen = false;
}

void AxisEvents::stick(CommandRouter& router, bool isX, int16_t value, uint32_t timestamp, float deadZone)
{
    bool repeated = _groupOpen && timestamp == _groupTimestamp && (isX ? _xSeen : _ySeen);
    if (_groupOpen && (timestamp != _groupTimestamp || repeated)) {
        evaluate(router, deadZone);
    }

    if (isX) {
        _rawX = value;
        _xSeen = true;
    } else {
        _rawY = value;
        _ySeen = true;
    }

    _groupOpen = true;
    _groupTimestamp = timestamp;
}

void AxisEvents::flush(CommandRouter& router, float deadZone)
{
    if (_groupOpen) {
        evaluate(router, deadZone);
    }
}

void AxisEvents::pollTrigger(CommandRouter& router, int16_t value)
{
    trigger(router, value);
}

void AxisEvents::pollStick(CommandRouter& router, int16_t rawX, int16_t rawY, float deadZone)
{
    flush(router, deadZone);
    _rawX = rawX;
    _rawY = rawY;
    evaluate(router, deadZone);
}

void AxisEvents::reset()
{
    _trigger.reset();
    _flick.reset();
    _rawX = 0;
    _rawY = 0;
    _groupOpen = false;
    _groupTimestamp = 0;
    _xSeen = false;
    _ySeen = false;
}

void command_handle_axis_event(CommandRouter& router, AxisEvents& axes, int sdlAxis, int16_t value, uint32_t timestamp, float deadZone)
{
    switch (sdlAxis) {
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        axes.trigger(router, value);
        break;
    case SDL_CONTROLLER_AXIS_RIGHTX:
        axes.stick(router, true, value, timestamp, deadZone);
        break;
    case SDL_CONTROLLER_AXIS_RIGHTY:
        axes.stick(router, false, value, timestamp, deadZone);
        break;
    default:
        break;
    }
}

namespace {
    void reset_controller_commands(ControllerRecoveryContext& context)
    {
        context.router.sourceLost(CommandSource::Controller);
        context.axes.reset();
        context.leftTriggerDown = false;
    }
}

void ControllerRecovery::suppress(ControllerRecoveryContext& context, const ControllerPhysicalState& state)
{
    state_ = state;
    suppressed_ = true;
    reset_controller_commands(context);
}

void ControllerRecovery::focusLost(ControllerRecoveryContext& context, const ControllerPhysicalState& state)
{
    focused_ = false;
    suppress(context, state);
}

void ControllerRecovery::focusGained(ControllerRecoveryContext& context, float deadZone)
{
    focused_ = true;
    recoverIfNeutral(context, deadZone);
}

void ControllerRecovery::disconnect(ControllerRecoveryContext& context)
{
    state_ = ControllerPhysicalState();
    suppressed_ = false;
    reset_controller_commands(context);
}

void ControllerRecovery::recoverIfNeutral(ControllerRecoveryContext& context, float deadZone)
{
    if (!suppressed_ || !focused_) return;
    for (bool down : state_.buttons) {
        if (down) return;
    }
    if (state_.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > kTriggerThreshold
        || state_.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > kTriggerThreshold) return;
    const int sticks[] = { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_RIGHTX };
    for (int xAxis : sticks) {
        float x;
        float y;
        command_stick_normalize(state_.axes[xAxis] / 32767.0f,
            state_.axes[xAxis + 1] / 32767.0f, deadZone, &x, &y);
        if (x != 0.0f || y != 0.0f) return;
    }
    suppressed_ = false;
    reset_controller_commands(context);
}

bool ControllerRecovery::button(ControllerRecoveryContext& context, int button, bool down, float deadZone)
{
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) return false;
    bool dispatch = !suppressed_ && focused_;
    state_.buttons[button] = down;
    recoverIfNeutral(context, deadZone);
    return dispatch;
}

bool ControllerRecovery::axis(ControllerRecoveryContext& context, int axis, int16_t value, float deadZone)
{
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) return false;
    bool dispatch = !suppressed_ && focused_;
    state_.axes[axis] = value;
    recoverIfNeutral(context, deadZone);
    return dispatch;
}

void ControllerRecovery::poll(ControllerRecoveryContext& context, const ControllerPhysicalState& state, float deadZone)
{
    if (pumpDepth_ != 0 || !suppressed_) return;
    state_ = state;
    recoverIfNeutral(context, deadZone);
}

void ControllerRecovery::endPump(ControllerRecoveryContext& context, const ControllerPhysicalState& state, float deadZone)
{
    if (pumpDepth_ == 0) return;
    pumpDepth_--;
    poll(context, state, deadZone);
}

bool command_controller_button(CommandRouter& router, int button, bool& menuAPending)
{
    if (router.context() != CommandContext::World) {
        if (button == SDL_CONTROLLER_BUTTON_A) menuAPending = true;
        return false;
    }
    Command command;
    if (command_for_button(button, &command)) {
        router.post(CommandSource::Controller, command, CommandEdge::Pressed);
    }
    return true;
}

int command_pump(CommandRouter& router, CommandContext context, const std::function<int()>& body)
{
    router.setContext(context);
    return body();
}

void command_enter_world(CommandRouter& router, WorldHeartbeat& heartbeat, uint32_t now)
{
    heartbeat.tick(now);
    router.setContext(CommandContext::World);
}

bool command_process_key(CommandRouter& router, int scancode, bool down, bool repeat, bool worldActive)
{
    Command command;
    if (command_for_scancode(scancode, &command)) {
        router.post(CommandSource::Keyboard, command, command_edge_for_key(down, repeat));
    }

    return command_key_swallowed(scancode, worldActive);
}

CommandRouter& game_commands()
{
    static CommandRouter router;
    return router;
}

} // namespace fallout
