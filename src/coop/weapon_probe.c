#include "coop/weapon_probe.h"
#include "coop/campaign.h"
#include "coop/encounter_probe.h"
#include "coop/runtime.h"
#include "game/ai.h"
#include "game/anim.h"
#include "game/anim_private.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/script.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/target.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WEAPON_TAG = 796000,
    TARGET_TAG = 797000,
    SWAP_TAG = 798000,
    LAST_PHASE = 10 };
static const int prototypes[4] = { BP_SWORD, BP_BOW, BP_REVOLVER, BP_BOOMERANG };
static const int aims[4] = { HIT_LOC_HEAD, HIT_LOC_LEG, HIT_LOC_ARM, HIT_LOC_TORSO };
struct CoopWeaponProbe {
    CoopSession* session;
    CoopGameplay* game;
    int local;
    bool host, framebuffer, active, sent, done, complete, stop_seen, stop_sent;
    bool previous_auto;
    CombatAttackObserver previous_observer;
    uint64_t chats, phase_at, ready_at;
    unsigned phase, ack[4], state, observed[4], aimed[4], ranged[4], hits[4];
    unsigned baseline[4], repeat_start, once_seen[4], once_ammo[4], turn_seen[4];
    unsigned friendly_denials, stand_attempts, approach_attempts, empty_attempts;
    int64_t remote_position;
    int remote_damage;
    uint32_t expected, halt_expected;
    bool failed;
};
static CoopWeaponProbe* observing;

static int64_t tagged(int tag, bool weapon)
{
    int64_t obj;
    int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
            int type = obj_field_int32_get(obj, OBJ_F_TYPE);
            if (weapon ? type == OBJ_TYPE_WEAPON && obj_field_int32_get(obj, OBJ_F_ITEM_PAD_I_1) == tag
                       : type == OBJ_TYPE_NPC && obj_field_int32_get(obj, OBJ_F_NPC_PAD_I_1) == tag) return obj;
        } while (obj_inst_next(&obj, &iterator));
    return OBJ_HANDLE_NULL;
}
static bool idle(int64_t pc)
{
    AnimID current, fidget;
    return !sub_423300(pc, &current) || (anim_is_current_goal_type(pc, AG_ANIM_FIDGET, &fidget) && anim_id_is_equal(&current, &fidget));
}
static int ammo(int64_t pc, int64_t weapon)
{
    int type = item_weapon_ammo_type(weapon);
    return type == 10000 ? -1 : item_ammo_quantity_get(pc, type);
}
static void observe(const CombatContext* combat, bool resolved)
{
    CoopWeaponProbe* p = observing;
    if (!p || !p->active) return;
    int member = coop_campaign_slot_for_object(combat->attacker_obj);
    if (member < 0 || combat->field_28 != tagged(TARGET_TAG + member, false)) return;
    if (!resolved) {
        if (combat->hit_loc != HIT_LOC_TORSO) p->aimed[member] |= 1u << combat->hit_loc;
        return;
    }
    p->observed[member]++;
    if (combat->flags & CF_RANGED) p->ranged[member]++;
    if (combat->flags & CF_HIT) p->hits[member]++;
    AnimID id;
    AnimRunInfo* run = NULL;
    if (sub_423300(combat->attacker_obj, &id)) anim_id_to_run_info(&id, &run);
    fprintf(stderr, "ARCMP weapon native phase=%u member=%d hit-location=%d flags=%x damage=%d run-flags=%x auto=%d intercepted=%d\n",
        p->phase, member, combat->hit_loc, combat->flags, combat->total_dam, run ? run->flags : 0, combat_auto_attack_get(combat->attacker_obj), combat->target_obj != combat->field_28);
}
static bool capture(CoopWeaponProbe* p, const char* stage)
{
    if (!p->framebuffer) return true;
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    iso_interface_refresh();
    intgame_show();
    gamelib_redraw();
    iso_redraw();
    tig_window_display();
    char path[80];
    snprintf(path, sizeof(path), "arcmp-weapons-%s.bmp", stage);
    return tig_video_screenshot_save(path) == TIG_OK;
}
static bool click(int64_t obj)
{
    location_origin_set(obj_field_int64_get(obj, OBJ_F_LOCATION));
    TigRect rect;
    object_get_rect(obj, 0, &rect);
    for (int y = rect.y; y < rect.y + rect.height; y += 2)
        for (int x = rect.x; x < rect.x + rect.width; x += 2) {
            TargetDescriptor picked;
            if (x < 0 || x >= 800 || y < 80 || y >= 430 || !target_pick_at_screen_xy(x, y, &picked, false)
                || picked.is_loc || picked.obj != obj) continue;
            TigMessage m = { .type = TIG_MESSAGE_MOUSE };
            m.data.mouse.x = x;
            m.data.mouse.y = y;
            m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN;
            intgame_process_event(&m);
            m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP;
            intgame_process_event(&m);
            return true;
        }
    return false;
}
static bool announce(CoopWeaponProbe* p, unsigned phase)
{
    char text[48];
    snprintf(text, sizeof(text), "weapons-phase:%u", phase);
    return coop_session_chat(p->session, text);
}
static bool make_weapon(int prototype, int tag, int64_t pc)
{
    int64_t item = 0;
    if (!object_create(sub_4685A0(prototype), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)) return false;
    obj_field_int32_set(item, OBJ_F_ITEM_PAD_I_1, tag);
    return item_transfer(item, pc);
}
static bool make_target(unsigned member, int64_t loc)
{
    int64_t target = 0;
    if (!object_create(sub_4685A0(BP_WOLF), loc, &target)) return false;
    Script none = { 0 };
    for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++)
        obj_arrayfield_script_set(target, OBJ_F_SCRIPTS_IDX, sap, &none);
    obj_field_int32_set(target, OBJ_F_NPC_PAD_I_1, TARGET_TAG + member);
    obj_field_int32_set(target, OBJ_F_HP_PTS, 10000);
    obj_field_int32_set(target, OBJ_F_CRITTER_FLAGS, obj_field_int32_get(target, OBJ_F_CRITTER_FLAGS) | OCF_PARALYZED | OCF_FATIGUE_IMMUNE);
    return coop_encounter_place_far(target, loc);
}
CoopWeaponProbe* coop_weapon_probe_create(CoopSession* session, CoopGameplay* game, int local, bool framebuffer)
{
    CoopWeaponProbe* p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->session = session;
    p->game = game;
    p->local = local;
    p->host = coop_session_is_host(session);
    p->framebuffer = framebuffer;
    p->previous_auto = combat_auto_attack_get(player_get_local_pc_obj());
    combat_auto_attack_set(false);
    if (p->host) {
        observing = p;
        p->previous_observer = combat_attack_observer_set(observe);
    }
    return p;
}
bool coop_weapon_probe_start(CoopWeaponProbe* p)
{
    if (!p || !p->host || p->active) return false;
    int64_t origin = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        if (!coop_encounter_place_far(pc, location_make(location_get_x(origin) + 4 * i, location_get_y(origin) + 2))) return false;
        for (int skill = BASIC_SKILL_BOW; skill < BASIC_SKILL_COUNT; skill++)
            basic_skill_points_set(pc, skill, 20);
        tech_skill_points_set(pc, TECH_SKILL_FIREARMS, 20);
        stat_base_set(pc, STAT_PERCEPTION, 20);
        int64_t loc = obj_field_int64_get(pc, OBJ_F_LOCATION);
        if (!make_weapon(prototypes[i], WEAPON_TAG + i, pc)
            || !make_target(i, location_make(location_get_x(loc), location_get_y(loc) + (i ? 8 : 2)))) return false;
        int64_t weapon = tagged(WEAPON_TAG + i, true);
        fprintf(stderr, "ARCMP weapon setup member=%u prototype=%d name=%d skill=%d range=%d flags=%x\n", i,
            obj_get_id(obj_field_handle_get(weapon, OBJ_F_PROTOTYPE_HANDLE)).d.a,
            obj_field_int32_get(weapon, OBJ_F_NAME), item_weapon_skill(weapon), item_weapon_range(weapon, pc),
            obj_field_int32_get(weapon, OBJ_F_WEAPON_FLAGS));
        int type = item_weapon_ammo_type(weapon);
        if (type != 10000) {
            int quantity = item_ammo_quantity_get(pc, type);
            if (quantity && !item_ammo_transfer(pc, 0, quantity, type, 0)) return false;
            int64_t supply = item_ammo_create(30, type, loc);
            if (!supply || !item_transfer(supply, pc)) return false;
        }
    }
    return announce(p, 1);
}
static bool result(CoopWeaponProbe* p, unsigned wanted)
{
    unsigned status;
    uint32_t sequence = coop_battle_last_result(coop_gameplay_battle(p->game), &status);
    if (sequence != p->expected) return false;
    if (status != wanted) {
        fprintf(stderr, "ARCMP weapon unexpected receipt phase=%u member=%d status=%u wanted=%u\n", p->phase, p->local, status, wanted);
        p->failed = true;
        return false;
    }
    return true;
}
static bool attack(CoopWeaponProbe* p, int64_t target, int hit_loc, CoopAttackMode mode)
{
    unsigned status;
    p->expected = coop_battle_last_result(coop_gameplay_battle(p->game), &status) + 1;
    return coop_runtime_combat(COOP_COMBAT_ATTACK, (hit_loc << COOP_ATTACK_AIM_SHIFT) | (mode << COOP_ATTACK_MODE_SHIFT), target);
}
static bool all_idle(void)
{
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        if (!idle(pc) || (obj_field_int32_get(pc, OBJ_F_CRITTER_FLAGS2) & OCF2_USING_BOOMERANG)) return false;
    }
    int64_t obj;
    int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
            if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_PROJECTILE
                && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return false;
        } while (obj_inst_next(&obj, &iterator));
    return true;
}
static bool phase_ready_host(CoopWeaponProbe* p, uint64_t now)
{
    if (!all_idle()) {
        p->ready_at = 0;
        return false;
    }
    if (p->phase == 2 || p->phase == 4) {
        for (unsigned i = 0; i < 4; i++) {
            unsigned required = p->phase == 2 ? 1 : 2;
            if (p->observed[i] != required) return false;
            if (i == 1 || i == 2) {
                int count = ammo(coop_campaign_object(i), tagged(WEAPON_TAG + i, true));
                if (count != 30 - (int)required) return false;
            }
            if (p->phase == 4 && aims[i] && !(p->aimed[i] & (1u << aims[i]))) return false;
        }
    }
    if (!p->ready_at) p->ready_at = now;
    return now - p->ready_at >= (p->phase == 2 || p->phase == 7 || p->phase == 8 || p->phase == 9 ? 1600u : 150u);
}
int coop_weapon_probe_poll(CoopWeaponProbe* p, uint64_t now)
{
    if (!p || p->failed) return -1;
    for (; p->chats < coop_session_chat_count(p->session); p->chats++) {
        const CoopChat* c = coop_session_chat_at(p->session, p->chats);
        unsigned phase;
        if (!c->slot && sscanf(c->text, "weapons-phase:%u", &phase) == 1) {
            if (phase != p->phase + 1 || phase > LAST_PHASE) return -1;
            p->active = true;
            p->phase = phase;
            p->phase_at = now;
            p->ready_at = 0;
            p->state = 0;
            p->sent = p->done = p->stop_seen = p->stop_sent = false;
            memcpy(p->baseline, p->observed, sizeof(p->baseline));
            p->remote_position = obj_field_int64_get(coop_campaign_object(1), OBJ_F_LOCATION);
            int64_t remote_target = tagged(TARGET_TAG + 1, false);
            p->remote_damage = remote_target ? obj_field_int32_get(remote_target, OBJ_F_HP_DAMAGE) : 0;
            fprintf(stderr, "ARCMP weapons phase=%u member=%d\n", phase, p->local);
        }
        if (sscanf(c->text, "weapons-done:%u", &phase) == 1 && c->slot < 4) p->ack[c->slot] = phase;
        if (!c->slot && !strcmp(c->text, "weapons-stop-repeat")) p->stop_seen = true;
    }
    if (!p->active) return 0;
    if (now - p->phase_at > 25000) {
        fprintf(stderr, "ARCMP weapons timeout phase=%u member=%d state=%u sent=%d counts=%u,%u,%u,%u\n", p->phase, p->local, p->state, p->sent, p->observed[0], p->observed[1], p->observed[2], p->observed[3]);
        return -1;
    }
    int64_t pc = player_get_local_pc_obj(), weapon = tagged(WEAPON_TAG + p->local, true), target = tagged(TARGET_TAG + p->local, false);
    if (!weapon || !target) return 0;
    bool ready = false;
    if (p->host && p->phase == 6 && p->observed[1] >= p->baseline[1] + 2 && !p->stop_sent) {
        p->stop_sent = coop_session_chat(p->session, "weapons-stop-repeat");
        if (!p->stop_sent) return -1;
    }
    if (!p->done) {
        if (p->phase == 1 || p->phase == 7 || p->phase == 9) {
            bool equips = p->phase == 1 || p->local == 1;
            int64_t desired = p->phase == 7 ? tagged(SWAP_TAG, true) : weapon;
            if (equips && desired && !p->sent) p->sent = coop_runtime_item(COOP_ITEM_MOVE, desired, 0, ITEM_INV_LOC_WEAPON);
            unsigned status;
            ready = !equips || (p->sent && !coop_items_busy(coop_gameplay_items(p->game)) && coop_items_last_result(coop_gameplay_items(p->game), &status) && !status && item_wield_get(pc, ITEM_INV_LOC_WEAPON) == desired);
            if (ready && p->phase != 1 && p->local == 1) {
                if (!p->state) {
                    SDL_Keymod previous = SDL_GetModState();
                    SDL_SetModState(SDL_KMOD_SHIFT);
                    unsigned battle_status;
                    p->expected = coop_battle_last_result(coop_gameplay_battle(p->game), &battle_status) + 1;
                    bool clicked = click(target);
                    SDL_SetModState(previous);
                    if (!clicked) return -1;
                    p->state = 1;
                }
                ready = result(p, 0);
                if (ready) {
                    if (p->phase == 7)
                        p->stand_attempts++;
                    else
                        p->empty_attempts++;
                }
            }
        } else if (p->phase == 2) {
            if (!p->sent) {
                unsigned status;
                p->expected = coop_battle_last_result(coop_gameplay_battle(p->game), &status) + 1;
                p->sent = click(target);
            }
            ready = p->sent && result(p, 0);
            if (ready && !capture(p, "once")) return -1;
        } else if (p->phase == 3 || p->phase == 5) {
            bool turn_based = p->phase == 3;
            if (p->host && !p->sent) {
                unsigned status;
                p->expected = coop_battle_last_result(coop_gameplay_battle(p->game), &status) + 1;
                p->sent = coop_runtime_combat(COOP_COMBAT_POLICY, turn_based, 0);
            }
            ready = (!p->host || (p->sent && result(p, 0))) && combat_is_turn_based() == turn_based;
            if (turn_based) ready = ready && combat_turn_based_is_active();
        } else if (p->phase == 4) {
            bool own_turn = combat_turn_based_whos_turn_get() == pc;
            if (own_turn && !coop_battle_busy(coop_gameplay_battle(p->game))) {
                if (!p->sent) {
                    int64_t ally = coop_campaign_object((p->local + 1) % 4);
                    p->sent = attack(p, ally, HIT_LOC_HEAD, COOP_ATTACK_ONCE);
                } else if (!p->state && result(p, COOP_COMBAT_TARGET)) {
                    p->friendly_denials++;
                    p->sent = attack(p, target, aims[p->local], COOP_ATTACK_ONCE);
                    p->state = 1;
                } else if (p->state == 1 && result(p, 0)) {
                    if (!capture(p, "turn")) return -1;
                    unsigned status;
                    p->expected = coop_battle_last_result(coop_gameplay_battle(p->game), &status) + 1;
                    p->sent = coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                    p->state = 2;
                } else if (p->state == 2) {
                    unsigned status;
                    uint32_t sequence = coop_battle_last_result(coop_gameplay_battle(p->game), &status);
                    if (sequence == p->expected && status == COOP_COMBAT_BUSY) {
                        p->expected++;
                        p->sent = coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                    }
                }
            }
            ready = p->state == 2 && !own_turn;
        } else if (p->phase == 6) {
            ready = p->local != 1;
            if (p->local == 1) {
                if (!p->sent) {
                    combat_auto_attack_set(true);
                    unsigned status;
                    p->expected = coop_battle_last_result(coop_gameplay_battle(p->game), &status) + 1;
                    p->sent = click(target);
                }
                if (p->stop_seen && !p->state && result(p, 0)) {
                    bool accepted;
                    p->halt_expected = coop_gameplay_last_result(p->game, &accepted) + 1;
                    p->state = coop_runtime_halt(obj_field_int64_get(pc, OBJ_F_LOCATION)) ? 1 : 0;
                }
                if (p->state) {
                    bool accepted;
                    uint32_t result = coop_gameplay_last_result(p->game, &accepted);
                    ready = result == p->halt_expected && accepted;
                    if (result == p->halt_expected && !accepted) p->state = 0;
                    if (ready) combat_auto_attack_set(false);
                }
            }
        } else if (p->phase == 8) {
            ready = p->local != 1;
            if (p->local == 1) {
                if (!p->sent) {
                    p->sent = attack(p, target, HIT_LOC_TORSO, COOP_ATTACK_ONCE);
                    p->approach_attempts++;
                }
                ready = p->sent && result(p, 0);
            }
        } else
            ready = true;
        if (ready) {
            char text[48];
            snprintf(text, sizeof(text), "weapons-done:%u", p->phase);
            if (!coop_session_chat(p->session, text)) return -1;
            p->done = true;
        }
    }
    if (p->host) {
        bool all = true;
        for (unsigned i = 0; i < 4; i++)
            all = all && p->ack[i] == p->phase;
        if (all && phase_ready_host(p, now)) {
            for (unsigned i = 0; i < 4; i++)
                fprintf(stderr, "ARCMP weapon settled phase=%u member=%u resolved=%u ammo=%d target-damage=%d\n", p->phase, i,
                    p->observed[i], ammo(coop_campaign_object(i), tagged(WEAPON_TAG + i, true)), obj_field_int32_get(tagged(TARGET_TAG + i, false), OBJ_F_HP_DAMAGE));
            if (p->phase == 2) {
                memcpy(p->once_seen, p->observed, sizeof(p->once_seen));
                for (unsigned i = 0; i < 4; i++)
                    p->once_ammo[i] = (unsigned)ammo(coop_campaign_object(i), tagged(WEAPON_TAG + i, true));
            }
            if (p->phase == 4) memcpy(p->turn_seen, p->observed, sizeof(p->turn_seen));
            if (p->phase == 6) {
                p->repeat_start = p->observed[1] - p->baseline[1];
                if (p->repeat_start < 2 || !make_weapon(BP_SWORD, SWAP_TAG, coop_campaign_object(1))) return -1;
                int64_t t = tagged(TARGET_TAG + 1, false), loc = obj_field_int64_get(coop_campaign_object(1), OBJ_F_LOCATION);
                if (!coop_encounter_place_far(t, location_make(location_get_x(loc), location_get_y(loc) + 8))) return -1;
            }
            if (p->phase == 7 && (p->observed[1] != p->baseline[1] || obj_field_int64_get(coop_campaign_object(1), OBJ_F_LOCATION) != p->remote_position || obj_field_int32_get(tagged(TARGET_TAG + 1, false), OBJ_F_HP_DAMAGE) != p->remote_damage)) return -1;
            if (p->phase == 8) {
                if (p->observed[1] == p->baseline[1]) return 0;
                if (obj_field_int64_get(coop_campaign_object(1), OBJ_F_LOCATION) == p->remote_position
                    || location_dist(obj_field_int64_get(coop_campaign_object(1), OBJ_F_LOCATION),
                           obj_field_int64_get(tagged(TARGET_TAG + 1, false), OBJ_F_LOCATION))
                        > 1) return -1;
                int64_t bow = tagged(WEAPON_TAG + 1, true), member = coop_campaign_object(1);
                int type = item_weapon_ammo_type(bow), quantity = item_ammo_quantity_get(member, type);
                if (quantity && !item_ammo_transfer(member, 0, quantity, type, 0)) return -1;
                int64_t loc = obj_field_int64_get(member, OBJ_F_LOCATION);
                if (!coop_encounter_place_far(tagged(TARGET_TAG + 1, false), location_make(location_get_x(loc), location_get_y(loc) + 8))) return -1;
            }
            if (p->phase == 9 && (ammo(coop_campaign_object(1), tagged(WEAPON_TAG + 1, true)) || obj_field_int64_get(coop_campaign_object(1), OBJ_F_LOCATION) != p->remote_position || obj_field_int32_get(tagged(TARGET_TAG + 1, false), OBJ_F_HP_DAMAGE) != p->remote_damage)) return -1;
            if (p->phase == LAST_PHASE)
                p->complete = true;
            else if (!announce(p, p->phase + 1))
                return -1;
            /* The next announcement must be consumed before advancing again. */
            p->ack[0] = 0;
        }
    }
    return p->failed ? -1 : p->complete ? 1
                                        : 0;
}
bool coop_weapon_probe_report(CoopWeaponProbe* p)
{
    if (!p || p->failed || !p->done || p->phase != LAST_PHASE || p->friendly_denials != 1) return false;
    FILE* file = fopen("arcmp-weapons-result.json", "wb");
    if (!file) return false;
    fprintf(file, "{\"verified\":true,\"phases\":%u,\"local_member\":%d,\"friendly_called_shot_denied\":%u,\"stand_attempts\":%u,\"empty_attempts\":%u,\"repeat_attacks\":%u,\"native\":[", p->phase, p->local, p->friendly_denials, p->stand_attempts, p->empty_attempts, p->repeat_start);
    for (unsigned i = 0; i < 4; i++) {
        fprintf(file, "%s{\"member\":%u,\"resolved\":%u,\"aimed_mask\":%u,\"ranged\":%u,\"hits\":%u,\"once_resolved\":%u,\"once_ammo\":%u,\"turn_resolved\":%u}", i ? "," : "", i, p->observed[i], p->aimed[i], p->ranged[i], p->hits[i], p->once_seen[i], p->once_ammo[i], p->turn_seen[i]);
    }
    fprintf(file, "],\"members\":[");
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i), weapon = tagged(WEAPON_TAG + i, true), t = tagged(TARGET_TAG + i, false);
        char id[OID_STR_LEN];
        int64_t current_weapon = item_wield_get(pc, ITEM_INV_LOC_WEAPON);
        ObjectID object_id = current_weapon ? obj_get_id(current_weapon) : (ObjectID) { .type = OID_TYPE_NULL };
        objid_id_to_str(id, object_id);
        fprintf(file, "%s{\"member\":%u,\"weapon\":\"%s\",\"ammo\":%d,\"target_damage\":%d}", i ? "," : "", i, id, ammo(pc, weapon), obj_field_int32_get(t, OBJ_F_HP_DAMAGE));
    }
    fprintf(file, "]}\n");
    return fclose(file) == 0;
}
void coop_weapon_probe_destroy(CoopWeaponProbe* p)
{
    if (!p) return;
    if (observing == p) {
        combat_attack_observer_set(p->previous_observer);
        observing = NULL;
    }
    combat_auto_attack_set(p->previous_auto);
    free(p);
}
