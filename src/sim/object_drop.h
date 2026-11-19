#ifndef FALLOUT_SIM_OBJECT_DROP_H_
#define FALLOUT_SIM_OBJECT_DROP_H_

#include "game/object_types.h"
#include "object_handle.h"
#include "plib/gnw/rect.h"

namespace fallout {

struct ObjectDropHooks {
    int (*scriptId)(Object*, int*);
    void (*runScript)(int, Object*, Object*);
    bool (*readOverride)(int, bool*);
    int (*remove)(Object*, Object*);
    Object* (*topEnvironment)(Object*);
    int (*connect)(Object*, int, int, Rect*);
    void (*refresh)(Rect*, int);
};

// The obj_drop orchestration. Resource and script calls stay engine seams;
// identity capture, cancellation and continuation are production code here.
int object_drop_guarded(const ObjectRegistry& registry, Object* actor, Object* item, uint64_t (*epoch)(), const ObjectDropHooks& hooks);

} // namespace fallout

#endif
