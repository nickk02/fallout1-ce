#ifndef FALLOUT_TESTS_RUNTIME_ANIMATION_STUBS_H_
#define FALLOUT_TESTS_RUNTIME_ANIMATION_STUBS_H_

namespace fallout {
struct Object;
}

namespace fallout_test {
struct AnimationStubState {
    unsigned int now = 0;
    int leaseBegins = 0;
    int leaseFinishes = 0;
    int objectWrites = 0;
    int refreshes = 0;
    int yields = 0;
    // Runs on every free_move_yield call, so a test can apply the same rule
    // free_move.cc does.
    void (*yieldHook)(fallout::Object*) = nullptr;
    // Makes the art calls answer with a two-frame cycle instead of failing the
    // test, so a real animation can run to its end.
    bool art = false;
    // Runs after a combat animation lease is finished, where combat.cc checks
    // whether the active attack can complete.
    void (*leaseFinishedHook)() = nullptr;
};
extern AnimationStubState animationStub;
void animation_stub_reset();
void animation_stub_set_combat(bool active);
int animation_stub_outstanding_leases();
}

#endif
