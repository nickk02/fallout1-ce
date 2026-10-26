#ifndef FALLOUT_SIM_WORLD_SIM_H_
#define FALLOUT_SIM_WORLD_SIM_H_

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

// Counts a tick of the legacy calendar, for the shadow comparison.
void world_sim_note_legacy_calendar_tick();

uint64_t world_sim_tick_count();
uint64_t world_sim_legacy_calendar_ticks();

} // namespace fallout

#endif /* FALLOUT_SIM_WORLD_SIM_H_ */
