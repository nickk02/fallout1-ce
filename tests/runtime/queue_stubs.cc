// Stands in for everything src/game/queue.cc reaches outside itself, so that the
// real queue can be linked into an asset-free test. None of the event handlers
// listed here is ever called by the tests: they replace the entries of the
// queue's own handler table (q_func) with ones that record what happened. The
// allocator and game_time() are the two stubs that matter to the queue's
// behavior; both are counted or controlled by the test.

#include "queue_stubs.h"

#include <stdlib.h>

#include "game/actions.h"
#include "game/critter.h"
#include "game/display.h"
#include "game/game.h"
#include "game/item.h"
#include "game/map.h"
#include "game/message.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/scripts.h"
#include "plib/gnw/memory.h"

namespace fallout_test {

QueueStubState queueStub;

void queue_stub_reset()
{
    for (void* ptr : queueStub.live) {
        free(ptr);
    }
    queueStub = QueueStubState();
}

} // namespace fallout_test

namespace fallout {

Object* obj_dude = nullptr;
MessageList misc_message_file;

// The engine's allocator, with bookkeeping.
void* mem_malloc(size_t size)
{
    void* ptr = malloc(size);
    fallout_test::queueStub.live.insert(ptr);
    fallout_test::queueStub.allocations++;
    return ptr;
}

void mem_free(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }

    if (fallout_test::queueStub.live.erase(ptr) == 0) {
        fallout_test::queueStub.badFrees++;
        return;
    }

    fallout_test::queueStub.frees++;
    free(ptr);
}

int game_time()
{
    return fallout_test::queueStub.now;
}

// Same rule as the engine's: a time of 0 is stored as 1.
void set_game_time(int time)
{
    if (time == 0) {
        time = 1;
    }

    fallout_test::queueStub.now = time;
}

// Handlers and helpers that queue.cc names but the tests never reach.
int item_d_process(Object*, void*) { return 0; }
int item_d_clear(Object*, void*) { return 0; }
int item_d_load(DB_FILE*, void**) { return -1; }
int item_d_save(DB_FILE*, void*) { return -1; }
int item_wd_process(Object*, void*) { return 0; }
int item_wd_clear(Object*, void*) { return 0; }
int item_wd_load(DB_FILE*, void**) { return -1; }
int item_wd_save(DB_FILE*, void*) { return -1; }
int item_m_trickle(Object*, void*) { return 0; }
int item_m_turn_off_from_queue(Object*, void*) { return 0; }
int critter_wake_up(Object*, void*) { return 0; }
int critter_wake_clear(Object*, void*) { return 0; }
int critter_sneak_check(Object*, void*) { return 0; }
int critter_sneak_clear(Object*, void*) { return 0; }
int critter_check_poison(Object*, void*) { return 0; }
int critter_process_rads(Object*, void*) { return 0; }
int critter_load_rads(DB_FILE*, void**) { return -1; }
int critter_save_rads(DB_FILE*, void*) { return -1; }
int script_q_process(Object*, void*) { return 0; }
int script_q_load(DB_FILE*, void**) { return -1; }
int script_q_save(DB_FILE*, void*) { return -1; }
int gtime_q_process(Object*, void*) { return 0; }
int scr_map_q_process(Object*, void*) { return 0; }

int obj_destroy(Object*) { return 0; }
Object* obj_find_first() { return nullptr; }
Object* obj_find_next() { return nullptr; }
Object* obj_top_environment(Object*) { return nullptr; }
Object* inven_find_id(Object*, int) { return nullptr; }
int action_explode(int, int, int, int, Object*, bool) { return 0; }
void display_print(char*) { }
bool message_search(MessageList*, MessageListItem*) { return false; }
int db_freadInt(DB_FILE*, int*) { return -1; }
int db_fwriteInt(DB_FILE*, int) { return -1; }

} // namespace fallout
