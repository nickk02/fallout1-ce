#include "game/scripts.h"
#include "sim/script_binding.h"
#include "test_harness.h"

using namespace fallout;
namespace {
Script* live;
Script* lookup(int sid) { return live != nullptr && live->scr_id == sid ? live : nullptr; }
struct Fixture {
    Script script {};
    Program program {};
    Object source {};
    Fixture()
    {
        script.scr_id = 10;
        script.program = &program;
        script.source = &source;
        script.runtimeIdentity.incarnation = new_script_incarnation();
        program_lifetimes().admit(&program);
        live = &script;
    }
    ~Fixture()
    {
        program_lifetimes().retire(&program);
        live = nullptr;
    }
};
}

TEST(script_binding_fresh_lookup_follows_compaction)
{
    Fixture f;
    const auto binding = begin_script_call(f.script, 5);
    Script moved = f.script;
    live = &moved;
    CHECK(finish_script_call(binding, 5, lookup));
    CHECK(moved.source == nullptr);
    CHECK(f.script.source == &f.source);
}

TEST(script_binding_sid_reuse_removal_and_epoch_reject)
{
    Fixture f;
    const auto binding = begin_script_call(f.script, 5);
    live = nullptr;
    CHECK(!finish_script_call(binding, 5, lookup));
    live = &f.script;
    CHECK(!finish_script_call(binding, 6, lookup));
    f.script.runtimeIdentity.incarnation = new_script_incarnation();
    CHECK(!finish_script_call(binding, 5, lookup));
    CHECK(f.script.source == &f.source);
}

TEST(script_binding_program_address_reuse_and_retirement_reject)
{
    Fixture f;
    const auto binding = begin_script_call(f.script, 5);
    program_lifetimes().retire(&f.program);
    CHECK(!finish_script_call(binding, 5, lookup));
    CHECK(program_lifetimes().admit(&f.program) != binding.programIdentity);
    CHECK(!finish_script_call(binding, 5, lookup));
    CHECK(f.script.source == &f.source);
}

TEST(script_binding_nested_dispatch_cannot_clear_newer_source)
{
    Fixture f;
    const auto outer = begin_script_call(f.script, 5);
    const auto inner = begin_script_call(f.script, 5);
    CHECK(finish_script_call(inner, 5, lookup));
    f.script.source = &f.source;
    CHECK(!finish_script_call(outer, 5, lookup));
    CHECK(f.script.source == &f.source);
}

TEST(script_binding_lifetime_exhaustion_and_invalid_bindings_fail_closed)
{
    ProgramLifetimes lifetimes(UINT64_MAX - 1);
    int a, b;
    CHECK_EQ(lifetimes.admit(&a), UINT64_MAX);
    lifetimes.retire(&a);
    CHECK_EQ(lifetimes.admit(&a), 0u);
    CHECK_EQ(lifetimes.admit(&b), 0u);
    CHECK(!lifetimes.matches(&a, UINT64_MAX));
    Fixture f;
    f.script.runtimeIdentity.incarnation = 0;
    CHECK(!begin_script_call(f.script, 5).valid());
    CHECK(!finish_script_call({}, 5, lookup));
}
