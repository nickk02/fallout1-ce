#ifndef FALLOUT_GAME_SIM_SERVICE_H_
#define FALLOUT_GAME_SIM_SERVICE_H_

namespace fallout {

// Starts the world simulation service (sim/world_sim.h) with the engine's
// functions. Call once the window and input system are initialized. If it
// cannot start it logs that and the game runs as with mode 0.
void sim_service_init(int mode);

// Call before the input system is torn down.
void sim_service_exit();

} // namespace fallout

#endif /* FALLOUT_GAME_SIM_SERVICE_H_ */
