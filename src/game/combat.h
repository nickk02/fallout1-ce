#ifndef FALLOUT_GAME_COMBAT_H_
#define FALLOUT_GAME_COMBAT_H_

#include "game/anim.h"
#include "game/combat_defs.h"
#include "game/message.h"
#include "game/object_types.h"
#include "game/party.h"
#include "game/proto_types.h"
#include "plib/db/db.h"
#include "sim/pause_set.h"

namespace fallout {

extern unsigned int combat_state;
extern STRUCT_664980* gcsd;
extern bool combat_call_display;
extern int cf_table[WEAPON_CRITICAL_FAILURE_TYPE_COUNT][WEAPON_CRITICAL_FAILURE_EFFECT_COUNT];

extern MessageList combat_message_file;
extern Object* combat_turn_obj;
extern int combat_exps;
extern int combat_free_move;

int combat_init();
void combat_reset();
void combat_exit();
int find_cid(int start, int cid, Object** critterList, int critterListLength);
int combat_load(DB_FILE* stream);
int combat_save(DB_FILE* stream);
Object* combat_whose_turn();
void combat_data_init(Object* obj);
void combat_over_from_load();
void combat_give_exps(int exp_points);
int combat_in_range(Object* critter);
void combat_end();
void combat_turn_run();
void combat_end_turn();
void combat(STRUCT_664980* attack);
void combat_ctd_init(Attack* attack, Object* attacker, Object* defender, int hitMode, int hitLocation);
int combat_attack(Object* a1, Object* a2, int hitMode, int location);
int combat_bullet_start(const Object* a1, const Object* a2);
void compute_explosion_on_extras(Attack* attack, int a2, bool isGrenade, int a4);
int determine_to_hit(Object* a1, Object* a2, int hitLocation, int hitMode);
int determine_to_hit_no_range(Object* a1, Object* a2, int hitLocation, int hitMode);
void death_checks(Attack* attack);
void apply_damage(Attack* attack, bool animated);
void combat_display(Attack* attack);
void combat_anim_begin();
void combat_anim_finished();
int combat_check_bad_shot(Object* attacker, Object* defender, int hitMode, bool aiming);
bool combat_to_hit(Object* target, int* accuracy);
void combat_attack_this(Object* a1);
void combat_outline_on();
void combat_outline_off();
void combat_highlight_change();
bool combat_is_shot_blocked(Object* a1, int from, int to, Object* a4, int* a5);
int combat_player_knocked_out_by();
int combat_explode_scenery(Object* a1, Object* a2);
void combat_delete_critter(Object* obj);

// `true` while fights run in real time instead of in turns.
bool combat_is_realtime();

// Switches between real-time fights and the original turn-based ones. Set once
// at start from `REALTIME_COMBAT` in f1_res.ini, so regression checks can run
// the classic rules.
void combat_set_realtime(bool enabled);

// Action points come back over time. The combat pause spends them.
void combat_vats_update();
void combat_vats_pause();

// Real-time fights stop while a full screen such as the inventory is open. The
// freeze holds its own claim on the world pause, so it can open inside the
// combat pause and close again without ending it.
struct CombatFreeze {
    unsigned int start = 0;
    PauseToken token;
};

CombatFreeze combat_freeze_begin();
void combat_freeze_end(CombatFreeze& freeze);

// Moves the combat timers forward by the length of a pause that just ended. Only
// used when the pause set owns pausing (WORLD_SIM=2).
void combat_world_resumed(unsigned int elapsed);

static inline bool isInCombat()
{
    return (combat_state & COMBAT_STATE_0x01) != 0;
}

} // namespace fallout

#endif /* FALLOUT_GAME_COMBAT_H_ */
