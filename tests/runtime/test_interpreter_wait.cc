// Runs the engine's own interpreter (src/int/intrpret.cc) on tiny programs built
// in memory, against a clock the test controls. These are characterization
// tests: they record what the interpreter does today, including behavior that
// looks wrong. A test that records such behavior says so in its name. When the
// behavior is fixed on purpose, that test is the one to change.

#include "test_harness.h"

#include "interpreter_stubs.h"

#include <vector>

#include "int/intrpret.h"
#include "sim/script_binding.h"

using namespace fallout;
using namespace fallout_test;

namespace {

// The image is a 42-byte header, the procedure table (a count, then 24 bytes per
// procedure), the identifier table and the static string table (four bytes each,
// both empty), and then the code.
const int kHeaderSize = 42;
const int kProcedureSize = 24;

int codeStartFor(int procedureCount)
{
    return kHeaderSize + 4 + kProcedureSize * procedureCount + 4 + 4;
}

// Without procedures.
const int kCodeStart = 54;

// Program bytes. Words are big endian.
struct Code {
    std::vector<unsigned char> bytes;

    Code& op(unsigned int opcode)
    {
        bytes.push_back(static_cast<unsigned char>(opcode >> 8));
        bytes.push_back(static_cast<unsigned char>(opcode));
        return *this;
    }

    // The integer constant opcode, then its value.
    Code& push(int value)
    {
        op(VALUE_TYPE_INT);
        for (int shift = 24; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<unsigned char>(value >> shift));
        }
        return *this;
    }
};

void putLong(std::vector<unsigned char>& data, size_t position, unsigned int value)
{
    for (int i = 0; i < 4; i++) {
        data[position + i] = static_cast<unsigned char>(value >> (24 - 8 * i));
    }
}

void putWord(std::vector<unsigned char>& data, size_t position, unsigned int value)
{
    data[position] = static_cast<unsigned char>(value >> 8);
    data[position + 1] = static_cast<unsigned char>(value);
}

// `procedureOffsets` are where each procedure's code starts, counted from the
// start of the code.
std::vector<unsigned char> image(const Code& code, const std::vector<int>& procedureOffsets = {})
{
    int count = static_cast<int>(procedureOffsets.size());
    int codeStart = codeStartFor(count);
    std::vector<unsigned char> data(codeStart, 0);

    // The return addresses setupCall() and executeProc() push are 24 and 20.
    // Each must hold the instruction that takes the saved address back off the
    // return stack and ends the interpreter's pass.
    putWord(data, 20, OPCODE_POP_EXIT);
    putWord(data, 24, OPCODE_POP_EXIT);

    putLong(data, kHeaderSize, count);
    for (int i = 0; i < count; i++) {
        size_t entry = kHeaderSize + 4 + i * kProcedureSize;
        putLong(data, entry + 16, codeStart + procedureOffsets[i]);
    }

    data.insert(data.end(), code.bytes.begin(), code.bytes.end());
    return data;
}

// Loads a program through the real allocateProgram() and points it at its code.
Program* load(const Code& code, const char* path = "test.int", const std::vector<int>& procedureOffsets = {})
{
    interpreter_stub_set_image(path, image(code, procedureOffsets));
    Program* program = allocateProgram(path);
    program->instructionPointer = codeStartFor(static_cast<int>(procedureOffsets.size()));
    return program;
}

// Starts each test from a clean interpreter, clock and program list.
class Scope {
public:
    Scope()
    {
        clearPrograms();
        interpreter_stub_reset();
        initInterpreter();
        interpretSetCPUBurstSize(10);
    }

    ~Scope() { clearPrograms(); }
};

Program* watched = nullptr;

// Ends a loop that would not end by itself, the way an outside event would.
void stop_after_many_reads(unsigned int reads)
{
    if (reads >= 5000) {
        watched->flags |= PROGRAM_FLAG_EXITED;
    }
}

// push 7, push <ms>, wait, push 111, exit program. The 111 shows that the
// program went on past the wait.
Code waitProgram(int milliseconds)
{
    Code code;
    code.push(7).push(milliseconds).op(OPCODE_WAIT).push(111).op(OPCODE_EXIT_PROGRAM);
    return code;
}

} // namespace

TEST(interpreter_wait_INVERSION_clears_a_wait_before_its_deadline)
{
    Scope scope;
    interpreterStub.now = 1000;

    Program* program = load(waitProgram(5000));
    interpret(program, 10);

    // The clock never moved, 5000 ms are still owed, and the program is
    // already past the wait and finished. checkWait() returns true while
    // now <= waitEnd, and interpret() reads true as "stop waiting".
    CHECK((program->flags & PROGRAM_FLAG_EXITED) != 0);
    CHECK((program->flags & PROGRAM_IS_WAITING) == 0);
    CHECK_EQ(program->stackValues->size(), 2u);
    CHECK_EQ(program->stackValues->at(1).integerValue, 111);
    CHECK_EQ(interpreterStub.now, 1000u);

    interpretFreeProgram(program);
}

TEST(interpreter_wait_INVERSION_never_clears_once_the_deadline_is_passed_and_keeps_its_state)
{
    Scope scope;

    // Each clock read moves time on by 1 ms, so the first check of a wait for
    // 0 ms already happens after the deadline.
    interpreterStub.now = 1000;
    interpreterStub.advancePerRead = 1;

    Program* program = load(waitProgram(0));
    interpret(program, 10);

    // The burst ran out in the wait. The program is still in it, and nothing
    // after it ran. The reads stay small: a finite burst gives control back.
    CHECK((program->flags & PROGRAM_FLAG_EXITED) == 0);
    CHECK((program->flags & PROGRAM_IS_WAITING) != 0);
    CHECK(program->checkWaitFunc != nullptr);
    CHECK_EQ(program->stackValues->size(), 1u);
    CHECK_EQ(program->stackValues->at(0).integerValue, 7);
    CHECK(interpreterStub.clockReads < 20u);

    int pointer = program->instructionPointer;
    unsigned int end = program->waitEnd;
    CHECK(program->waitStart == end);

    // Another burst: the same place, the same stack, the same wait.
    interpret(program, 10);
    CHECK_EQ(program->instructionPointer, pointer);
    CHECK_EQ(program->stackValues->size(), 1u);
    CHECK((program->flags & PROGRAM_IS_WAITING) != 0);
    CHECK(program->waitEnd == end);

    // Putting the clock back to the deadline lets the preserved program go on
    // from where it stood: the 7 is still on its stack, 111 comes after it.
    interpreterStub.advancePerRead = 0;
    interpreterStub.now = end;
    interpret(program, 10);
    CHECK((program->flags & PROGRAM_IS_WAITING) == 0);
    CHECK((program->flags & PROGRAM_FLAG_EXITED) != 0);
    CHECK_EQ(program->stackValues->size(), 2u);
    CHECK_EQ(program->stackValues->at(0).integerValue, 7);
    CHECK_EQ(program->stackValues->at(1).integerValue, 111);

    interpretFreeProgram(program);
}

TEST(interpreter_wait_pending_wait_spins_with_an_unlimited_burst)
{
    Scope scope;
    interpreterStub.now = 1000;
    interpreterStub.advancePerRead = 1;
    interpreterStub.onRead = stop_after_many_reads;

    Program* program = load(waitProgram(0));
    watched = program;

    // -1 is what executeProcedure() passes. The loop does not give control
    // back; only the stop above ends it.
    interpret(program, -1);

    CHECK(interpreterStub.clockReads >= 5000u);
    CHECK((program->flags & PROGRAM_IS_WAITING) != 0);
    CHECK_EQ(program->stackValues->size(), 1u);

    interpreterStub.onRead = nullptr;
    interpretFreeProgram(program);
}

TEST(interpreter_wait_pending_wait_spins_in_a_critical_section_even_with_a_finite_burst)
{
    Scope scope;
    interpreterStub.now = 1000;
    interpreterStub.advancePerRead = 1;
    interpreterStub.onRead = stop_after_many_reads;

    Code code;
    code.op(OPCODE_ENTER_CRITICAL_SECTION);
    code.push(7).push(0).op(OPCODE_WAIT).push(111).op(OPCODE_EXIT_PROGRAM);
    Program* program = load(code);
    watched = program;

    interpret(program, 10);

    CHECK(interpreterStub.clockReads >= 5000u);
    CHECK((program->flags & PROGRAM_FLAG_CRITICAL_SECTION) != 0);
    CHECK((program->flags & PROGRAM_IS_WAITING) != 0);
    CHECK_EQ(program->stackValues->size(), 1u);

    interpreterStub.onRead = nullptr;
    interpretFreeProgram(program);
}

namespace {

// push <a>, push <b>, exit. The exit marks the program for removal.
Program* finishingProgram(const char* path, int a, int b)
{
    Code code;
    code.push(a).push(b).op(OPCODE_EXIT);
    return load(code, path);
}

} // namespace

TEST(interpreter_update_program_runs_only_the_selected_program)
{
    Scope scope;

    Program* a = finishingProgram("a.int", 1, 2);
    Program* b = finishingProgram("b.int", 3, 4);
    runProgram(a);
    runProgram(b);

    updateProgram(a);

    // a ran and was removed from the list. b was not run, not even to the
    // point of leaving its start. The named events and script libraries were
    // not visited either.
    CHECK_EQ(interpreterStub.removedPrograms, 1);
    CHECK_EQ(interpreterStub.updateIntLibCalls, 0);
    CHECK_EQ(b->instructionPointer, kCodeStart);
    CHECK_EQ(b->stackValues->size(), 0u);
    CHECK((b->flags & PROGRAM_FLAG_EXITED) == 0);
    CHECK(!b->exited);

    // The full pass runs what is left and visits the libraries.
    updatePrograms();
    CHECK_EQ(interpreterStub.removedPrograms, 2);
    CHECK_EQ(interpreterStub.updateIntLibCalls, 1);
}

TEST(interpreter_update_program_with_a_program_not_in_the_list_runs_nothing)
{
    Scope scope;

    Program* a = finishingProgram("a.int", 1, 2);
    Program* stranger = finishingProgram("s.int", 5, 6);
    runProgram(a);

    updateProgram(stranger);
    updateProgram(nullptr);

    CHECK_EQ(a->instructionPointer, kCodeStart);
    CHECK_EQ(stranger->instructionPointer, kCodeStart);
    CHECK_EQ(interpreterStub.removedPrograms, 0);

    interpretFreeProgram(stranger);
}

namespace {

// A program with one procedure that waits. The main code is a single no-op: it
// only gives the saved instruction pointer somewhere to point. The procedure
// drops the argument count setupCall() pushed, waits, and returns.
const int kOneProcedure = 1;

Program* loadProcedureProgram(int milliseconds)
{
    Code code;
    code.op(OPCODE_NOOP);
    int procedureOffset = static_cast<int>(code.bytes.size());
    code.op(OPCODE_POP).push(milliseconds).op(OPCODE_WAIT).op(OPCODE_POP_FLAGS_RETURN);
    return load(code, "proc.int", { procedureOffset });
}

int savedPointer() { return codeStartFor(kOneProcedure); }

} // namespace

TEST(execute_procedure_INVERSION_returns_a_finished_call_because_the_wait_clears_at_once)
{
    Scope scope;
    interpreterStub.now = 1000;

    Program* program = loadProcedureProgram(5000);
    program->instructionPointer = savedPointer();
    executeProcedure(program, 0);

    // The flags are compared without the high half, which holds the last opcode
    // run. What a synchronous caller relies on: the procedure ran to its end, the
    // program is back where it was, and nothing is left pending. Here that holds
    // only because the 5000 ms wait was skipped at a clock that did not move.
    CHECK_EQ(program->instructionPointer, savedPointer());
    CHECK_EQ(program->stackValues->size(), 0u);
    CHECK_EQ(program->returnStackValues->size(), 0u);
    CHECK_EQ(program->flags & 0xFFFF, 0);
    CHECK(program->checkWaitFunc == nullptr);
    CHECK_EQ(interpreterStub.now, 1000u);

    interpretFreeProgram(program);
}

TEST(execute_procedure_blocks_on_a_pending_wait_and_leaves_an_unfinished_call_only_when_stopped)
{
    Scope scope;
    interpreterStub.now = 1000;
    interpreterStub.advancePerRead = 1;
    interpreterStub.onRead = stop_after_many_reads;

    Program* program = loadProcedureProgram(0);
    program->instructionPointer = savedPointer();
    watched = program;

    // The first check is after the deadline, so today the wait never clears.
    // executeProcedure() does not return on its own; the outside stop does.
    executeProcedure(program, 0);

    CHECK(interpreterStub.clockReads >= 5000u);

    // What a caller would see if it did return here: a call still in progress.
    CHECK(program->instructionPointer != savedPointer());
    CHECK((program->flags & PROGRAM_IS_WAITING) != 0);
    CHECK_EQ(program->returnStackValues->size(), 2u);
    CHECK_EQ(program->stackValues->size(), 3u);

    interpreterStub.onRead = nullptr;
    interpretFreeProgram(program);
}

TEST(execute_proc_with_a_finite_burst_gives_control_back_with_the_call_unfinished)
{
    Scope scope;
    interpreterStub.now = 1000;
    interpreterStub.advancePerRead = 1;

    Program* program = loadProcedureProgram(0);
    program->instructionPointer = savedPointer();

    // The interrupt form only sets the call up. The burst then runs it.
    executeProc(program, 0);
    interpret(program, 10);

    // The same program and the same wait as above, but control came back with
    // the call open: waiting, frame on both stacks, saved address not restored.
    CHECK((program->flags & PROGRAM_IS_WAITING) != 0);
    CHECK(program->instructionPointer != savedPointer());
    CHECK_EQ(program->returnStackValues->size(), 2u);
    CHECK_EQ(program->stackValues->size(), 3u);
    CHECK(interpreterStub.clockReads < 40u);

    int pointer = program->instructionPointer;
    interpret(program, 10);
    CHECK_EQ(program->instructionPointer, pointer);
    CHECK_EQ(program->returnStackValues->size(), 2u);
    CHECK_EQ(program->stackValues->size(), 3u);

    // Once the wait lets go, a later pass finishes the call and restores the
    // caller's state.
    interpreterStub.advancePerRead = 0;
    interpreterStub.now = program->waitEnd;
    interpret(program, 10);
    CHECK_EQ(program->instructionPointer, savedPointer());
    CHECK_EQ(program->stackValues->size(), 0u);
    CHECK_EQ(program->returnStackValues->size(), 0u);
    CHECK_EQ(program->flags & 0xFFFF, 0);

    interpretFreeProgram(program);
}

namespace {

bool sawRunningInsidePass = false;

void note_running_then_stop(unsigned int reads)
{
    if (interpretIsRunning()) {
        sawRunningInsidePass = true;
    }

    stop_after_many_reads(reads * 1000);
}

} // namespace

TEST(interpreter_is_running_only_inside_a_pass_and_clears_on_exit)
{
    Scope scope;
    CHECK(!interpretIsRunning());

    interpreterStub.now = 1000;
    interpreterStub.advancePerRead = 1;
    interpreterStub.onRead = note_running_then_stop;
    sawRunningInsidePass = false;

    Program* program = load(waitProgram(0));
    watched = program;
    interpret(program, -1);

    CHECK(sawRunningInsidePass);
    CHECK(!interpretIsRunning());

    interpreterStub.onRead = nullptr;
    interpretFreeProgram(program);
}

TEST(interpreter_is_running_clears_after_a_script_error_unwinds_the_pass)
{
    Scope scope;

    // A word with the high bit of its first byte clear is a bad opcode. The
    // error unwinds the pass through the program's jump buffer.
    Code code;
    code.op(0x0001);
    // Two procedures, because the error report reads the next procedure's
    // start to find the one it stopped in.
    Program* program = load(code, "test.int", { 0, 100 });

    interpret(program, 10);

    CHECK(!interpretIsRunning());
    CHECK((program->flags & PROGRAM_FLAG_EXITED) != 0);

    interpretFreeProgram(program);
}

TEST(interpreter_allocation_and_free_manage_program_lifetime_identity)
{
    Scope scope;
    Program* program = load(waitProgram(0));
    const uint64_t identity = program_lifetimes().identity(program);
    CHECK(identity != 0);
    CHECK(program_lifetimes().matches(program, identity));
    interpretFreeProgram(program);
    CHECK(!program_lifetimes().matches(program, identity));
    Program* replacement = load(waitProgram(0));
    CHECK(program_lifetimes().identity(replacement) > identity);
    CHECK(!program_lifetimes().matches(program, identity));
    interpretFreeProgram(replacement);
}
