#include "sim/world_sim.h"

#include <stdio.h>

#include "sim/sim_clock.h"

namespace fallout {

namespace {

    // Ticks between two shadow log lines: ten seconds at 60 Hz.
    const uint64_t kShadowLogInterval = 600;

    struct State {
        int mode = WORLD_SIM_EXISTING;
        WorldSimPlatform platform = {};
        PauseSet pauses;
        SimClock clock;
        PauseToken focusToken;
        unsigned int pauseStart = 0;
        uint64_t ticks = 0;
        uint64_t legacyCalendarTicks = 0;
    };

    State* state = nullptr;
    bool initialized = false;
    int mode = WORLD_SIM_EXISTING;

    // The focus callback that was installed before ours. It survives world_sim_exit()
    // when ours could not be taken out, so the chain is not broken.
    WorldSimFocusFunction* chainedFocus = nullptr;

    void onPauseChanged(bool paused)
    {
        if (state == nullptr || state->mode != WORLD_SIM_OWNED) {
            return;
        }

        if (paused) {
            state->pauseStart = state->platform.now();
            state->platform.setWorldPaused(true);
        } else {
            unsigned int now = state->platform.now();
            unsigned int elapsed = now - state->pauseStart;
            state->platform.setWorldPaused(false);
            state->platform.shiftTimers(elapsed, now);
        }
    }

    void onTick()
    {
        state->ticks++;

        if (state->platform.log != nullptr && state->ticks % kShadowLogInterval == 0) {
            char text[128];
            snprintf(text, sizeof(text), "WORLD_SIM: %llu sim ticks, %llu legacy calendar ticks\n",
                static_cast<unsigned long long>(state->ticks),
                static_cast<unsigned long long>(state->legacyCalendarTicks));
            state->platform.log(text);
        }
    }

    void pump()
    {
        if (state == nullptr) {
            return;
        }

        state->clock.advance(state->platform.now(), &state->pauses, onTick);
    }

    void onFocus(int gained)
    {
        if (state != nullptr) {
            if (gained == 0) {
                // A second loss without a regain keeps the one claim.
                if (!state->focusToken.active()) {
                    state->focusToken = state->pauses.acquire();
                }
            } else {
                state->focusToken.release();
            }
        }

        if (chainedFocus != nullptr) {
            chainedFocus(gained);
        }
    }

    bool platformIsComplete(int requestedMode, const WorldSimPlatform& platform)
    {
        if (platform.now == nullptr || platform.addPump == nullptr || platform.removePump == nullptr
            || platform.getFocus == nullptr || platform.setFocus == nullptr) {
            return false;
        }

        if (requestedMode == WORLD_SIM_OWNED
            && (platform.setWorldPaused == nullptr || platform.shiftTimers == nullptr)) {
            return false;
        }

        return true;
    }

} // namespace

bool world_sim_init(int requestedMode, const WorldSimPlatform& platform)
{
    if (initialized) {
        return false;
    }

    if (requestedMode != WORLD_SIM_EXISTING && requestedMode != WORLD_SIM_SHADOW && requestedMode != WORLD_SIM_OWNED) {
        return false;
    }

    if (requestedMode == WORLD_SIM_EXISTING) {
        initialized = true;
        mode = WORLD_SIM_EXISTING;
        return true;
    }

    if (!platformIsComplete(requestedMode, platform)) {
        return false;
    }

    state = new State();
    state->mode = requestedMode;
    state->platform = platform;
    state->pauses.setListener(onPauseChanged);

    chainedFocus = platform.getFocus();
    platform.setFocus(onFocus);
    platform.addPump(pump);

    initialized = true;
    mode = requestedMode;
    return true;
}

void world_sim_exit()
{
    if (!initialized) {
        return;
    }

    if (state != nullptr) {
        State* old = state;

        old->platform.removePump(pump);

        if (old->platform.getFocus() == onFocus) {
            old->platform.setFocus(chainedFocus);
            chainedFocus = nullptr;
        }

        // No hooks from here on. A pause still held leaves the animations
        // frozen, so thaw them.
        old->pauses.setListener(nullptr);
        bool wasPaused = old->mode == WORLD_SIM_OWNED && old->pauses.isPaused();

        state = nullptr;
        initialized = false;
        mode = WORLD_SIM_EXISTING;

        if (wasPaused) {
            old->platform.setWorldPaused(false);
        }

        // Tokens held elsewhere only hold weak references.
        delete old;
        return;
    }

    initialized = false;
    mode = WORLD_SIM_EXISTING;
}

int world_sim_mode()
{
    return mode;
}

bool world_sim_owns_pauses()
{
    return state != nullptr && state->mode == WORLD_SIM_OWNED;
}

bool world_sim_world_paused()
{
    return world_sim_owns_pauses() && state->pauses.isPaused();
}

PauseToken world_sim_pause()
{
    if (state == nullptr) {
        return PauseToken();
    }

    return state->pauses.acquire();
}

void world_sim_reset()
{
    if (state == nullptr) {
        return;
    }

    state->clock.reset();

    if (state->mode == WORLD_SIM_OWNED && state->pauses.isPaused()) {
        state->pauseStart = state->platform.now();
        if (state->platform.pauseOriginReset != nullptr) {
            state->platform.pauseOriginReset();
        }
    }
}

void world_sim_note_legacy_calendar_tick()
{
    if (state != nullptr) {
        state->legacyCalendarTicks++;
    }
}

uint64_t world_sim_tick_count()
{
    return state != nullptr ? state->ticks : 0;
}

uint64_t world_sim_legacy_calendar_ticks()
{
    return state != nullptr ? state->legacyCalendarTicks : 0;
}

} // namespace fallout
