#ifndef FALLOUT_SIM_SCRIPT_BINDING_H_
#define FALLOUT_SIM_SCRIPT_BINDING_H_

#include <stdint.h>
#include <unordered_map>

namespace fallout {

struct Script;
struct Program;

// Runtime-only. These fields travel with a record during compaction but are
// never written to a save file or restored from one.
struct ScriptRuntimeIdentity {
    uint64_t incarnation = 0;
    uint64_t dispatch = 0;
};

class ProgramLifetimes {
public:
    explicit ProgramLifetimes(uint64_t last = 0)
        : _last(last)
    {
    }
    uint64_t admit(const void* program);
    void retire(const void* program);
    uint64_t identity(const void* program) const;
    bool matches(const void* program, uint64_t identity) const;

private:
    uint64_t _last;
    std::unordered_map<const void*, uint64_t> _live;
};

ProgramLifetimes& program_lifetimes();
uint64_t new_script_incarnation();
bool script_incarnations_available(unsigned int count);

struct ScriptCallBinding {
    int sid = -1;
    uint64_t incarnation = 0;
    uint64_t dispatch = 0;
    uint64_t programIdentity = 0;
    uint64_t epoch = 0;
    Program* program = nullptr;
    bool valid() const { return incarnation != 0 && dispatch != 0 && programIdentity != 0; }
};

ScriptCallBinding begin_script_call(Script& script, uint64_t epoch);
bool finish_script_call(const ScriptCallBinding& binding, uint64_t epoch, Script* (*lookup)(int));

} // namespace fallout

#endif
