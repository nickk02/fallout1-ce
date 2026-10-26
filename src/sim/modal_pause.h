#ifndef FALLOUT_SIM_MODAL_PAUSE_H_
#define FALLOUT_SIM_MODAL_PAUSE_H_

#include "sim/world_sim.h"

namespace fallout {

// Holds the world pause for the length of a full-screen modal. Acquire it just
// before map_disable_bk_processes() and release it just after the matching
// map_enable_bk_processes(). In modes 0 and 1 it never pauses the world.
class ModalPause {
public:
    ModalPause()
        : token_(world_sim_pause())
    {
    }

    ~ModalPause() { token_.release(); }

    ModalPause(ModalPause&&) noexcept = default;
    ModalPause& operator=(ModalPause&&) noexcept = default;

    ModalPause(const ModalPause&) = delete;
    ModalPause& operator=(const ModalPause&) = delete;

    // Safe to call more than once.
    void release() { token_.release(); }

private:
    PauseToken token_;
};

} // namespace fallout

#endif /* FALLOUT_SIM_MODAL_PAUSE_H_ */
