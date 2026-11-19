#ifndef FALLOUT_SIM_PROGRAM_DISPATCH_H_
#define FALLOUT_SIM_PROGRAM_DISPATCH_H_

namespace fallout {

namespace program_dispatch_detail {

    // `all` visits every node. Otherwise only the node holding `only`.
    template <typename Node, typename Program, typename Run, typename Exited, typename Remove>
    void pass(Node* head, bool all, const Program* only, Run run, Exited exited, Remove remove)
    {
        Node* curr = head;
        while (curr != nullptr) {
            Node* next = curr->next;
            if (all || curr->program == only) {
                if (curr->program != nullptr) {
                    run(curr->program);
                    if (exited(curr->program)) {
                        remove(curr);
                    }
                }

                // The match was handled. `next` may have gone during `run`.
                if (!all) {
                    return;
                }
            }
            curr = next;
        }
    }

} // namespace program_dispatch_detail

// One pass over every program in the list, which is what updatePrograms() does.
// `run` interprets a program. `exited` tells whether it has finished, and
// `remove` unlinks and frees the node. The next node is read before `run`, as the
// original loop did.
template <typename Node, typename Run, typename Exited, typename Remove>
void program_dispatch_all(Node* head, Run run, Exited exited, Remove remove)
{
    program_dispatch_detail::pass(head, true, static_cast<decltype(head->program)>(nullptr), run, exited, remove);
}

// The same pass for the node holding `only`; its siblings are left alone. A null
// `only` does nothing. It never means "all".
template <typename Node, typename Program, typename Run, typename Exited, typename Remove>
void program_dispatch_one(Node* head, const Program* only, Run run, Exited exited, Remove remove)
{
    if (only == nullptr) {
        return;
    }

    program_dispatch_detail::pass(head, false, only, run, exited, remove);
}

} // namespace fallout

#endif /* FALLOUT_SIM_PROGRAM_DISPATCH_H_ */
