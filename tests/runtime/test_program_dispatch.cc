#include "test_harness.h"

#include <vector>

#include "sim/program_dispatch.h"

using namespace fallout;

namespace {

struct FakeProgram {
    int id;
    bool exited = false;
    int runs = 0;
};

struct FakeNode {
    FakeProgram* program;
    FakeNode* next;
};

// Three programs in a list, the way the interpreter keeps them.
struct FakeList {
    FakeProgram a { 1 };
    FakeProgram b { 2 };
    FakeProgram c { 3 };
    FakeNode nc { &c, nullptr };
    FakeNode nb { &b, &nc };
    FakeNode na { &a, &nb };
    std::vector<int> removed;

    void dispatch(const FakeProgram* only, bool all = false)
    {
        auto run = [](FakeProgram* p) { p->runs++; };
        auto exited = [](FakeProgram* p) { return p->exited; };
        auto remove = [this](FakeNode* node) { removed.push_back(node->program->id); };
        if (all) {
            program_dispatch_all(&na, run, exited, remove);
        } else {
            program_dispatch_one(&na, only, run, exited, remove);
        }
    }
};

} // namespace

TEST(program_dispatch_without_a_selection_runs_every_program)
{
    FakeList list;
    list.b.exited = true;
    list.dispatch(nullptr, true);

    CHECK_EQ(list.a.runs, 1);
    CHECK_EQ(list.b.runs, 1);
    CHECK_EQ(list.c.runs, 1);
    CHECK_EQ(list.removed.size(), 1u);
    CHECK_EQ(list.removed[0], 2);
}

TEST(program_dispatch_with_a_selection_leaves_the_siblings_alone)
{
    FakeList list;
    list.a.exited = true;
    list.c.exited = true;
    list.dispatch(&list.b);

    CHECK_EQ(list.a.runs, 0);
    CHECK_EQ(list.b.runs, 1);
    CHECK_EQ(list.c.runs, 0);

    // Exited siblings are not cleaned up by someone else's pass.
    CHECK_EQ(list.removed.size(), 0u);
}

TEST(program_dispatch_selection_removes_only_the_selected_exited_program)
{
    FakeList list;
    list.b.exited = true;
    list.c.exited = true;
    list.dispatch(&list.b);

    CHECK_EQ(list.removed.size(), 1u);
    CHECK_EQ(list.removed[0], 2);
    CHECK_EQ(list.c.runs, 0);
}

TEST(program_dispatch_selection_works_at_the_head_and_the_tail)
{
    FakeList list;
    list.dispatch(&list.a);
    list.dispatch(&list.c);

    CHECK_EQ(list.a.runs, 1);
    CHECK_EQ(list.b.runs, 0);
    CHECK_EQ(list.c.runs, 1);
}

TEST(program_dispatch_selection_not_in_the_list_runs_nothing)
{
    FakeList list;
    FakeProgram stranger { 9 };
    list.dispatch(&stranger);

    CHECK_EQ(list.a.runs + list.b.runs + list.c.runs, 0);
    CHECK_EQ(stranger.runs, 0);
    CHECK_EQ(list.removed.size(), 0u);
}

TEST(program_dispatch_selection_ignores_a_node_freed_while_it_runs)
{
    // The selected program removes the node after it in the list. The pass
    // must stop at the match and never read that node.
    FakeProgram a { 1 };
    FakeProgram b { 2 };
    FakeNode* nb = new FakeNode { &b, nullptr };
    FakeNode na { &a, nb };

    program_dispatch_one(
        &na, &a,
        [&](FakeProgram* p) {
            p->runs++;
            delete na.next;
            na.next = nullptr;
        },
        [](FakeProgram*) { return false; },
        [](FakeNode*) {});

    CHECK_EQ(a.runs, 1);
}

TEST(program_dispatch_null_selection_is_a_no_op_not_all)
{
    FakeList list;
    list.a.exited = true;
    list.b.exited = true;
    list.dispatch(nullptr);

    CHECK_EQ(list.a.runs + list.b.runs + list.c.runs, 0);
    CHECK_EQ(list.removed.size(), 0u);
}
