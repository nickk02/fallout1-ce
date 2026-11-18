#include "game/sim_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/anim.h"
#include "game/combat.h"
#include "game/free_move.h"
#include "game/scripts.h"
#include "game/textobj.h"
#include "int/intrpret.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/input.h"
#include "sim/world_sim.h"

namespace fallout {

// Called when the first pause is taken and when the last one is released.
static void sim_service_set_world_paused(bool paused)
{
    if (paused) free_move_sim_cancel();
    anim_set_paused(paused);

    // Timed and conditional script procedures stand still. On the way out the
    // interpreter moves the timed ones forward by the length of the pause.
    if (paused) {
        interpretSuspendEvents();
    } else {
        interpretResumeEvents();
    }
}

static void sim_service_shift_timers(unsigned int elapsed, unsigned int now)
{
    combat_world_resumed(elapsed);
    free_move_world_resumed(elapsed);
    text_object_shift_times(elapsed);
    interpretShiftWaits(elapsed);

    // From the clock reading of this moment. The background pass keeps its own
    // reading, which is as old as its last run.
    scr_rebase_timed_events(now);
}

static void sim_service_pause_origin_reset()
{
    interpretRestartSuspension();
}

static void sim_service_log(const char* text)
{
    debug_printf("%s", text);
}

static bool sim_service_log_enabled = false;

static void sim_service_event(const char* line)
{
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
}

static void sim_service_describe(char* out, size_t size)
{
    unsigned int pending = 0;
    unsigned long long dropped = 0;
    const char* status = "";
    scr_calendar_stats(&pending, &dropped, &status);

    snprintf(out, size, " game_time=%d cal_pending=%u cal_dropped=%llu cal_status=%s ap_grants=%llu",
        game_time(), pending, dropped, status, combat_ap_grants());
}

void sim_service_note(const char* line)
{
    if (sim_service_log_enabled) {
        sim_service_event(line);
    }
}

void sim_service_init(int mode)
{
    const char* simlog = getenv("FALLOUT_ME_SIMLOG");
    sim_service_log_enabled = simlog != nullptr && simlog[0] != '\0' && strcmp(simlog, "0") != 0;

    WorldSimPlatform platform = {};
    platform.now = get_time;
    platform.addPump = add_bk_process;
    platform.removePump = remove_bk_process;
    platform.getFocus = get_focus_func;
    platform.setFocus = set_focus_func;
    platform.setWorldPaused = sim_service_set_world_paused;
    platform.shiftTimers = sim_service_shift_timers;
    platform.pauseOriginReset = sim_service_pause_origin_reset;
    platform.gameplayTick = free_move_sim_tick;
    platform.log = sim_service_log;
    if (sim_service_log_enabled) {
        platform.event = sim_service_event;
        platform.describe = sim_service_describe;
    }

    if (!world_sim_init(mode, platform)) {
        debug_printf("WORLD_SIM=%d could not start. Running the existing scheduler.\n", mode);
    }
}

void sim_service_exit()
{
    free_move_sim_cancel();
    world_sim_exit();
}

} // namespace fallout
