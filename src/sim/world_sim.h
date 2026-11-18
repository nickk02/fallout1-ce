#ifndef FALLOUT_SIM_WORLD_SIM_H_
#define FALLOUT_SIM_WORLD_SIM_H_

#include <stddef.h>
#include <stdint.h>

#include "sim/pause_set.h"

namespace fallout {

// The one simulation clock and pause set of the game, with the engine reduced
// to a table of functions so the wiring can be tested without the engine.
//
// Modes (WORLD_SIM in f1_res.ini):
//   0  Existing scheduler. Nothing is registered and no pause is tracked.
//   1  Shadow. The clock counts ticks and the pause set tracks owners, but no
//      game state is touched.
//   2  Owned pauses. The pause set is the only owner of the world freeze and of
//      the timer shift that follows a pause.
enum WorldSimMode {
    WORLD_SIM_EXISTING = 0,
    WORLD_SIM_SHADOW = 1,
    WORLD_SIM_OWNED = 2,
};

typedef void WorldSimPumpFunction();
typedef void WorldSimFocusFunction(int gained);

struct WorldSimPlatform {
    // Free-running millisecond counter. May wrap at 2^32.
    unsigned int (*now)();

    // Runs the function from the engine's background processing.
    void (*addPump)(WorldSimPumpFunction* function);
    void (*removePump)(WorldSimPumpFunction* function);

    // The window focus callback: called with 0 when focus is lost and with 1
    // when it returns.
    WorldSimFocusFunction* (*getFocus)();
    void (*setFocus)(WorldSimFocusFunction* function);

    // Mode 2 only. Called when the first pause is taken (true) and when the last
    // one is released (false), never for nested ones. The engine freezes and
    // thaws everything that runs on its own while the world is paused.
    void (*setWorldPaused)(bool paused);

    // Mode 2 only. Called once after the last pause is released, with the time
    // the whole pause lasted and the current time from now(), so wall-clock
    // timers can be moved forward or restarted. A timer restarted from any other
    // clock reading, such as a value saved by the last background pass, would
    // be stale by the length of the pause.
    void (*shiftTimers)(unsigned int elapsed, unsigned int now);

    // Mode 2 only, optional. Called by world_sim_reset() while a pause is held,
    // after the start of the pause moved to now. Engine state that keeps its own
    // pause start moves it too.
    void (*pauseOriginReset)();

    // Optional.
    void (*log)(const char* text);

    // Optional, mode 2 only. Runs once per actual 60 Hz simulation tick.
    // May pause or change lifecycle. That ends the current batch, including a
    // pause released inside the callback. Nested pumps do nothing. An epoch or
    // service change abandons the remaining elapsed time and takes a fresh
    // clock baseline on the next pump. No movement/input sampling is implied.
    void (*gameplayTick)();

    // Optional. Receives one diagnostic line (no newline) for a pause, a
    // lifecycle change, or every 600 ticks. Null means no diagnostics. It is
    // called while the service is in the middle of an operation: it may only
    // format and write the line. It must not call any world_sim function and
    // must not run gameplay. While it runs, world_sim_init() returns false,
    // world_sim_exit(), world_sim_reset() and world_sim_invalidate() do nothing,
    // and world_sim_pause() returns an empty token.
    void (*event)(const char* line);

    // Optional. Writes a suffix for the periodic summary line, starting with a
    // space, such as " game_time=12". It only reads game state. The same rule as
    // for `event` applies: it must not call any world_sim function or run
    // gameplay, and the same calls are refused while it runs.
    void (*describe)(char* out, size_t size);
};

// Returns false, and changes nothing, if the service is already started, the
// mode is unknown, or a function the mode needs is missing. Mode 0 starts
// nothing.
bool world_sim_init(int mode, const WorldSimPlatform& platform);

// Undoes world_sim_init(): removes the pump, puts the previous focus callback
// back (unless something else replaced ours since), and drops the pause set.
// Tokens still held by callers stay valid and do nothing. Safe to call when not
// started, and to call twice.
void world_sim_exit();

int world_sim_mode();

// Drain due ticks with the current intent before publishing a new input sample.
// Returns false for nested/diagnostic calls, lifecycle changes, or inactivity; callers
// must not publish fresh intent then. Uses the same pinned background pump.
bool world_sim_drain();

// `true` in mode 2. The legacy pause code runs when this is false.
bool world_sim_owns_pauses();

// `true` in mode 2 while any pause is held. Legacy consumers of wall-clock time
// skip their work while this is true.
bool world_sim_world_paused();

// A claim on the pause. Empty in mode 0.
PauseToken world_sim_pause();

// Called when the game state is reset (new game, load). The clock forgets its
// last time. Pauses held by callers are kept, and the start of a pause in
// progress moves to now, so timers made after the reset are not shifted by the
// time before it.
void world_sim_reset();

// The lifecycle generation. It counts up for the whole life of the process and
// never goes back, so a value read earlier equals the current one only if
// nothing below happened in between. It changes when:
//   - world_sim_init() succeeds, in every mode, mode 0 included;
//   - world_sim_reset() runs while the service is started (also in mode 0);
//   - world_sim_exit() ends a started service.
// A refused init, a reset before init, and an exit when not started change
// nothing. With no service started the value stays where it was; it is not
// reset to zero. A consumer stores the value with its own state and drops that
// state when the value no longer matches.
//
// An epoch change is not a pause. It tells consumers that earlier marks are
// stale. Reset and in-pump invalidation also rebase the clock as documented.
uint64_t world_sim_epoch();

// Marks that the map the world was built on is being replaced, without a game
// reset: changes the epoch while the service is started and does nothing
// otherwise. It leaves the tick count, pause set and pause start unchanged.
// During a pump it also abandons that batch and resets the clock baseline;
// outside a pump it leaves the clock unchanged. A consumer that keeps a mark or a
// pass in flight across the replacement sees the epoch change and drops it.
void world_sim_invalidate();

// Counts a tick of the legacy calendar, for the shadow comparison.
void world_sim_note_legacy_calendar_tick();

uint64_t world_sim_tick_count();
uint64_t world_sim_legacy_calendar_ticks();

} // namespace fallout

#endif /* FALLOUT_SIM_WORLD_SIM_H_ */
