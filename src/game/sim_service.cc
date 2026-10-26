#include "game/sim_service.h"

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

void sim_service_init(int mode)
{
    WorldSimPlatform platform = {};
    platform.now = get_time;
    platform.addPump = add_bk_process;
    platform.removePump = remove_bk_process;
    platform.getFocus = get_focus_func;
    platform.setFocus = set_focus_func;
    platform.setWorldPaused = sim_service_set_world_paused;
    platform.shiftTimers = sim_service_shift_timers;
    platform.pauseOriginReset = sim_service_pause_origin_reset;
    platform.log = sim_service_log;

    if (!world_sim_init(mode, platform)) {
        debug_printf("WORLD_SIM=%d could not start. Running the existing scheduler.\n", mode);
    }
}

void sim_service_exit()
{
    world_sim_exit();
}

} // namespace fallout
