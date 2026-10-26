#ifndef FALLOUT_SIM_PAUSE_SET_H_
#define FALLOUT_SIM_PAUSE_SET_H_

#include <stdint.h>

#include <functional>
#include <memory>

namespace fallout {

class PauseSet;

// One owner's claim on the pause. The world is paused while at least one token
// is held. Releasing is idempotent and may happen in any order. A token that
// outlives its PauseSet releases nothing and does not crash.
class PauseToken {
public:
    PauseToken();
    ~PauseToken();

    PauseToken(PauseToken&& other) noexcept;
    PauseToken& operator=(PauseToken&& other) noexcept;

    PauseToken(const PauseToken&) = delete;
    PauseToken& operator=(const PauseToken&) = delete;

    // Gives the claim back. A second call, or a call on an empty or moved-from
    // token, does nothing.
    void release();

    // `true` while the token holds a claim on a live PauseSet.
    bool active() const;

    // Shared with the owning PauseSet. Not for callers.
    struct State;

private:
    friend class PauseSet;

    explicit PauseToken(const std::shared_ptr<State>& state);

    // release() for destructors and move assignment, which must not throw.
    void releaseQuietly() noexcept;

    std::weak_ptr<State> state_;
    bool held_;
};

// Counts the held tokens. Not thread safe: use it from the game thread only.
class PauseSet {
public:
    typedef std::function<void(bool paused)> Listener;

    PauseSet();
    ~PauseSet();

    PauseSet(const PauseSet&) = delete;
    PauseSet& operator=(const PauseSet&) = delete;

    // Takes a claim. The result is empty (inactive) only if the count would
    // overflow.
    PauseToken acquire();

    bool isPaused() const;
    uint32_t holderCount() const;

    // Counts every change between not paused and paused, in both directions.
    // A consumer that saves this number can tell, later, that a pause began
    // or ended since then, even one that began and ended without the consumer
    // looking, or one that was released after the consumer last looked while it
    // was still paused. Nested holders do not change it. It needs no listener.
    uint64_t transitionCount() const;

    // Called when the set goes from not paused to paused and back, never for a
    // change in the count alone. The listener may acquire and release tokens;
    // it is then called again until it has seen the final state, so it is never
    // left out of step. It may also destroy the set. Pass an empty function to
    // remove it.
    //
    // The listener should not throw. If it does, the exception leaves the call
    // that caused the change (acquire(), release()) and the set stays usable.
    // A token destructor and a move assignment swallow it, as they must not throw.
    void setListener(const Listener& listener);

private:
    std::shared_ptr<PauseToken::State> state_;
};

} // namespace fallout

#endif /* FALLOUT_SIM_PAUSE_SET_H_ */
