#ifndef FALLOUT_TESTS_RUNTIME_INTERPRETER_STUBS_H_
#define FALLOUT_TESTS_RUNTIME_INTERPRETER_STUBS_H_

#include <vector>

namespace fallout_test {

struct InterpreterStubState {
    // The value get_time() returns next, in ms.
    unsigned int now = 0;

    // Added to `now` after every read. Zero keeps the clock still.
    unsigned int advancePerRead = 0;

    unsigned int clockReads = 0;

    // Called after every read with the running read count. A test uses it to
    // stop a loop that would otherwise never end.
    void (*onRead)(unsigned int reads) = nullptr;

    int updateIntLibCalls = 0;
    int removedPrograms = 0;
};

extern InterpreterStubState interpreterStub;

// Clears the state and every registered image.
void interpreter_stub_reset();

// Makes `image` readable through the interpreter's file calls under `path`.
void interpreter_stub_set_image(const char* path, const std::vector<unsigned char>& image);

} // namespace fallout_test

#endif /* FALLOUT_TESTS_RUNTIME_INTERPRETER_STUBS_H_ */
