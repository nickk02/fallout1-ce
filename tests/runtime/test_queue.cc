// Runs the engine's own event queue (src/game/queue.cc). The handler table
// q_func is the queue's public hook: each test points the entries it uses at
// handlers that write to a calls, so the order of calls can be checked.

#include "test_harness.h"

#include "queue_stubs.h"

#include <functional>
#include <string>
#include <vector>

#include "game/queue.h"
#include "plib/gnw/memory.h"

using namespace fallout;
using namespace fallout_test;

namespace {

// What the handlers did, in order: "h<tag>" for a handler call, "f<tag>" for a
// data free.
std::vector<std::string> calls;

// Run by the handler of the event with this tag, before the handler returns.
std::function<void(int tag)> onHandle;

// The result the handler returns for a tag. Zero when missing.
std::vector<int> results;

int tagOf(void* data) { return data != nullptr ? *static_cast<int*>(data) : -1; }

int handler(Object*, void* data)
{
    int tag = tagOf(data);
    calls.push_back("h" + std::to_string(tag));
    if (onHandle) {
        onHandle(tag);
    }
    return tag >= 0 && tag < static_cast<int>(results.size()) ? results[tag] : 0;
}

void freeData(void* data)
{
    calls.push_back("f" + std::to_string(tagOf(data)));
}

int tags[16];

// Event data: the tag is the value, the address is what the queue holds.
int* tag(int value)
{
    tags[value] = value;
    return &tags[value];
}

EventTypeDescription savedTable[EVENT_TYPE_COUNT];

class Scope {
public:
    Scope()
    {
        for (int i = 0; i < EVENT_TYPE_COUNT; i++) {
            savedTable[i] = q_func[i];
        }
        queue_stub_reset();
        queue_init();
        calls.clear();
        onHandle = nullptr;
        results.clear();

        // Two types with logging handlers and a logging free.
        q_func[EVENT_TYPE_SCRIPT].handlerProc = handler;
        q_func[EVENT_TYPE_SCRIPT].freeProc = freeData;
        q_func[EVENT_TYPE_GAME_TIME].handlerProc = handler;
        q_func[EVENT_TYPE_GAME_TIME].freeProc = nullptr;
    }

    ~Scope()
    {
        queue_clear();
        for (int i = 0; i < EVENT_TYPE_COUNT; i++) {
            q_func[i] = savedTable[i];
        }
        onHandle = nullptr;
        queue_stub_reset();
    }
};

int add(int delay, int value, int type = EVENT_TYPE_SCRIPT)
{
    return queue_add(delay, nullptr, tag(value), type);
}

} // namespace

TEST(queue_peek_time_tells_an_empty_queue_from_an_event_at_time_zero)
{
    Scope scope;

    int time = 77;
    CHECK(!queue_peek_time(&time));
    CHECK_EQ(time, 77);

    // The old function reports an empty queue as 0 ...
    CHECK_EQ(queue_next_time(), 0);

    // ... which an event at time 0 reports too. Only peek tells them apart.
    queueStub.now = 0;
    CHECK_EQ(add(0, 1), 0);
    time = 77;
    CHECK(queue_peek_time(&time));
    CHECK_EQ(time, 0);
    CHECK_EQ(queue_next_time(), 0);
}

TEST(queue_peek_time_reports_the_earliest_event)
{
    Scope scope;
    queueStub.now = 100;
    add(50, 1);
    add(10, 2);
    add(30, 3);

    int time = 0;
    CHECK(queue_peek_time(&time));
    CHECK_EQ(time, 110);
}

TEST(queue_process_one_on_an_empty_queue_does_nothing)
{
    Scope scope;

    int result = 55;
    CHECK(!queue_process_one(1000, &result));
    CHECK_EQ(result, 55);
    CHECK_EQ(calls.size(), 0u);
}

TEST(queue_process_one_leaves_a_future_head_alone)
{
    Scope scope;
    queueStub.now = 0;
    add(10, 1);

    int result = 55;
    CHECK(!queue_process_one(9, &result));
    CHECK_EQ(result, 55);
    CHECK_EQ(calls.size(), 0u);

    // The cutoff is inclusive.
    CHECK(queue_process_one(10, &result));
    CHECK_EQ(result, 0);
    CHECK_EQ(calls.size(), 2u);
}

TEST(queue_process_one_accepts_a_null_result_pointer)
{
    Scope scope;
    queueStub.now = 0;
    add(0, 1);

    CHECK(queue_process_one(0, nullptr));
    CHECK_EQ(calls.size(), 2u);
}

TEST(queue_process_one_returns_the_handler_result)
{
    Scope scope;
    queueStub.now = 0;
    results.assign(8, 0);
    results[3] = 9;
    add(0, 3);

    int result = 0;
    CHECK(queue_process_one(0, &result));
    CHECK_EQ(result, 9);
}

TEST(queue_events_at_the_same_time_run_in_the_order_they_were_added)
{
    Scope scope;
    queueStub.now = 0;
    add(5, 1);
    add(5, 2);
    add(5, 3);

    // An earlier event added last still runs first.
    add(2, 4);

    queueStub.now = 5;
    CHECK_EQ(queue_process(), 0);

    const std::vector<std::string> expected = { "h4", "f4", "h1", "f1", "h2", "f2", "h3", "f3" };
    CHECK(calls == expected);
}

TEST(queue_process_uses_the_time_it_captured_when_a_handler_moves_the_clock)
{
    Scope scope;
    queueStub.now = 10;
    add(0, 1);
    add(40, 2);

    // The first handler moves the game time far past the second event.
    onHandle = [](int tag) {
        if (tag == 1) {
            queueStub.now = 1000;
        }
    };

    CHECK_EQ(queue_process(), 0);

    // The second event was due at the later time but not at the captured one.
    const std::vector<std::string> expected = { "h1", "f1" };
    CHECK(calls == expected);
    int time = 0;
    CHECK(queue_peek_time(&time));
    CHECK_EQ(time, 50);
}

TEST(queue_process_one_uses_the_cutoff_it_is_given_not_the_game_time)
{
    Scope scope;
    queueStub.now = 0;
    add(10, 1);

    // The game time is far past the event, but the caller's cutoff is not.
    queueStub.now = 1000;
    CHECK(!queue_process_one(5, nullptr));
    CHECK(queue_process_one(10, nullptr));
}

TEST(queue_events_a_handler_adds_for_the_captured_time_run_in_the_same_call)
{
    Scope scope;
    queueStub.now = 10;
    add(0, 1);

    // Due at the captured time: runs in this call. One tick later: does not.
    onHandle = [](int tag) {
        if (tag == 1) {
            add(0, 2);
            add(1, 3);
        }
    };

    CHECK_EQ(queue_process(), 0);

    const std::vector<std::string> expected = { "h1", "f1", "h2", "f2" };
    CHECK(calls == expected);
    int time = 0;
    CHECK(queue_peek_time(&time));
    CHECK_EQ(time, 11);
}

TEST(queue_a_handler_that_clears_the_queue_ends_the_pass_and_frees_everything_once)
{
    Scope scope;
    queueStub.now = 0;
    add(0, 1);
    add(0, 2);
    add(5, 3);

    onHandle = [](int tag) {
        if (tag == 1) {
            queue_clear();
        }
    };

    CHECK_EQ(queue_process(), 0);

    // The running event is already out of the list. The others were freed by the
    // clear, without their handlers, and the running one after its handler.
    const std::vector<std::string> expected = { "h1", "f2", "f3", "f1" };
    CHECK(calls == expected);
    int time = 0;
    CHECK(!queue_peek_time(&time));
    CHECK_EQ(queueStub.live.size(), 0u);
    CHECK_EQ(queueStub.badFrees, 0);
}

TEST(queue_a_handler_that_removes_another_events_owner_stops_it_from_running)
{
    Scope scope;
    queueStub.now = 0;

    Object owner = {};
    queue_add(0, nullptr, tag(1), EVENT_TYPE_SCRIPT);
    queue_add(0, &owner, tag(2), EVENT_TYPE_SCRIPT);
    queue_add(0, nullptr, tag(3), EVENT_TYPE_SCRIPT);

    onHandle = [&owner](int tag) {
        if (tag == 1) {
            queue_remove(&owner);
        }
    };

    CHECK_EQ(queue_process(), 0);

    // Event 2 never ran, but its data was freed.
    const std::vector<std::string> expected = { "h1", "f2", "f1", "h3", "f3" };
    CHECK(calls == expected);
    CHECK_EQ(queueStub.live.size(), 0u);
    CHECK_EQ(queueStub.badFrees, 0);
}

TEST(queue_a_handler_may_run_the_queue_again)
{
    Scope scope;
    queueStub.now = 0;
    add(0, 1);
    add(0, 2);
    add(0, 3);

    // The first handler runs one more event itself, inside its own call.
    onHandle = [](int tag) {
        if (tag == 1) {
            CHECK(queue_process_one(0, nullptr));
        }
    };

    CHECK_EQ(queue_process(), 0);

    const std::vector<std::string> expected = { "h1", "h2", "f2", "f1", "h3", "f3" };
    CHECK(calls == expected);
    CHECK_EQ(queueStub.live.size(), 0u);
    CHECK_EQ(queueStub.badFrees, 0);
}

TEST(queue_a_nonzero_result_stops_the_pass_after_that_event_is_freed)
{
    Scope scope;
    queueStub.now = 0;
    results.assign(8, 0);
    results[2] = 4;
    add(0, 1);
    add(0, 2);
    add(0, 3);

    CHECK_EQ(queue_process(), 4);

    // Event 3 was due, and stays queued.
    const std::vector<std::string> expected = { "h1", "f1", "h2", "f2" };
    CHECK(calls == expected);
    int time = 0;
    CHECK(queue_peek_time(&time));

    // The next pass picks it up.
    calls.clear();
    CHECK_EQ(queue_process(), 0);
    const std::vector<std::string> next = { "h3", "f3" };
    CHECK(calls == next);
}

TEST(queue_data_is_freed_after_the_handler_and_the_node_is_given_back)
{
    Scope scope;
    queueStub.now = 0;

    // Data from the engine's allocator, with the engine's own free.
    q_func[EVENT_TYPE_SCRIPT].freeProc = mem_free;
    int* data = static_cast<int*>(mem_malloc(sizeof(int)));
    *data = 5;
    int before = queueStub.allocations;
    queue_add(0, nullptr, data, EVENT_TYPE_SCRIPT);

    // The node itself came from the same allocator.
    CHECK_EQ(queueStub.allocations, before + 1);
    CHECK_EQ(queueStub.live.size(), 2u);

    onHandle = [data](int) {
        // Not freed yet while the handler runs.
        CHECK(queueStub.live.count(data) == 1);
    };

    CHECK_EQ(queue_process(), 0);
    CHECK_EQ(queueStub.live.size(), 0u);
    CHECK_EQ(queueStub.badFrees, 0);
}

TEST(queue_an_event_type_without_a_free_leaves_its_data_alone)
{
    Scope scope;
    queueStub.now = 0;
    add(0, 7, EVENT_TYPE_GAME_TIME);

    CHECK_EQ(queue_process(), 0);

    const std::vector<std::string> expected = { "h7" };
    CHECK(calls == expected);
    CHECK_EQ(queueStub.live.size(), 0u);
}

TEST(queue_process_matches_a_loop_of_queue_process_one)
{
    std::vector<std::string> byProcess;
    std::vector<std::string> byLoop;
    int resultByProcess = 0;
    int resultByLoop = 0;

    for (int run = 0; run < 2; run++) {
        Scope scope;
        queueStub.now = 3;
        results.assign(8, 0);
        results[4] = 2;
        add(0, 1);
        add(0, 4);
        add(0, 5);
        add(9, 6);
        add(0, 2);

        if (run == 0) {
            resultByProcess = queue_process();
            byProcess = calls;
        } else {
            int time = queueStub.now;
            int result = 0;
            while (result == 0 && queue_process_one(time, &result)) {
            }
            resultByLoop = result;
            byLoop = calls;
        }
    }

    CHECK(byProcess == byLoop);
    CHECK_EQ(resultByProcess, resultByLoop);
    CHECK_EQ(resultByProcess, 2);
}

TEST(queue_each_event_type_runs_its_own_handler_table_entry)
{
    Scope scope;
    queueStub.now = 0;

    static std::vector<std::string> seen;
    seen.clear();

    q_func[EVENT_TYPE_GAME_TIME].handlerProc = [](Object*, void*) {
        seen.push_back("gametime");
        return 0;
    };

    add(0, 1, EVENT_TYPE_SCRIPT);
    add(0, 2, EVENT_TYPE_GAME_TIME);

    CHECK_EQ(queue_process(), 0);

    const std::vector<std::string> expected = { "h1", "f1" };
    CHECK(calls == expected);
    CHECK_EQ(seen.size(), 1u);
}
