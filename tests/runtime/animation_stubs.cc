// External resource seams for the real anim.cc. Unsupported art/sound/script
// execution fails the current test instead of pretending the resource exists.
#include "animation_stubs.h"
#include "test_harness.h"

#include <stdlib.h>

#include "game/anim.h"
#include "game/art.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/free_move.h"
#include "game/game.h"
#include "game/gconfig.h"
#include "game/gsound.h"
#include "game/intface.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/roll.h"
#include "game/scripts.h"
#include "game/stat.h"
#include "game/tile.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/memory.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/vcr.h"

namespace {
fallout::CombatAnimationLeases leases;
void unsupported(const char* seam)
{
    fallout_test::report_failure(__FILE__, __LINE__, seam);
}
void dirty(fallout::Rect* rect)
{
    if (rect != nullptr) *rect = { 1, 2, 3, 4 };
}
}

namespace fallout_test {
AnimationStubState animationStub;
void animation_stub_reset()
{
    animationStub = {};
    leases.reset();
    fallout::combat_state = 0;
    fallout::combat_free_move = 0;
    fallout::obj_dude = nullptr;
    fallout::game_user_wants_to_quit = 0;
}
void animation_stub_set_combat(bool active) { fallout::combat_state = active ? 1 : 0; }
int animation_stub_outstanding_leases() { return leases.count(); }
}

namespace fallout {
Config game_config {};
unsigned int combat_state = 0;
int combat_free_move = 0;
int game_user_wants_to_quit = 0;
int map_elevation = 0;
Object* obj_dude = nullptr;
TileData* square[ELEVATION_COUNT] {};
int off_tile[2][6] {};
unsigned char keys[SDL_NUM_SCANCODES] {};
Rect scr_size { 0, 0, 639, 479 };

CombatAnimationLease combat_anim_begin()
{
    ++fallout_test::animationStub.leaseBegins;
    return leases.begin();
}
void combat_anim_finished(CombatAnimationLease lease)
{
    if (leases.finish(lease)) ++fallout_test::animationStub.leaseFinishes;
    if (fallout_test::animationStub.leaseFinishedHook != nullptr) fallout_test::animationStub.leaseFinishedHook();
}
bool combat_is_realtime() { return false; }
void free_move_yield(Object* actor)
{
    ++fallout_test::animationStub.yields;
    if (fallout_test::animationStub.yieldHook != nullptr) fallout_test::animationStub.yieldHook(actor);
}
void tile_refresh_rect(Rect*, int) { ++fallout_test::animationStub.refreshes; }
int debug_printf(const char*, ...) { return 0; }
unsigned int get_time() { return fallout_test::animationStub.now; }
unsigned int get_bk_time() { return fallout_test::animationStub.now; }
unsigned int elapsed_tocks(unsigned int a, unsigned int b) { return a - b; }
int vcr_status() { return 0; }
void mouse_get_position(int* x, int* y)
{
    *x = 0;
    *y = 0;
}
void* mem_malloc(size_t size) { return malloc(size); }
void mem_free(void* ptr) { free(ptr); }
bool config_get_value(Config*, const char*, const char*, int*) { return false; }
bool configGetBool(Config*, const char*, const char*, bool*) { return false; }
int perk_level(int) { return 0; }
int stat_level(Object*, int) { return 0; }
int item_mp_cost(Object*, int, bool) { return 1; }
int roll_random(int min, int) { return min; }
bool is_pc_flag(int) { return false; }
void pc_flag_off(int) { }
int intface_get_attack(int* mode, bool* aiming)
{
    *mode = 0;
    *aiming = false;
    return 0;
}
void intface_update_move_points(int, int) { }
bool critter_is_dead(Object*) { return false; }
bool critter_is_prone(Object*) { return false; }
int critter_body_type(Object*) { return 0; }
int critter_compute_ap_from_distance(Object*, int distance) { return distance; }

// Object backend: field writes are observable, but no scripts, maps or rendering
// run. Objects must be registered explicitly by the fixture that owns storage.
void obj_bound(Object*, Rect* rect) { dirty(rect); }
int obj_offset(Object* object, int x, int y, Rect* rect)
{
    object->x += x;
    object->y += y;
    dirty(rect);
    ++fallout_test::animationStub.objectWrites;
    return 0;
}
int obj_change_fid(Object* object, int fid, Rect* rect)
{
    object->fid = fid;
    dirty(rect);
    ++fallout_test::animationStub.objectWrites;
    return 0;
}
int obj_set_frame(Object* object, int frame, Rect* rect)
{
    object->frame = frame;
    dirty(rect);
    ++fallout_test::animationStub.objectWrites;
    return 0;
}
int obj_inc_frame(Object* object, Rect* rect) { return obj_set_frame(object, object->frame + 1, rect); }
int obj_dec_frame(Object* object, Rect* rect) { return obj_set_frame(object, object->frame - 1, rect); }
int obj_set_rotation(Object* object, int rotation, Rect* rect)
{
    object->rotation = rotation;
    dirty(rect);
    ++fallout_test::animationStub.objectWrites;
    return 0;
}
int obj_move_to_tile(Object* object, int tile, int elevation, Rect* rect)
{
    object->tile = tile;
    object->elevation = elevation;
    dirty(rect);
    ++fallout_test::animationStub.objectWrites;
    return 0;
}
int obj_set_light(Object* object, int distance, int intensity, Rect* rect)
{
    object->lightDistance = distance;
    object->lightIntensity = intensity;
    dirty(rect);
    ++fallout_test::animationStub.objectWrites;
    return 0;
}
int obj_turn_on(Object* object, Rect* rect)
{
    object->flags &= ~OBJECT_HIDDEN;
    dirty(rect);
    return 0;
}
int obj_turn_off(Object* object, Rect* rect)
{
    object->flags |= OBJECT_HIDDEN;
    dirty(rect);
    return 0;
}
int obj_toggle_flat(Object* object, Rect* rect)
{
    object->flags ^= OBJECT_FLAT;
    dirty(rect);
    return 0;
}
int obj_turn_on_outline(Object*, Rect* rect)
{
    dirty(rect);
    return 0;
}
int obj_turn_off_outline(Object*, Rect* rect)
{
    dirty(rect);
    return 0;
}
int obj_turn_on_light(Object*, Rect* rect)
{
    dirty(rect);
    return 0;
}
int obj_turn_off_light(Object*, Rect* rect)
{
    dirty(rect);
    return 0;
}
int obj_erase_object(Object*, Rect* rect)
{
    dirty(rect);
    unsupported("obj_erase_object: storage ownership requires a fixture seam");
    return -1;
}
Object* obj_blocking_at(Object*, int, int)
{
    unsupported("obj_blocking_at");
    return nullptr;
}
Object* obj_find_first_at(int)
{
    unsupported("obj_find_first_at");
    return nullptr;
}
Object* obj_find_next_at()
{
    unsupported("obj_find_next_at");
    return nullptr;
}
bool obj_is_locked(Object*) { return false; }
int obj_use_door(Object*, Object*, int)
{
    unsupported("obj_use_door");
    return -1;
}
int obj_dist(Object*, Object*)
{
    unsupported("obj_dist");
    return 0;
}
int proto_ptr(int, Proto** proto)
{
    *proto = nullptr;
    unsupported("proto_ptr");
    return -1;
}
bool scr_chk_spatials_in(Object*, int, int)
{
    unsupported("scr_chk_spatials_in");
    return false;
}

static char dummyArt;
static char dummyKey;
Art* art_ptr_lock(int, CacheEntry** key)
{
    if (fallout_test::animationStub.art) {
        *key = reinterpret_cast<CacheEntry*>(&dummyKey);
        return reinterpret_cast<Art*>(&dummyArt);
    }
    *key = nullptr;
    unsupported("art_ptr_lock");
    return nullptr;
}
int art_ptr_unlock(CacheEntry*)
{
    if (!fallout_test::animationStub.art) unsupported("art_ptr_unlock");
    return 0;
}
bool art_exists(int)
{
    if (fallout_test::animationStub.art) return true;
    unsupported("art_exists");
    return false;
}
int art_get_base_name(int, int, char* out)
{
    *out = '\0';
    unsupported("art_get_base_name");
    return -1;
}
int art_frame_fps(Art*)
{
    if (!fallout_test::animationStub.art) unsupported("art_frame_fps");
    return fallout_test::animationStub.art ? 10 : 1;
}
int art_frame_max_frame(Art*)
{
    if (!fallout_test::animationStub.art) unsupported("art_frame_max_frame");
    return fallout_test::animationStub.art ? 2 : 1;
}
int art_frame_action_frame(Art*)
{
    if (!fallout_test::animationStub.art) unsupported("art_frame_action_frame");
    return 0;
}
int art_frame_hot(Art*, int, int, int* x, int* y)
{
    *x = *y = 0;
    if (!fallout_test::animationStub.art) unsupported("art_frame_hot");
    return 0;
}
int art_frame_offset(Art*, int, int* x, int* y)
{
    *x = *y = 0;
    if (!fallout_test::animationStub.art) unsupported("art_frame_offset");
    return 0;
}
int art_id(int type, int frm, int anim, int weapon, int rotation) { return (type << 24) | frm | (anim << 16) | (weapon << 12) | (rotation << 28); }
Sound* gsound_load_sound_volume(const char*, Object*, int)
{
    unsupported("gsound_load_sound_volume");
    return nullptr;
}
void gsound_delete_sfx(Sound*) { unsupported("gsound_delete_sfx"); }
int gsnd_anim_sound(Sound*, void*)
{
    unsupported("gsnd_anim_sound");
    return 0;
}
int gsound_compute_relative_volume(Object*) { return 0; }
char* gsnd_build_character_sfx_name(Object*, int, int)
{
    unsupported("gsnd_build_character_sfx_name");
    return nullptr;
}
int tile_coord(int, int* x, int* y, int)
{
    *x = *y = 0;
    unsupported("tile_coord");
    return -1;
}
int tile_num(int, int, int, bool)
{
    unsupported("tile_num");
    return -1;
}
int tile_num_in_direction(int, int, int)
{
    unsupported("tile_num_in_direction");
    return -1;
}
int tile_dir(int, int)
{
    unsupported("tile_dir");
    return 0;
}
int square_num(int, int, int)
{
    unsupported("square_num");
    return -1;
}
}
