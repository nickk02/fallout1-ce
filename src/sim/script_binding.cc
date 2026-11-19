#include "sim/script_binding.h"

#include <new>

#include "game/scripts.h"

namespace fallout {
namespace {
    uint64_t lastScript = 0;
    uint64_t lastDispatch = 0;
}

uint64_t ProgramLifetimes::admit(const void* program)
{
    if (program == nullptr || _last == UINT64_MAX || _live.find(program) != _live.end()) return 0;
    try {
        const uint64_t next = _last + 1;
        _live.emplace(program, next);
        _last = next;
        return next;
    } catch (const std::bad_alloc&) {
        return 0;
    }
}

void ProgramLifetimes::retire(const void* program) { _live.erase(program); }
uint64_t ProgramLifetimes::identity(const void* program) const
{
    auto found = _live.find(program);
    return found != _live.end() ? found->second : 0;
}
bool ProgramLifetimes::matches(const void* program, uint64_t identity) const
{
    return identity != 0 && this->identity(program) == identity;
}
ProgramLifetimes& program_lifetimes()
{
    static ProgramLifetimes lifetimes;
    return lifetimes;
}
uint64_t new_script_incarnation() { return lastScript != UINT64_MAX ? ++lastScript : 0; }
bool script_incarnations_available(unsigned int count) { return UINT64_MAX - lastScript >= count; }

ScriptCallBinding begin_script_call(Script& script, uint64_t epoch)
{
    if (script.runtimeIdentity.incarnation == 0 || lastDispatch == UINT64_MAX) return {};
    const uint64_t programIdentity = program_lifetimes().identity(script.program);
    if (programIdentity == 0) return {};
    script.runtimeIdentity.dispatch = ++lastDispatch;
    return { script.scr_id, script.runtimeIdentity.incarnation, script.runtimeIdentity.dispatch, programIdentity, epoch, script.program };
}

bool finish_script_call(const ScriptCallBinding& binding, uint64_t epoch, Script* (*lookup)(int))
{
    if (!binding.valid() || binding.epoch != epoch || !program_lifetimes().matches(binding.program, binding.programIdentity)) return false;
    Script* script = lookup(binding.sid);
    if (script == nullptr || script->runtimeIdentity.incarnation != binding.incarnation
        || script->runtimeIdentity.dispatch != binding.dispatch || script->program != binding.program) return false;
    script->source = nullptr;
    return true;
}

} // namespace fallout
