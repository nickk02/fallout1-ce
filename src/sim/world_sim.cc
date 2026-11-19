#include "sim/world_sim.h"

#include <memory>
#include <stdio.h>

#include "sim/sim_clock.h"
#include "sim/sim_log.h"

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

    std::shared_ptr<State> state;
    bool pumping = false;
    bool initialized = false;
    int mode = WORLD_SIM_EXISTING;

    // Lives as long as the process, outside State, so that it survives the
    // State being deleted and made again.
    uint64_t epoch = 0;

    // The focus callback that was installed before ours. It survives world_sim_exit()
    // when ours could not be taken out, so the chain is not broken.
    WorldSimFocusFunction* chainedFocus = nullptr;

    // Diagnostic line buffer size. The formatters truncate.
    const size_t kEventLineSize = 192;

    // Set while platform.event or platform.describe runs. Those callbacks may
    // only format and write. The calls that change the lifecycle are refused
    // meanwhile, so a sink cannot free the state under the caller.
    bool delivering = false;

    class DeliveryScope {
    public:
        DeliveryScope() { delivering = true; }
        ~DeliveryScope() { delivering = false; }

        DeliveryScope(const DeliveryScope&) = delete;
        DeliveryScope& operator=(const DeliveryScope&) = delete;
    };

    void deliverEvent(const char* line)
    {
        DeliveryScope scope;
        state->platform.event(line);
    }

    void emitLifecycle(const char* what)
    {
        if (state == nullptr || state->platform.event == nullptr) {
            return;
        }

        char line[kEventLineSize];
        sim_log_format_lifecycle(line, sizeof(line), what, epoch, state->ticks, state->pauses.isPaused());
        deliverEvent(line);
    }

    void onPauseChanged(bool paused)
    {
        if (state == nullptr || state->mode != WORLD_SIM_OWNED) {
            return;
        }

        if (paused) {
            state->pauseStart = state->platform.now();
            if (state->platform.event != nullptr) {
                char line[kEventLineSize];
                sim_log_format_pause(line, sizeof(line), true, epoch, state->ticks, 0);
                deliverEvent(line);
            }
            state->platform.setWorldPaused(true);
        } else {
            unsigned int now = state->platform.now();
            unsigned int elapsed = now - state->pauseStart;
            if (state->platform.event != nullptr) {
                char line[kEventLineSize];
                sim_log_format_pause(line, sizeof(line), false, epoch, state->ticks, elapsed);
                deliverEvent(line);
            }
            state->platform.setWorldPaused(false);
            state->platform.shiftTimers(elapsed, now);
        }
    }

    void onTick(const std::shared_ptr<State>& pass, uint64_t passEpoch)
    {
        pass->ticks++;
        if (pass->mode == WORLD_SIM_OWNED && pass->platform.gameplayTick != nullptr) {
            pass->platform.gameplayTick();
        }
        // The callback may replace the service, reset, or invalidate its map.
        // Stop the old clock before returning to advance(), which still holds
        // the old pause set. The pump pins both until advance() returns.
        if (state != pass || epoch != passEpoch) {
            pass->clock.reset();
            return;
        }

        if (pass->platform.log != nullptr && pass->ticks % kShadowLogInterval == 0) {
            char text[128];
            snprintf(text, sizeof(text), "WORLD_SIM: %llu sim ticks, %llu legacy calendar ticks\n",
                static_cast<unsigned long long>(pass->ticks),
                static_cast<unsigned long long>(pass->legacyCalendarTicks));
            pass->platform.log(text);
            if (state != pass || epoch != passEpoch) {
                pass->clock.reset();
                return;
            }
        }

        if (pass->platform.event != nullptr && pass->ticks % kShadowLogInterval == 0) {
            char detail[kEventLineSize];
            detail[0] = '\0';
            if (pass->platform.describe != nullptr) {
                DeliveryScope scope;
                pass->platform.describe(detail, sizeof(detail));
            }

            char line[kEventLineSize];
            sim_log_format_summary(line, sizeof(line), pass->mode, epoch, pass->ticks, pass->legacyCalendarTicks, detail);
            deliverEvent(line);
        }
    }

    void pump()
    {
        if (state == nullptr || pumping || delivering) return;
        pumping = true;
        struct PumpGuard {
            ~PumpGuard() { pumping = false; }
        } guard;
        const auto pass = state;
        const uint64_t passEpoch = epoch;
        const unsigned int now = pass->platform.now();
        if (state != pass || epoch != passEpoch) {
            pass->clock.reset();
            return;
        }
        pass->clock.advance(now, &pass->pauses, [&] { onTick(pass, passEpoch); });
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
    if (delivering || initialized) {
        return false;
    }

    if (requestedMode != WORLD_SIM_EXISTING && requestedMode != WORLD_SIM_SHADOW && requestedMode != WORLD_SIM_OWNED) {
        return false;
    }

    if (requestedMode == WORLD_SIM_EXISTING) {
        initialized = true;
        mode = WORLD_SIM_EXISTING;
        epoch++;
        return true;
    }

    if (!platformIsComplete(requestedMode, platform)) {
        return false;
    }

    state = std::make_shared<State>();
    state->mode = requestedMode;
    state->platform = platform;
    state->pauses.setListener(onPauseChanged);

    chainedFocus = platform.getFocus();
    platform.setFocus(onFocus);
    platform.addPump(pump);

    initialized = true;
    mode = requestedMode;
    epoch++;
    emitLifecycle("init");
    return true;
}

void world_sim_exit()
{
    if (delivering || !initialized) {
        return;
    }

    // Anything that kept the epoch from before no longer matches.
    epoch++;

    if (state != nullptr) {
        const auto old = state;
        old->clock.reset();

        emitLifecycle("exit");

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

        // Tokens held elsewhere only hold weak references. An active pump
        // retains this state until its old clock has returned from advance().
        return;
    }

    initialized = false;
    mode = WORLD_SIM_EXISTING;
}

uint64_t world_sim_epoch()
{
    return epoch;
}

void world_sim_invalidate()
{
    if (!delivering && initialized) {
        epoch++;
        if (pumping && state != nullptr) state->clock.reset();
        emitLifecycle("invalidate");
    }
}

bool world_sim_drain()
{
    if (state == nullptr || pumping || delivering) return false;
    const auto pass = state;
    const uint64_t passEpoch = epoch;
    pump();
    return state == pass && epoch == passEpoch;
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
    if (delivering || state == nullptr) {
        return PauseToken();
    }

    return state->pauses.acquire();
}

void world_sim_reset()
{
    if (delivering) {
        return;
    }

    if (initialized) {
        epoch++;
    }

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

    emitLifecycle("reset");
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
