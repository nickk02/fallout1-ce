#ifndef FALLOUT_PLIB_GNW_GAME_COMMANDS_H_
#define FALLOUT_PLIB_GNW_GAME_COMMANDS_H_

#include <stdint.h>

#include <SDL_gamecontroller.h>
#include <functional>

namespace fallout {

// Trigger deflection that counts as a press.
static const int kTriggerThreshold = 16000;

// Game actions that more than one input source can trigger. The values match
// ControllerAction.
enum class Command {
    Interact,
    Attack,
    Inspect,
    NextTarget,
    PreviousTarget,
    Count,
};

enum class CommandSource {
    Keyboard,
    Controller,
    Count,
};

enum class CommandEdge {
    None,
    Pressed,
    Released,
};

enum class CommandContext {
    World,
    Other,
};

// A key event becomes an edge: a press that is not a key repeat is `Pressed`,
// a release is `Released`, and a repeat is `None`.
CommandEdge command_edge_for_key(bool down, bool repeat);

// E interacts, F attacks, Q inspects, [ picks the previous target and ] the next.
bool command_for_scancode(int scancode, Command* out);

// Takes the logical button, after the A and B swap. A interacts and Y inspects.
bool command_for_button(int sdlButton, Command* out);

// `true` for the five command keys and K, only while the world is active. The
// game does not see these keys.
bool command_key_swallowed(int scancode, bool worldActive);

// Keeps pending and held state per source and per command.
class CommandRouter {
public:
    // Changing the context latches every held command and clears pending and
    // held. A latched command stays latched until its source posts `Released`,
    // so a button held across the change does not fire again.
    void setContext(CommandContext context);
    CommandContext context() const;

    // `Pressed` counts only in the World context, when the command is not
    // latched and the source does not already hold it. `Released` counts in
    // every context and keeps pending, so a tap delivers.
    void post(CommandSource source, Command command, CommandEdge edge);

    // Returns `true` once per press, whatever the source, and consumes it.
    bool take(Command command);
    bool pending(Command command) const;

    // Held by any source and not latched.
    bool held(Command command) const;
    bool latched(CommandSource source, Command command) const;

    // Forgets everything one source holds. The other sources keep their state.
    void sourceLost(CommandSource source);

    // Clears everything and sets the World context.
    void reset();

private:
    struct State {
        bool pending;
        bool held;
        bool latched;
    };

    State& state(CommandSource source, Command command);
    const State& state(CommandSource source, Command command) const;

    CommandContext _context = CommandContext::World;
    State _state[static_cast<int>(CommandSource::Count)][static_cast<int>(Command::Count)] = {};
};

// Records when the world loop last ran. The world counts as active for 150 ms
// after a tick.
class WorldHeartbeat {
public:
    static const uint32_t kWindowMs = 150;

    void tick(uint32_t now);
    bool active(uint32_t now) const;

private:
    bool _ticked = false;
    uint32_t _last = 0;
};

// Turns an analog trigger into edges. It tracks the state from every sample,
// in every context, so a trigger that goes down while the world is not active
// is down when the world returns and does not fire. `Released` always reaches
// the router.
class AnalogTrigger {
public:
    void sample(CommandRouter& router, CommandSource source, Command command, bool down);
    bool down() const;
    void reset();

private:
    bool _down = false;
};

// Turns a flick of the right stick to the left or right into a press of
// PreviousTarget or NextTarget. Tracked in every context like AnalogTrigger.
class FlickDetector {
public:
    void sample(CommandRouter& router, float x, float y);
    void reset();

private:
    bool _flicked = false;
    Command _command = Command::NextTarget;
};

// The dead-zone math for a stick. `rawX` and `rawY` are the axis values divided
// by 32767. Inside the dead zone the result is 0, 0. Outside it the length
// starts again at 0, so the cursor does not jump at the edge.
void command_stick_normalize(float rawX, float rawY, float deadZone, float* x, float* y);

// Feeds the right trigger and the right stick to the commands from events, in
// queue order, and from the world tick's polls.
//
// Each event carries its own value, so a press and a release that arrive in
// one pump are both seen. The stick has two axis events per physical sample
// with the same timestamp. They are applied to a cached pair and evaluated
// once, when the next sample (a different timestamp, or the same axis again)
// arrives or at the end of the pump (`flush`). SDL timestamps are in
// milliseconds and can collide, so a repeated axis starts a new group.
class AxisEvents {
public:
    void trigger(CommandRouter& router, int16_t value);
    void stick(CommandRouter& router, bool isX, int16_t value, uint32_t timestamp, float deadZone);
    void flush(CommandRouter& router, float deadZone);
    void pollTrigger(CommandRouter& router, int16_t value);
    void pollStick(CommandRouter& router, int16_t rawX, int16_t rawY, float deadZone);
    void reset();

private:
    void evaluate(CommandRouter& router, float deadZone);

    AnalogTrigger _trigger;
    FlickDetector _flick;
    int16_t _rawX = 0;
    int16_t _rawY = 0;
    bool _groupOpen = false;
    uint32_t _groupTimestamp = 0;
    bool _xSeen = false;
    bool _ySeen = false;
};

// One axis event of the controller. The pump has set the context already. Only
// the right trigger and the right stick count. Every other axis is ignored.
void command_handle_axis_event(CommandRouter& router, AxisEvents& axes, int sdlAxis, int16_t value, uint32_t timestamp, float deadZone);

struct ControllerPhysicalState {
    bool buttons[SDL_CONTROLLER_BUTTON_MAX] = {};
    int16_t axes[SDL_CONTROLLER_AXIS_MAX] = {};
};

struct ControllerRecoveryContext {
    CommandRouter& router;
    AxisEvents& axes;
    bool& leftTriggerDown;
};

// Suppressed input updates physical state only. The edge that observes neutral
// is never dispatched. A later fresh edge in the same queue can be dispatched.
// SDL polling can see the end of the queue, so polling cannot recover while a
// pump is open. Ordered event values may recover during the pump.
class ControllerRecovery {
public:
    void suppress(ControllerRecoveryContext& context, const ControllerPhysicalState& state);
    void focusLost(ControllerRecoveryContext& context, const ControllerPhysicalState& state);
    void focusGained(ControllerRecoveryContext& context, float deadZone);
    void disconnect(ControllerRecoveryContext& context);
    bool suppressed() const { return suppressed_; }
    bool button(ControllerRecoveryContext& context, int button, bool down, float deadZone);
    bool axis(ControllerRecoveryContext& context, int axis, int16_t value, float deadZone);
    void poll(ControllerRecoveryContext& context, const ControllerPhysicalState& state, float deadZone);
    void beginPump() { pumpDepth_++; }
    void endPump(ControllerRecoveryContext& context, const ControllerPhysicalState& state, float deadZone);

private:
    void recoverIfNeutral(ControllerRecoveryContext& context, float deadZone);
    ControllerPhysicalState state_;
    bool suppressed_ = false;
    bool focused_ = true;
    unsigned int pumpDepth_ = 0;
};

// Returns true for World routing. In Other, retains an A tap until menu polling.
// The caller passes logical buttons after the A/B swap.
bool command_controller_button(CommandRouter& router, int button, bool& menuAPending);

// A pump is World only when a world loop asks for it by name. Every other pump
// is Other, including a pump nested inside a World pump.
constexpr CommandContext kDefaultPumpContext = CommandContext::Other;

// Sets the context, then runs the body of the pump and returns its value. The
// previous context is not restored: a nested Other pump leaves the router Other
// for the rest of the outer pump, and the next explicit World entry brings it
// back. A command held across the change is latched and needs a release.
int command_pump(CommandRouter& router, CommandContext context, const std::function<int()>& body);

// The world loop calls this every frame, before it polls the controller or
// takes a command. It stamps the heartbeat for the other heartbeat consumers
// and sets the World context.
void command_enter_world(CommandRouter& router, WorldHeartbeat& heartbeat, uint32_t now);

// Posts the edge for a mapped key. `Released` is posted in every context.
// Returns `true` if the game must not see the event.
bool command_process_key(CommandRouter& router, int scancode, bool down, bool repeat, bool worldActive);

// The router the controller code uses.
CommandRouter& game_commands();

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_GAME_COMMANDS_H_ */
