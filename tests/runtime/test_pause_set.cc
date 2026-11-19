#include "test_harness.h"

#include <utility>
#include <vector>

#include "sim/pause_set.h"

using namespace fallout;

TEST(pause_set_starts_unpaused)
{
    PauseSet pauses;
    CHECK(!pauses.isPaused());
    CHECK_EQ(pauses.holderCount(), 0u);
}

TEST(pause_set_nested_tokens_count)
{
    PauseSet pauses;
    PauseToken a = pauses.acquire();
    PauseToken b = pauses.acquire();
    CHECK(pauses.isPaused());
    CHECK_EQ(pauses.holderCount(), 2u);

    a.release();
    CHECK(pauses.isPaused());
    b.release();
    CHECK(!pauses.isPaused());
}

TEST(pause_set_release_in_any_order)
{
    PauseSet pauses;
    PauseToken a = pauses.acquire();
    PauseToken b = pauses.acquire();
    PauseToken c = pauses.acquire();

    b.release();
    CHECK_EQ(pauses.holderCount(), 2u);
    c.release();
    a.release();
    CHECK(!pauses.isPaused());
}

TEST(pause_set_release_is_idempotent)
{
    PauseSet pauses;
    PauseToken a = pauses.acquire();
    PauseToken b = pauses.acquire();

    a.release();
    a.release();
    CHECK_EQ(pauses.holderCount(), 1u);
    CHECK(!a.active());
    CHECK(b.active());
}

TEST(pause_set_destructor_releases)
{
    PauseSet pauses;
    {
        PauseToken token = pauses.acquire();
        CHECK(pauses.isPaused());
    }
    CHECK(!pauses.isPaused());
}

TEST(pause_set_moved_token_keeps_one_claim)
{
    PauseSet pauses;
    PauseToken a = pauses.acquire();
    PauseToken b(std::move(a));

    CHECK(!a.active());
    CHECK(b.active());
    CHECK_EQ(pauses.holderCount(), 1u);

    // The moved-from token is inert.
    a.release();
    CHECK_EQ(pauses.holderCount(), 1u);

    b.release();
    CHECK(!pauses.isPaused());
}

TEST(pause_set_move_assignment_releases_the_old_claim)
{
    PauseSet pauses;
    PauseToken a = pauses.acquire();
    PauseToken b = pauses.acquire();
    CHECK_EQ(pauses.holderCount(), 2u);

    a = std::move(b);
    CHECK_EQ(pauses.holderCount(), 1u);
    CHECK(a.active());
    CHECK(!b.active());
}

TEST(pause_set_tokens_move_into_containers)
{
    PauseSet pauses;
    std::vector<PauseToken> tokens;
    for (int i = 0; i < 20; i++) {
        tokens.push_back(pauses.acquire());
    }
    CHECK_EQ(pauses.holderCount(), 20u);

    tokens.erase(tokens.begin() + 3);
    CHECK_EQ(pauses.holderCount(), 19u);

    tokens.clear();
    CHECK(!pauses.isPaused());
}

TEST(pause_set_token_can_outlive_the_set)
{
    PauseToken token;
    {
        PauseSet pauses;
        token = pauses.acquire();
        CHECK(token.active());
    }

    CHECK(!token.active());
    token.release();
}

TEST(pause_set_listener_sees_only_transitions)
{
    PauseSet pauses;
    std::vector<bool> seen;
    pauses.setListener([&seen](bool paused) { seen.push_back(paused); });
    CHECK_EQ(seen.size(), 0u);

    PauseToken a = pauses.acquire();
    PauseToken b = pauses.acquire();
    a.release();
    CHECK_EQ(seen.size(), 1u);

    b.release();
    CHECK_EQ(seen.size(), 2u);
    CHECK(seen[0]);
    CHECK(!seen[1]);
}

TEST(pause_set_listener_can_be_removed)
{
    PauseSet pauses;
    int calls = 0;
    pauses.setListener([&calls](bool) { calls++; });
    pauses.setListener(nullptr);

    PauseToken token = pauses.acquire();
    token.release();
    CHECK_EQ(calls, 0);
}

TEST(pause_set_listener_set_while_paused_gets_the_next_change_only)
{
    PauseSet pauses;
    PauseToken token = pauses.acquire();

    std::vector<bool> seen;
    pauses.setListener([&seen](bool paused) { seen.push_back(paused); });
    CHECK_EQ(seen.size(), 0u);

    token.release();
    CHECK_EQ(seen.size(), 1u);
    CHECK(!seen[0]);
}

TEST(pause_set_listener_may_acquire_without_recursion)
{
    PauseSet pauses;
    std::vector<PauseToken> extra;
    std::vector<bool> seen;
    int depth = 0;
    int maxDepth = 0;

    pauses.setListener([&](bool paused) {
        depth++;
        if (depth > maxDepth) {
            maxDepth = depth;
        }
        seen.push_back(paused);

        // Pause again as soon as the world is told it is running.
        if (!paused && extra.empty()) {
            extra.push_back(pauses.acquire());
        }
        depth--;
    });

    PauseToken token = pauses.acquire();
    token.release();

    CHECK_EQ(maxDepth, 1);
    CHECK(pauses.isPaused());

    // The listener ended on the state the set is in: true, false, true.
    CHECK_EQ(seen.size(), 3u);
    CHECK(seen.back());

    // The listener uses locals that die before `extra` does.
    pauses.setListener(nullptr);
}

TEST(pause_set_listener_may_release_during_a_notification)
{
    PauseSet pauses;
    PauseToken outer = pauses.acquire();
    PauseToken inner;
    std::vector<bool> seen;

    pauses.setListener([&](bool paused) {
        seen.push_back(paused);
        if (!paused && seen.size() == 1) {
            // Pause again, then give that up from the next notification.
            inner = pauses.acquire();
        } else if (paused) {
            inner.release();
        }
    });

    outer.release();

    // false, then the re-pause, then the release of it: false, true, false.
    CHECK(!pauses.isPaused());
    CHECK_EQ(seen.size(), 3u);
    CHECK(!seen[0]);
    CHECK(seen[1]);
    CHECK(!seen[2]);
}

TEST(pause_set_listener_may_destroy_the_set)
{
    PauseSet* pauses = new PauseSet();
    int calls = 0;
    pauses->setListener([&](bool) {
        calls++;
        delete pauses;
        pauses = nullptr;
    });

    PauseToken token = pauses->acquire();
    CHECK_EQ(calls, 1);
    CHECK(pauses == nullptr);
    CHECK(!token.active());
    token.release();
}

TEST(pause_set_listener_may_replace_itself)
{
    PauseSet pauses;
    int first = 0;
    int second = 0;

    pauses.setListener([&](bool) {
        first++;
        pauses.setListener([&](bool) { second++; });
    });

    PauseToken token = pauses.acquire();
    token.release();
    CHECK_EQ(first, 1);
    CHECK_EQ(second, 1);
}

TEST(pause_set_transition_count_counts_both_directions)
{
    PauseSet pauses;
    CHECK_EQ(pauses.transitionCount(), 0u);

    PauseToken a = pauses.acquire();
    CHECK_EQ(pauses.transitionCount(), 1u);

    // Nested holders change nothing.
    PauseToken b = pauses.acquire();
    CHECK_EQ(pauses.transitionCount(), 1u);
    a.release();
    CHECK_EQ(pauses.transitionCount(), 1u);

    b.release();
    CHECK_EQ(pauses.transitionCount(), 2u);

    pauses.acquire().release();
    CHECK_EQ(pauses.transitionCount(), 4u);
}

TEST(pause_set_transition_count_needs_no_listener)
{
    PauseSet pauses;
    pauses.setListener([](bool) {});
    pauses.setListener(nullptr);
    pauses.acquire().release();
    CHECK_EQ(pauses.transitionCount(), 2u);
}

TEST(pause_set_throwing_listener_leaves_the_set_usable)
{
    PauseSet pauses;
    int calls = 0;
    pauses.setListener([&calls](bool) {
        calls++;
        if (calls == 1) {
            throw 1;
        }
    });

    bool caught = false;
    try {
        PauseToken token = pauses.acquire();
    } catch (int) {
        caught = true;
    }
    CHECK(caught);

    // The claim from the failed acquire was given back.
    CHECK(!pauses.isPaused());

    // Later changes are still announced.
    int before = calls;
    PauseToken token = pauses.acquire();
    CHECK(calls > before);
    CHECK(pauses.isPaused());
    pauses.setListener(nullptr);
}

TEST(pause_set_token_destructor_swallows_a_listener_exception)
{
    PauseSet pauses;
    PauseToken token = pauses.acquire();
    pauses.setListener([](bool) { throw 1; });

    // Would end the process if the exception left the destructor.
    token = PauseToken();
    CHECK(!pauses.isPaused());
    pauses.setListener(nullptr);
}
