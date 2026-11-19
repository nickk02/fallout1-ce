#include "sim/pause_set.h"

#include <limits.h>

#include <utility>

namespace fallout {

struct PauseToken::State {
    uint32_t holders = 0;
    uint64_t transitions = 0;

    // The state the listener was last told about.
    bool notified = false;
    bool notifying = false;
    PauseSet::Listener listener;
};

namespace {

    // Calls the listener until it has seen the current state. A call made while a
    // notification is running only records the change; the outer loop delivers it.
    void notify(PauseToken::State& state);

} // namespace

PauseToken::PauseToken()
    : held_(false)
{
}

PauseToken::PauseToken(const std::shared_ptr<State>& state)
    : state_(state)
    , held_(true)
{
}

PauseToken::~PauseToken()
{
    releaseQuietly();
}

void PauseToken::releaseQuietly() noexcept
{
    try {
        release();
    } catch (...) {
        // A listener threw. The claim is already given back.
    }
}

PauseToken::PauseToken(PauseToken&& other) noexcept
    : state_(std::move(other.state_))
    , held_(other.held_)
{
    other.state_.reset();
    other.held_ = false;
}

PauseToken& PauseToken::operator=(PauseToken&& other) noexcept
{
    if (this != &other) {
        releaseQuietly();
        state_ = std::move(other.state_);
        held_ = other.held_;
        other.state_.reset();
        other.held_ = false;
    }

    return *this;
}

bool PauseToken::active() const
{
    return held_ && !state_.expired();
}

PauseSet::PauseSet()
    : state_(std::make_shared<PauseToken::State>())
{
}

PauseSet::~PauseSet()
{
    // Tokens only hold weak references, so they become inert here. The listener
    // is not called: the owner is going away.
    state_->listener = nullptr;
}

PauseToken PauseSet::acquire()
{
    if (state_->holders == UINT32_MAX) {
        return PauseToken();
    }

    // The listener may destroy this set, so hold the state across the call.
    std::shared_ptr<PauseToken::State> state = state_;
    state->holders++;
    if (state->holders == 1) {
        state->transitions++;
    }
    PauseToken token(state);
    notify(*state);
    return token;
}

bool PauseSet::isPaused() const
{
    return state_->holders != 0;
}

uint64_t PauseSet::transitionCount() const
{
    return state_->transitions;
}

uint32_t PauseSet::holderCount() const
{
    return state_->holders;
}

void PauseSet::setListener(const Listener& listener)
{
    state_->listener = listener;
}

void PauseToken::release()
{
    if (!held_) {
        return;
    }

    held_ = false;

    std::shared_ptr<State> state = state_.lock();
    state_.reset();
    if (state == nullptr) {
        return;
    }

    if (state->holders != 0) {
        state->holders--;
        if (state->holders == 0) {
            state->transitions++;
        }
    }

    notify(*state);
}

namespace {

    void notify(PauseToken::State& state)
    {
        if (state.notifying) {
            return;
        }

        // Clears the flag if the listener throws. Otherwise no later change
        // would be announced.
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset = { state.notifying };

        state.notifying = true;
        while (state.notified != (state.holders != 0)) {
            state.notified = state.holders != 0;

            // Copy first: the listener may replace or clear itself.
            PauseSet::Listener listener = state.listener;
            if (listener) {
                listener(state.notified);
            }
        }
    }

} // namespace

} // namespace fallout
