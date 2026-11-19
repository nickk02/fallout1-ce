#ifndef FALLOUT_TESTS_RUNTIME_QUEUE_STUBS_H_
#define FALLOUT_TESTS_RUNTIME_QUEUE_STUBS_H_

#include <set>

namespace fallout_test {

struct QueueStubState {
    // What game_time() returns.
    int now = 0;

    // Allocations made through the engine's allocator (mem_malloc) that have
    // not been given back, and calls that did not match one.
    std::set<void*> live;
    int allocations = 0;
    int frees = 0;
    int badFrees = 0;
};

extern QueueStubState queueStub;

void queue_stub_reset();

} // namespace fallout_test

#endif /* FALLOUT_TESTS_RUNTIME_QUEUE_STUBS_H_ */
