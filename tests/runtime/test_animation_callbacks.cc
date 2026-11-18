#include "test_harness.h"

#include <functional>
#include <string>
#include <vector>

#include "sim/animation_callbacks.h"
#include "sim/animation_lease.h"

using namespace fallout;

namespace {
class Probe final : public AnimationPayload {
public:
    Probe(int& disposed, std::function<int()> run)
        : disposed(disposed)
        , run(std::move(run))
    {
    }
    ~Probe() override { ++disposed; }
    int invoke() override { return run(); }
    int& disposed;
    std::function<int()> run;
};
uint64_t epoch = 7;
uint64_t liveEpoch() { return epoch; }
std::vector<std::string> messages;
std::vector<int> types;
int present(void*, const char* text, int type)
{
    messages.emplace_back(text);
    types.push_back(type);
    return 0;
}
} // namespace

TEST(animation_callbacks_registration_failure_and_later_failure_dispose)
{
    AnimationCallbacks callbacks;
    int disposed = 0, called = 0;
    auto payload = [&] { return std::make_unique<Probe>(disposed, [&] { return ++called; }); };
    CHECK(!callbacks.add(0, 0, payload()));
    CHECK_EQ(disposed, 1);
    callbacks.begin(1);
    CHECK(!callbacks.add(2, 0, payload()));
    CHECK(!callbacks.add(1, 40, payload()));
    CHECK_EQ(disposed, 3);
    CHECK(callbacks.add(1, 0, payload()));
    CHECK(!callbacks.add(1, 0, payload()));
    CHECK_EQ(disposed, 4);
    // Engine registration cleanup after a later descriptor fails.
    callbacks.clear();
    callbacks.clear();
    CHECK_EQ(disposed, 5);
    CHECK_EQ(called, 0);
}

TEST(animation_callbacks_sync_dispatch_and_duplicate_claim)
{
    AnimationCallbacks callbacks;
    callbacks.begin(1);
    int disposed = 0, called = 0;
    CHECK(callbacks.add(1, 0, std::make_unique<Probe>(disposed, [&] {
        ++called;
        CHECK_EQ(callbacks.dispatch(1, 0, true).result, 0);
        CHECK_EQ(disposed, 0);
        return 12;
    })));
    // register_end can dispatch before returning to the registering caller.
    CHECK_EQ(callbacks.dispatch(1, 0, true).result, 12);
    CHECK_EQ(called, 1);
    CHECK_EQ(disposed, 1);
    CHECK(callbacks.owns(1, 0));
    callbacks.dispatch(1, 0, true);
    callbacks.clear();
    CHECK_EQ(disposed, 1);
}

TEST(animation_callbacks_skip_forced_and_reset_disposal)
{
    AnimationCallbacks callbacks;
    callbacks.begin(1);
    int disposed = 0, called = 0;
    for (int i = 0; i < 3; ++i) {
        CHECK(callbacks.add(1, i, std::make_unique<Probe>(disposed, [&] { ++called; return 0; }), i == 1));
    }
    callbacks.dispatch(1, 0, false); // nonforced teardown skips invocation
    callbacks.dispatch(1, 1, false); // forced teardown invokes once
    callbacks.clear(); // reset disposes the remaining descriptor
    CHECK_EQ(called, 1);
    CHECK_EQ(disposed, 3);
}

TEST(animation_callbacks_reset_slot_reuse_pins_old_invocation)
{
    AnimationCallbacks callbacks;
    callbacks.begin(1);
    int disposed = 0, called = 0;
    CHECK(callbacks.add(1, 0, std::make_unique<Probe>(disposed, [&] {
        ++called;
        callbacks.clear();
        callbacks.begin(2);
        CHECK(callbacks.add(2, 0, std::make_unique<Probe>(disposed, [&] { ++called; return 0; })));
        CHECK_EQ(disposed, 0);
        callbacks.dispatch(1, 0, true);
        return 0;
    })));
    const auto oldDelivery = callbacks.dispatch(1, 0, true);
    CHECK(!oldDelivery.current);
    CHECK(!callbacks.owns(1, 0));
    CHECK(callbacks.owns(2, 0));
    CHECK_EQ(disposed, 1);
    CHECK_EQ(called, 1);
    callbacks.dispatch(2, 0, true);
    CHECK_EQ(disposed, 2);
    CHECK_EQ(called, 2);
}

TEST(animation_taunt_distinct_text_and_type_snapshot)
{
    ObjectRegistry registry;
    int actor;
    registry.create(&actor);
    epoch = 7;
    messages.clear();
    types.clear();
    AnimationCallbacks callbacks;
    callbacks.begin(1);
    char scratch[] = "first";
    CHECK(callbacks.add(1, 0, std::make_unique<AnimationTaunt>(registry, object_ref_make(registry, &actor, epoch), scratch, 2, liveEpoch, present)));
    scratch[0] = 'x';
    CHECK(callbacks.add(1, 1, std::make_unique<AnimationTaunt>(registry, object_ref_make(registry, &actor, epoch), "second", 4, liveEpoch, present)));
    callbacks.dispatch(1, 0, true);
    callbacks.dispatch(1, 1, true);
    CHECK_EQ(messages.size(), 2u);
    CHECK_EQ(messages[0], "first");
    CHECK_EQ(messages[1], "second");
    CHECK_EQ(types[0], 2);
    CHECK_EQ(types[1], 4);
}

TEST(animation_taunt_actor_reuse_and_live_epoch_reject)
{
    ObjectRegistry registry;
    int actor;
    registry.create(&actor);
    epoch = 7;
    messages.clear();
    AnimationCallbacks callbacks;
    callbacks.begin(1);
    CHECK(callbacks.add(1, 0, std::make_unique<AnimationTaunt>(registry, object_ref_make(registry, &actor, epoch), "old", 0, liveEpoch, present)));
    registry.destroy(&actor);
    registry.create(&actor);
    callbacks.dispatch(1, 0, true);
    CHECK(messages.empty());
    CHECK(callbacks.add(1, 1, std::make_unique<AnimationTaunt>(registry, object_ref_make(registry, &actor, epoch), "persistent", 0, liveEpoch, present)));
    ++epoch;
    callbacks.dispatch(1, 1, true);
    CHECK(messages.empty());
}

TEST(animation_callbacks_disposal_reentry_invalidates_delivery)
{
    struct ResetOnDispose final : AnimationPayload {
        explicit ResetOnDispose(AnimationCallbacks& callbacks)
            : callbacks(callbacks)
        {
        }
        ~ResetOnDispose() override { callbacks.begin(2); }
        int invoke() override { return 0; }
        AnimationCallbacks& callbacks;
    };
    AnimationCallbacks callbacks;
    callbacks.begin(1);
    CHECK(callbacks.add(1, 0, std::make_unique<ResetOnDispose>(callbacks)));
    const auto delivery = callbacks.dispatch(1, 0, true);
    CHECK_EQ(delivery.result, 0);
    CHECK(!delivery.current);
    CHECK(!callbacks.owns(1, 0));
}

namespace {
class DisposeAction final : public AnimationPayload {
public:
    explicit DisposeAction(std::function<void()> dispose)
        : dispose(std::move(dispose))
    {
    }
    ~DisposeAction() override { dispose(); }
    int invoke() override { return 0; }
    std::function<void()> dispose;
};
} // namespace

TEST(animation_registration_cleanup_destructor_resets_current_index)
{
    AnimationCallbacks callbacks;
    AnimationLeaseSlot lease;
    int currentIndex = 3, descriptionCount = 1, disposed = 0;
    callbacks.begin(1);
    CHECK(lease.begin());
    CHECK(callbacks.add(1, 0, std::make_unique<DisposeAction>([&] {
        ++disposed;
        // Executes the same cleanup operation used by anim.cc, including its
        // engine index/count and lease writes, rather than bank.clear alone.
        CHECK_EQ(currentIndex, -1);
        CHECK_EQ(descriptionCount, 0);
        CHECK(!lease.claimEnd().valid);
        discard_animation_registration(callbacks, lease, currentIndex, descriptionCount);
    })));
    discard_animation_registration(callbacks, lease, currentIndex, descriptionCount);
    CHECK_EQ(currentIndex, -1);
    CHECK_EQ(descriptionCount, 0);
    CHECK_EQ(disposed, 1);
}

TEST(animation_registration_cleanup_destructor_replaces_same_slot)
{
    AnimationCallbacks callbacks;
    AnimationLeaseSlot lease;
    int currentIndex = 3, descriptionCount = 1, disposed = 0, invoked = 0;
    callbacks.begin(1);
    CHECK(lease.begin());
    CHECK(callbacks.add(1, 0, std::make_unique<DisposeAction>([&] {
        ++disposed;
        CHECK_EQ(currentIndex, -1);
        CHECK(!lease.claimEnd().valid);
        currentIndex = 3;
        descriptionCount = 1;
        CHECK(lease.begin());
        callbacks.begin(2);
        CHECK(callbacks.add(2, 0, std::make_unique<Probe>(disposed, [&] { ++invoked; return 0; })));
    })));
    discard_animation_registration(callbacks, lease, currentIndex, descriptionCount);
    CHECK_EQ(currentIndex, 3);
    CHECK_EQ(descriptionCount, 1);
    CHECK_EQ(disposed, 1);
    CHECK(callbacks.owns(2, 0));
    CHECK(lease.claimEnd().valid);
    CHECK(callbacks.dispatch(2, 0, true).current);
    CHECK_EQ(invoked, 1);
    CHECK_EQ(disposed, 2);
}

TEST(animation_callbacks_begin_destructor_replacement_survives)
{
    AnimationCallbacks callbacks;
    callbacks.begin(1);
    int disposed = 0;
    CHECK(callbacks.add(1, 0, std::make_unique<DisposeAction>([&] {
        callbacks.begin(3);
        CHECK(callbacks.add(3, 0, std::make_unique<Probe>(disposed, [] { return 0; })));
    })));
    callbacks.begin(2);
    // The outer begin must not restore serial 2 after the destructor's begin.
    CHECK(callbacks.owns(3, 0));
    CHECK(!callbacks.owns(2, 0));
    callbacks.clear();
    CHECK_EQ(disposed, 1);
}
