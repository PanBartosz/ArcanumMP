#include "coop/skills_probe.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "coop/engine_theft.h"
#include "coop/encounter_probe.h"
#include "coop/runtime.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/fate.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/script.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/trap.h"
#include "game/timeevent.h"
#include "game/tf.h"
#include "ui/inven_ui.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/item_ui.h"
#include "ui/skill_ui.h"
#include "tig/video.h"
#include "tig/window.h"
#include "tig/message.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PHASES = 21, TAG = 799000, NPC_TAG = 799900, GOLD_TAG = 799920 };
struct CoopSkillsProbe {
    CoopSession* session; CoopGameplay* game;
    int member; bool host, framebuffer, started, sent, done, complete, replayed, finish_queued, drain_started;
    bool stop_ready, stop_sent, replica_guard, spotted_seen;
    bool drain_clock_advanced;
    DateTime finish_clock;
    unsigned phase, completed, ack[4], native[PHASES + 2][4], feedback_base, feedback_before, replays, feedback_replays, denials;
    unsigned reentrant, retired, actor_errors, finish_native, old_theft_tokens[4], healed_damage, floater_expiry_checks;
    unsigned spots[4];
    uint32_t expected, stop_expected, old_access, sequence;
    uint64_t chats, at, quiet;
    int before, max_before, replay_max, finish_gold;
    CoopSkillRequest request;
    uint8_t item_request[59];
};
static int64_t tagged(int type, int tag)
{
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        int actual = obj_field_int32_get(obj, OBJ_F_TYPE);
        if ((type == -1 ? obj_type_is_item(actual) : actual == type)
            && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) {
            int field = type == OBJ_TYPE_NPC ? OBJ_F_NPC_PAD_I_1 : type == OBJ_TYPE_CONTAINER ? OBJ_F_CONTAINER_PAD_I_1 : OBJ_F_ITEM_PAD_I_1;
            if (obj_field_int32_get(obj, field) == tag) return obj;
        }
    } while (obj_inst_next(&obj, &iterator));
    return 0;
}
static int64_t supply(unsigned phase, unsigned member) { return tagged(-1, TAG + (int)phase * 4 + (int)member); }
static int64_t npc(unsigned member) { return tagged(OBJ_TYPE_NPC, NPC_TAG + (int)member); }
static int64_t gold(unsigned member) { return tagged(-1, GOLD_TAG + (int)member); }
static bool make_item(unsigned phase, unsigned member, int prototype, int64_t owner, int64_t* out)
{
    int64_t item;
    if (!object_create(sub_4685A0(prototype), obj_field_int64_get(owner, OBJ_F_LOCATION), &item)) return false;
    obj_field_int32_set(item, OBJ_F_ITEM_PAD_I_1, TAG + (int)phase * 4 + (int)member);
    if (!item_transfer(item, owner)) return false;
    if (out) *out = item;
    return true;
}
static bool fate(int64_t pc, int type)
{
    stat_base_set(pc, STAT_FATE_POINTS, 20);
    return fate_is_activated(pc, type) || fate_activate(pc, type);
}
static void observe(void* context, const SkillInvocation* invocation)
{
    CoopSkillsProbe* p = context;
    int skill = invocation->skill;
    if (skill == SKILL_SPOT_TRAP && invocation->target.obj && obj_field_int32_get(invocation->target.obj, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER) {
        int tag = obj_field_int32_get(invocation->target.obj, OBJ_F_CONTAINER_PAD_I_1);
        if (tag >= TAG + 40 && tag < TAG + 44) {
            int member = coop_campaign_slot_for_object(invocation->source.obj);
            if (member < 0 || member >= 4 || member != tag - TAG - 40) p->actor_errors++;
            else p->spots[member]++;
        }
        return;
    }
    if (!p->phase || (skill != SKILL_HEAL && skill != SKILL_REPAIR && skill != SKILL_PICK_LOCKS && skill != SKILL_DISARM_TRAPS && skill != SKILL_PICK_POCKET)) return;
    int member = coop_campaign_slot_for_object(invocation->source.obj);
    if (member < 0 || member >= 4) { p->actor_errors++; return; }
    if (p->drain_started) {
        p->finish_native++;
        DateTime clock = sub_45A7C0();
        p->drain_clock_advanced = datetime_compare(&clock, &p->finish_clock) > 0;
        return;
    }
    p->native[p->phase][member]++;
    fprintf(stderr, "ARCMP skills native phase=%u member=%d skill=%d success=%d target-member=%d\n", p->phase, member, skill, (invocation->flags & SKILL_INVOCATION_SUCCESS) != 0, coop_campaign_slot_for_object(invocation->target.obj));
    if (p->phase == 11 && invocation->target.obj != coop_campaign_object(((unsigned)member + 1) % 4)) p->actor_errors++;
    if (p->phase == 11) fprintf(stderr, "ARCMP skills native heal member=%d level=%d critical=%d target-damage=%d\n", member,
        basic_skill_level(invocation->source.obj, BASIC_SKILL_HEAL), (invocation->flags & SKILL_INVOCATION_CRITICAL) != 0,
        obj_field_int32_get(invocation->target.obj, OBJ_F_HP_DAMAGE));
    if (p->phase == 14 || p->phase == 15) {
        int64_t actor = invocation->source.obj; bool found = false;
        for (int slot = anim_find_first(actor); slot != -1; slot = anim_find_next(slot, actor)) {
            AnimRunInfo* run = &anim_run_info[slot];
            if (run->goals[0].params[AGDATA_SKILL_DATA].data != SKILL_PICK_POCKET
                || run->goals[0].params[AGDATA_SCRATCH_OBJ].obj != invocation->item.obj
                || run->goals[0].params[AGDATA_SCRATCH_VAL5].data <= 0) continue;
            unsigned token = (unsigned)run->goals[0].params[AGDATA_SCRATCH_VAL5].data;
            if (p->phase == 14) { p->old_theft_tokens[member] = token; p->reentrant++; }
            else {
                if (!p->old_theft_tokens[member] || token == p->old_theft_tokens[member]) { p->actor_errors++; break; }
                token = p->old_theft_tokens[member]; p->retired++;
            }
            coop_engine_theft_reached(token, &run->id, actor, invocation->target.obj, invocation->item.obj, p->phase == 15);
            if (p->native[p->phase][member] != 1) p->actor_errors++;
            found = true; break;
        }
        if (!found) p->actor_errors++;
    }
}
static bool prepare(CoopSkillsProbe* p, unsigned phase)
{
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i), item;
        if (phase == 6) {
            if (!make_item(6, i, BP_SWORD, pc, &item)) return false;
            object_hp_damage_set(item, 5);
            if (!fate(pc, FATE_CRIT_SUCCESS_REPAIR)) return false;
        }
        if (phase == 10 || phase == 12) {
            int64_t origin = obj_field_int64_get(pc, OBJ_F_LOCATION), target;
            if (!object_create(sub_4685A0(BP_WOOD_CHEST_1), location_make(location_get_x(origin) + 2, location_get_y(origin) + 2), &target)) return false;
            obj_field_int32_set(target, OBJ_F_CONTAINER_PAD_I_1, TAG + (int)phase * 4 + (int)i);
            obj_field_int32_set(target, OBJ_F_CONTAINER_FLAGS, phase == 12 ? OCOF_LOCKED : 0);
            if (phase == 10) {
                Script script = { .num = 30001 };
                obj_arrayfield_script_set(target, OBJ_F_SCRIPTS_IDX, SAP_USE, &script);
                if (trap_type(target) == TRAP_TYPE_INVALID) { fprintf(stderr, "ARCMP skills trap preparation invalid\n"); return false; }
                stat_base_set(pc, basic_skill_stat(BASIC_SKILL_SPOT_TRAP), 20); basic_skill_points_set(pc, BASIC_SKILL_SPOT_TRAP, 5);
                int effectiveness = basic_skill_effectiveness(pc, BASIC_SKILL_SPOT_TRAP, target);
                fprintf(stderr, "ARCMP skills spot setup member=%u points=%d level=%d effectiveness=%d\n", i, basic_skill_points_get(pc, BASIC_SKILL_SPOT_TRAP), basic_skill_level(pc, BASIC_SKILL_SPOT_TRAP), effectiveness);
                if (basic_skill_level(pc, BASIC_SKILL_SPOT_TRAP) != 20 || effectiveness < 50) return false;
                /* Native spotting is capped at 95%. Establish the discovered
                 * trap through bounded native passive checks, never a forced
                 * invocation flag or a manually written spotted bit. */
                for (unsigned attempt = 0; attempt < 20 && !trap_is_spotted(pc, target); attempt++) trap_attempt_spot(pc, target);
                if (!trap_is_spotted(pc, target)) return false;
                if (!fate(pc, FATE_CRIT_SUCCESS_DISARM_TRAPS)) return false;
            } else {
                if (!make_item(12, i, BP_LOCKPICKS, pc, NULL) || !fate(pc, FATE_CRIT_SUCCESS_PICK_LOCKS)) return false;
            }
        }
        if (phase == 11) {
            if (!make_item(11, i, BP_BANDAGES, pc, &item) || !fate(pc, FATE_CRIT_SUCCESS_HEAL)) return false;
            obj_field_int32_set(item, OBJ_F_GENERIC_USAGE_COUNT_REMAINING, 20);
            obj_field_int32_set(pc, OBJ_F_HP_DAMAGE, 10);
        }
        if (phase == 13) {
            int64_t origin = obj_field_int64_get(pc, OBJ_F_LOCATION), target;
            if (!object_create(sub_468570(OBJ_TYPE_NPC), origin, &target)) return false;
            obj_field_int32_set(target, OBJ_F_NPC_PAD_I_1, NPC_TAG + (int)i);
            obj_field_int32_set(target, OBJ_F_HP_PTS, 50); obj_field_int32_set(target, OBJ_F_HP_DAMAGE, 0);
            obj_field_int32_set(target, OBJ_F_CRITTER_FLAGS, obj_field_int32_get(target, OBJ_F_CRITTER_FLAGS) | OCF_PARALYZED);
            Script none = {0}; for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(target, OBJ_F_SCRIPTS_IDX, sap, &none);
            if (!coop_encounter_place_far(target, location_make(location_get_x(origin) - 2, location_get_y(origin) - 2))) return false;
            if (!make_item(90, i, BP_GOLD, target, &item)) return false;
            obj_field_int32_set(item, OBJ_F_ITEM_PAD_I_1, GOLD_TAG + (int)i);
            obj_field_int32_set(item, OBJ_F_GOLD_QUANTITY, 350);
        }
        if (phase == 14 || phase == 15 || phase == 20) {
            if (!fate(pc, FATE_CRIT_SUCCESS_PICK_POCKET)) return false;
            if (phase == 15 && !make_item(15, i, BP_SWORD, pc, NULL)) return false;
        }
        if (phase == 16) {
            if (fate_is_activated(pc, FATE_CRIT_SUCCESS_PICK_POCKET)) fate_deactivate(pc, FATE_CRIT_SUCCESS_PICK_POCKET);
            if (!critter_follow(npc(i), pc, true) || !make_item(16, i, BP_JACKET, npc(i), &item)
                || !item_wield_set(item, ITEM_INV_LOC_ARMOR)) return false;
            SkillInvocation invocation; skill_invocation_init(&invocation);
            invocation.skill = SKILL_PICK_POCKET; sub_4440E0(pc, &invocation.source); sub_4440E0(npc(i), &invocation.target); sub_4440E0(item, &invocation.item);
            fprintf(stderr, "ARCMP skills failure setup member=%u effectiveness=%d difficulty=%d\n", i,
                basic_skill_effectiveness(pc, BASIC_SKILL_PICK_POCKET, npc(i)), skill_invocation_difficulty(&invocation));
            if (basic_skill_effectiveness(pc, BASIC_SKILL_PICK_POCKET, npc(i)) >= skill_invocation_difficulty(&invocation) + 1) return false;
        }
        if (phase == 20) stat_base_set(pc, STAT_DEXTERITY, 20);
        if (phase == 21) {
            int64_t origin = obj_field_int64_get(pc, OBJ_F_LOCATION);
            if (!coop_encounter_place_far(npc(i), location_make(location_get_x(origin) - 12, location_get_y(origin) - 12))) return false;
        }
    }
    char text[64]; snprintf(text, sizeof(text), "skills-phase:%u", phase);
    return coop_session_chat(p->session, text);
}
CoopSkillsProbe* coop_skills_probe_create(CoopSession* session, CoopGameplay* game, int member, bool framebuffer)
{
    CoopSkillsProbe* p = calloc(1, sizeof(*p));
    if (p) {
        p->session = session; p->game = game; p->member = member; p->host = coop_session_is_host(session); p->framebuffer = framebuffer;
        p->feedback_base = skill_ui_coop_presentations();
        skill_invocation_observer_set(observe, p);
    }
    return p;
}
static bool capture(CoopSkillsProbe* p)
{
    if (!p->framebuffer) return true;
    if (p->phase != 1 && p->phase != 6 && p->phase != 10 && p->phase != 11 && p->phase != 12 && p->phase != 13 && p->phase != 16 && p->phase != 21) return true;
    iso_interface_refresh(); gamelib_redraw(); iso_redraw(); tig_window_display();
    char path[80]; snprintf(path, sizeof(path), "arcmp-skills-%u.bmp", p->phase);
    return tig_video_screenshot_save(path) == TIG_OK;
}
static bool skill_phase(unsigned phase) { return phase <= 10; }
static unsigned mask(void)
{
    unsigned value = 0; for (unsigned i = 0; i < 4; i++) if (critter_is_concealed(coop_campaign_object(i))) value |= 1u << i;
    return value;
}
int coop_skills_probe_poll(CoopSkillsProbe* p, uint64_t now, bool start)
{
    for (; p->chats < coop_session_chat_count(p->session); p->chats++) {
        const CoopChat* c = coop_session_chat_at(p->session, p->chats); unsigned phase;
        if (!c->slot && sscanf(c->text, "skills-phase:%u", &phase) == 1) {
            if (phase != p->phase + 1 || phase > PHASES) return -1;
            p->phase = phase; p->at = now; p->quiet = 0; p->sent = p->done = p->replayed = p->stop_ready = p->stop_sent = false;
            p->feedback_before = skill_ui_coop_presentations();
            if (phase == 21) p->finish_gold = item_gold_get(coop_campaign_object(0));
            /* Keep the normal stealing view/access alive across consecutive
             * attempts. Changing native UI mode would deliberately close its
             * lease, which is exercised explicitly in phase 17. */
            if ((phase < 14 || phase > 17) && phase != 21) { inven_ui_coop_stop(); skill_ui_cancel(); }
            unsigned status;
            p->expected = phase <= 10 ? coop_skills_last_result(coop_gameplay_skills(p->game), &status) + 1
                : phase <= 12 ? coop_magic_last_result(coop_gameplay_magic(p->game), &status) + 1
                : coop_items_last_result(coop_gameplay_items(p->game), &status) + 1;
            fprintf(stderr, "ARCMP skills phase=%u member=%d\n", phase, p->member);
        }
        if (sscanf(c->text, "skills-done:%u", &phase) == 1 && c->slot < 4) p->ack[c->slot] = phase;
        if (!c->slot && !strcmp(c->text, "skills-stop-ready")) p->stop_ready = true;
    }
    if (p->host && start && !p->started) { p->started = true; if (!prepare(p, 1)) return -1; }
    if (!p->phase || p->complete) return p->complete;
    if (now - p->at > 20000) {
        fprintf(stderr, "ARCMP skills timeout phase=%u member=%d sent=%d done=%d feedback=%u ack=%u/%u/%u/%u\n", p->phase, p->member, p->sent, p->done, skill_ui_coop_presentations(), p->ack[0], p->ack[1], p->ack[2], p->ack[3]);
        for (unsigned i = 0; i < 4; i++) {
            int64_t actor = coop_campaign_object(i), tool = supply(11, i);
            fprintf(stderr, "ARCMP skills timeout actor=%u hp-damage=%d tool=%d charges=%d native=%u\n", i, obj_field_int32_get(actor, OBJ_F_HP_DAMAGE), tool != 0, tool ? obj_field_int32_get(tool, OBJ_F_GENERIC_USAGE_COUNT_REMAINING) : -1, p->native[p->phase][i]);
            if (p->host) for (int slot = anim_find_first(actor); slot != -1; slot = anim_find_next(slot, actor))
                fprintf(stderr, "ARCMP skills timeout goal=%d slot=%d current=%d\n", anim_run_info[slot].goals[0].type, slot, anim_run_info[slot].current_goal);
        }
        return -1;
    }
    int64_t pc = player_get_local_pc_obj();
    if (!p->done && now - p->at > 1000) {
        if (!p->sent) {
            if (p->phase == 10 && !p->spotted_seen) {
                if (skill_ui_coop_presentations() < p->feedback_base + 3) return 0;
                if (skill_ui_coop_presentations() != p->feedback_base + 3 || skill_ui_coop_last_feedback() != 0) return -1;
                if (p->framebuffer) {
                    iso_interface_refresh(); gamelib_redraw(); iso_redraw(); tig_window_display();
                    if (tig_video_screenshot_save("arcmp-skills-spotted.bmp") != TIG_OK) return -1;
                }
                p->feedback_before = skill_ui_coop_presentations(); p->spotted_seen = true;
            }
            bool observer = p->phase <= 4 && (unsigned)p->member != p->phase - 1;
            if (observer) p->sent = true;
            else if (skill_phase(p->phase)) {
                CoopSkillRequest r = { .epoch = 1, .sequence = p->expected, .map = (uint32_t)map_current_map(), .generation = combat_turn_generation_get() };
                memcpy(r.actor, obj_get_id(pc).d.g.data, 16);
                int64_t target = 0;
                if (p->phase <= 5 || p->phase == 9) { r.action = COOP_SKILL_CONCEAL; r.value = p->phase != 5; }
                else if (p->phase == 10) { r.action = COOP_SKILL_DISARM; target = tagged(OBJ_TYPE_CONTAINER, TAG + 40 + p->member); }
                else { r.action = COOP_SKILL_REPAIR; target = supply(6, p->phase == 8 ? ((unsigned)p->member + 1) % 4 : (unsigned)p->member); }
                if ((p->phase >= 6 && p->phase <= 8) || p->phase == 10) {
                    if (!target) return 0;
                    if (!coop_object_id_from_engine(obj_get_id(target), &r.target)) return -1;
                }
                if (p->phase == 9) r.generation = r.generation ? r.generation - 1 : 1;
                p->request = r;
                if (p->phase == 6 || p->phase == 7 || p->phase == 10) {
                    TargetDescriptor td; target_descriptor_set_obj(&td, target);
                    sub_579FA0(pc, p->phase == 10 ? 2 : 3);
                    if (p->phase == 6 || p->phase == 7) {
                        int slot = item_inventory_location_get(target);
                        if (!inven_ui_is_created() || intgame_mode_get() != INTGAME_MODE_SKILL || slot < 0 || slot >= 120) return -1;
                        if (p->framebuffer && p->phase == 6) {
                            gamelib_redraw(); iso_redraw(); tig_window_display();
                            if (tig_video_screenshot_save("arcmp-skills-targeting.bmp") != TIG_OK) return -1;
                        }
                        TigMessage msg = { .type = TIG_MESSAGE_MOUSE };
                        msg.data.mouse.x = 368 + (slot % 10) * 32 + 8; msg.data.mouse.y = 49 + (slot / 10) * 32 + 8;
                        msg.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN;
                        tig_message_enqueue(&msg); p->sent = true;
                    } else {
                        skill_ui_apply(&td); p->sent = coop_skills_busy(coop_gameplay_skills(p->game));
                    }
                    if (!p->host && !p->replica_guard) {
                        SkillInvocation invocation; skill_invocation_init(&invocation); invocation.skill = SKILL_REPAIR;
                        if (skill_invocation_run(&invocation)) return -1;
                        p->replica_guard = true;
                    }
                } else if (p->phase <= 5) {
                    bool before = critter_is_concealed(pc); sub_579FA0(pc, 0);
                    if (critter_is_concealed(pc) != before) return -1;
                    p->sent = coop_skills_busy(coop_gameplay_skills(p->game));
                } else p->sent = coop_gameplay_skill_request(p->game, &r);
            } else if (p->phase == 11 || p->phase == 12) {
                int64_t item = supply(p->phase, (unsigned)p->member), target = p->phase == 11 ? coop_campaign_object(((unsigned)p->member + 1) % 4) : tagged(OBJ_TYPE_CONTAINER, TAG + 48 + p->member);
                if (!item || !target) return 0;
                TargetDescriptor td; target_descriptor_set_obj(&td, target); item_ui_activate(pc, item); item_ui_apply(&td);
                p->sent = coop_magic_busy(coop_gameplay_magic(p->game));
            } else {
                CoopItems* items = coop_gameplay_items(p->game);
                int64_t target = npc((unsigned)p->member), item = gold((unsigned)p->member);
                if (!target || !item) return 0;
                p->before = item_gold_get(pc);
                if (p->phase == 13 || p->phase == 19 || p->phase == 20) {
                    TargetDescriptor td; target_descriptor_set_obj(&td, p->phase == 19 ? coop_campaign_object(((unsigned)p->member + 1) % 4) : target);
                    sub_579FA0(pc, 1); skill_ui_apply(&td); p->sent = coop_items_busy(items);
                } else if (p->phase == 17) { p->old_access = coop_items_loot_generation(items); p->sent = coop_gameplay_item(p->game, COOP_LOOT_CLOSE, 0, 0, -1); }
                else {
                    unsigned op = p->phase == 15 ? COOP_LOOT_PUT : COOP_LOOT_TAKE;
                    if (p->phase == 15) { item = supply(15, (unsigned)p->member); if (!item) return 0; }
                    if (p->phase == 16) { item = supply(16, (unsigned)p->member); if (!item) return 0; }
                    if (p->phase == 18) {
                        CoopObjectId id; if (!coop_object_id_from_engine(obj_get_id(item), &id)) return -1;
                        p->sent = coop_items_submit_access(items, map_current_map(), obj_get_id(pc).d.g.data, (CoopItemOperation)op, id, 0, -1, p->old_access);
                    } else p->sent = coop_gameplay_item(p->game, (CoopItemOperation)op, item, 0, -1);
                    if (p->phase == 14) {
                        uint8_t* bytes = p->item_request; CoopObjectId id; if (!coop_object_id_from_engine(obj_get_id(item), &id)) return -1;
                        coop_object_write32(bytes, 1); coop_object_write32(bytes + 4, p->expected); coop_object_write32(bytes + 8, (uint32_t)map_current_map());
                        memcpy(bytes + 12, obj_get_id(pc).d.g.data, 16); bytes[28] = COOP_LOOT_TAKE; memcpy(bytes + 29, id.bytes, 17); bytes[46] = 0;
                        coop_object_write32(bytes + 47, UINT32_MAX); coop_object_write32(bytes + 51, coop_items_loot_generation(items)); coop_object_write32(bytes + 55, combat_turn_generation_get());
                    }
                }
            }
            if (!p->sent) return -1;
        }
        if (p->phase == 21 && p->host && !p->stop_ready) {
            bool all = true;
            for (unsigned i = 0; i < 4; i++) {
                bool found = false; int64_t actor = coop_campaign_object(i);
                for (int slot = anim_find_first(actor); slot != -1; slot = anim_find_next(slot, actor))
                    if ((anim_run_info[slot].goals[0].type == AG_USE_SKILL_ON || anim_run_info[slot].goals[0].type == AG_ATTEMPT_USE_PICKPOCKET_SKILL_ON)
                        && anim_run_info[slot].goals[0].params[AGDATA_SCRATCH_OBJ].obj == gold(i)
                        && anim_run_info[slot].goals[0].params[AGDATA_SKILL_DATA].data == SKILL_PICK_POCKET && anim_run_info[slot].goals[0].params[AGDATA_SCRATCH_VAL5].data > 0) found = true;
                all &= found;
            }
            if (all) { if (!coop_session_chat(p->session, "skills-stop-ready")) return -1; p->stop_ready = true; }
        }
        if (p->phase == 21 && p->stop_ready && !p->stop_sent) {
            bool accepted; p->stop_expected = coop_gameplay_last_result(p->game, &accepted) + 1;
            p->stop_sent = coop_runtime_halt(obj_field_int64_get(pc, OBJ_F_LOCATION));
        }
        unsigned status = 0; uint32_t result = 0;
        if (p->phase <= 10) result = coop_skills_last_result(coop_gameplay_skills(p->game), &status);
        else if (p->phase <= 12) result = coop_magic_last_result(coop_gameplay_magic(p->game), &status);
        else result = coop_items_last_result(coop_gameplay_items(p->game), &status);
        bool observer = p->phase <= 4 && (unsigned)p->member != p->phase - 1;
        bool ready = observer || result == p->expected;
        unsigned wanted = p->phase == 8 ? COOP_SKILL_OWNERSHIP : p->phase == 9 ? COOP_SKILL_STALE : p->phase == 18 ? COOP_LOOT_DENIED : p->phase == 19 ? COOP_ITEM_TARGET : p->phase == 21 ? COOP_ITEM_INTERRUPTED : 0;
        if (ready && !observer && status != wanted) { fprintf(stderr, "ARCMP skills unexpected phase=%u status=%u wanted=%u\n", p->phase, status, wanted); return -1; }
        if (ready) {
            if (p->phase <= 5) ready = mask() == (p->phase == 5 ? 0u : (1u << p->phase) - 1);
            else if (p->phase == 9) ready = mask() == 0;
            else if (p->phase == 6 || p->phase == 7) ready = object_hp_damage_get(supply(6, (unsigned)p->member)) == 0 && skill_ui_coop_presentations() == p->feedback_before + 1 && skill_ui_coop_last_feedback() == (p->phase == 6 ? 500 : 505);
            else if (p->phase == 10) ready = trap_type(tagged(OBJ_TYPE_CONTAINER, TAG + 40 + p->member)) == TRAP_TYPE_INVALID && skill_ui_coop_last_feedback() == 560 && skill_ui_coop_presentations() == p->feedback_before + 1;
            else if (p->phase == 11) ready = obj_field_int32_get(pc, OBJ_F_HP_DAMAGE) < 10 && obj_field_int32_get(supply(11, (unsigned)p->member), OBJ_F_GENERIC_USAGE_COUNT_REMAINING) == 19 && skill_ui_coop_presentations() == p->feedback_before + 1;
            else if (p->phase == 12) ready = !object_locked_get(tagged(OBJ_TYPE_CONTAINER, TAG + 48 + p->member)) && skill_ui_coop_last_feedback() == 550 && skill_ui_coop_presentations() == p->feedback_before + 1;
            else if (p->phase == 13 || p->phase == 20) ready = coop_items_stealing(coop_gameplay_items(p->game)) && coop_items_loot_target(coop_gameplay_items(p->game)) == npc((unsigned)p->member);
            else if (p->phase == 14) ready = item_gold_get(pc) == p->before + 100 && skill_ui_coop_presentations() == p->feedback_before + 1;
            else if (p->phase == 15) { int64_t parent = 0; item_parent(supply(15, (unsigned)p->member), &parent); ready = parent == npc((unsigned)p->member) && skill_ui_coop_presentations() == p->feedback_before + 1; }
            else if (p->phase == 16) {
                int64_t item = supply(16, (unsigned)p->member), parent = 0;
                ready = item && item_parent(item, &parent) && parent == npc((unsigned)p->member) && item_inventory_location_get(item) == ITEM_INV_LOC_ARMOR
                    && item_gold_get(pc) == p->before && skill_ui_coop_last_feedback() == 501 && skill_ui_coop_presentations() == p->feedback_before + 1;
            }
            else if (p->phase == 17) ready = !coop_items_loot_target(coop_gameplay_items(p->game));
            else if (p->phase == 21) { bool accepted; ready = p->stop_sent && coop_gameplay_last_result(p->game, &accepted) == p->stop_expected && accepted && item_gold_get(pc) == p->before; }
        }
        if (ready && p->phase == 7) {
            if (!p->quiet) p->quiet = now;
            if (now - p->quiet < 4000) ready = false;
            else {
                if (tf_has_entries(pc) || (obj_field_int32_get(pc, OBJ_F_FLAGS) & OF_TEXT_FLOATER)) return -1;
                p->floater_expiry_checks++;
            }
        }
        if (ready && (p->phase == 6 || p->phase == 14) && !p->replayed) {
            bool sent;
            if (p->phase == 6) { uint8_t bytes[COOP_SKILL_REQUEST_BYTES]; if (!coop_skill_request_encode(&p->request, bytes)) return -1; sent = p->host ? coop_skills_message(coop_gameplay_skills(p->game), 0, COOP_SKILL_REQUEST, bytes, sizeof(bytes)) : coop_session_send(p->session, 0, COOP_SKILL_REQUEST, bytes, sizeof(bytes)); p->replay_max = object_hp_max(supply(6, (unsigned)p->member)); }
            else sent = p->host ? coop_items_message(coop_gameplay_items(p->game), 0, COOP_ITEM_REQUEST, p->item_request, sizeof(p->item_request)) : coop_session_send(p->session, 0, COOP_ITEM_REQUEST, p->item_request, sizeof(p->item_request));
            if (!sent) return -1;
            p->replayed = true; p->replays++; p->quiet = now; ready = false;
        }
        if (p->replayed && now - p->quiet < 700) ready = false;
        if (ready && p->phase == 6 && p->replayed && object_hp_max(supply(6, (unsigned)p->member)) != p->replay_max) return -1;
        if (ready) {
            if (p->phase == 11) p->healed_damage = (unsigned)obj_field_int32_get(pc, OBJ_F_HP_DAMAGE);
            if (wanted) p->denials++;
            if (!capture(p)) return -1;
            char text[64]; snprintf(text, sizeof(text), "skills-done:%u", p->phase);
            if (!coop_session_chat(p->session, text)) return -1;
            p->done = true; p->completed = p->phase;
        }
    }
    if (p->host) {
        bool all = true; for (unsigned i = 0; i < 4; i++) all &= p->ack[i] == p->phase;
        if (all) {
            if (p->phase == 6 && !p->feedback_replays) {
                for (unsigned slot = 1; slot < 4; slot++) {
                    uint8_t bytes[COOP_SKILL_FEEDBACK_BYTES];
                    if (!coop_skills_feedback_last(coop_gameplay_skills(p->game), (uint8_t)slot, bytes)
                        || !coop_session_send(p->session, (uint8_t)slot, COOP_SKILL_FEEDBACK, bytes, sizeof(bytes))) return -1;
                    p->feedback_replays++;
                }
                p->quiet = now; return 0;
            }
            if (!p->quiet) p->quiet = now;
            if (now - p->quiet < 700) return 0;
            if (p->phase == PHASES) { p->complete = true; return 1; }
            if (!prepare(p, p->phase + 1)) { fprintf(stderr, "ARCMP skills preparation failed phase=%u\n", p->phase + 1); return -1; }
            p->ack[0] = 0;
        }
    }
    return 0;
}
int coop_skills_probe_finish(CoopSkillsProbe* p)
{
    int64_t actor = coop_campaign_object(0);
    if (!p->finish_queued) {
        p->finish_gold = item_gold_get(actor);
        /* The far targets exercise interruption; the positive drain control
         * uses a nearby clear tile, independent of crash-site path geometry. */
        if (!coop_encounter_place_far(npc(0), obj_field_int64_get(actor, OBJ_F_LOCATION))) return -1;
        p->finish_clock = sub_45A7C0();
        if (!coop_gameplay_item(p->game, COOP_LOOT_TAKE, gold(0), 0, -1)) return -1;
        p->finish_queued = true;
        fprintf(stderr, "ARCMP skills finish submitted gold=%d\n", p->finish_gold);
        return 0;
    }
    for (int slot = anim_find_first(actor); slot != -1; slot = anim_find_next(slot, actor))
        if ((anim_run_info[slot].goals[0].type == AG_USE_SKILL_ON || anim_run_info[slot].goals[0].type == AG_ATTEMPT_USE_PICKPOCKET_SKILL_ON)
            && anim_run_info[slot].goals[0].params[AGDATA_SCRATCH_OBJ].obj == gold(0)
            && anim_run_info[slot].goals[0].params[AGDATA_SKILL_DATA].data == SKILL_PICK_POCKET && anim_run_info[slot].goals[0].params[AGDATA_SCRATCH_VAL5].data > 0) {
            p->drain_started = true;
            bool started = coop_gameplay_finish(p->game), draining = coop_gameplay_draining_native(p->game);
            fprintf(stderr, "ARCMP skills finish admitted root=%d token=%d started=%d draining=%d\n", anim_run_info[slot].goals[0].type,
                anim_run_info[slot].goals[0].params[AGDATA_SCRATCH_VAL5].data, started, draining);
            if (!started || !draining) return -1;
            return 1;
        }
    return 0;
}
bool coop_skills_probe_report(CoopSkillsProbe* p)
{
    unsigned native = 0; bool counts = !p->actor_errors;
    for (unsigned phase = 1; phase <= PHASES; phase++) for (unsigned member = 0; member < 4; member++) {
        unsigned expected = p->host && (phase == 6 || phase == 7 || phase == 10 || phase == 11 || phase == 12 || phase == 14 || phase == 15 || phase == 16);
        if (p->native[phase][member] != expected) { fprintf(stderr, "ARCMP skills native count phase=%u member=%u actual=%u expected=%u\n", phase, member, p->native[phase][member], expected); counts = false; }
        native += p->native[phase][member];
    }
    unsigned spots = 0;
    for (unsigned i = 0; i < 4; i++) { spots += p->spots[i]; if (p->host ? !p->spots[i] || p->spots[i] > 20 : p->spots[i] != 0) counts = false; }
    unsigned views = skill_ui_coop_presentations() - p->feedback_base;
    bool good = p->completed == PHASES && counts && (p->host || p->replica_guard) && p->replays == 2 && p->denials == 5
        && views == (p->member == 0 ? 10u : 9u) && p->finish_native == (p->host ? 1u : 0u)
        && p->feedback_replays == (p->host ? 3u : 0u) && item_gold_get(coop_campaign_object(0)) == p->finish_gold + 100
        && p->reentrant == (p->host ? 4u : 0u) && p->retired == (p->host ? 4u : 0u) && p->healed_damage < 10
        && p->floater_expiry_checks == 1 && p->drain_clock_advanced == p->host && (!p->host || p->drain_started);
    FILE* f = fopen("arcmp-skills-result.json", "wb"); if (!f) return false;
    fprintf(f, "{\"verified\":%s,\"phases\":%u,\"member\":%d,\"native_invocations\":%u,\"native_counts_verified\":%s,\"personal_feedback\":%u,\"exact_replays\":%u,\"denials\":%u,\"finish_native\":%u,\"spot_invocations\":%u,\"feedback_replays\":%u,\"reentrant_callbacks\":%u,\"retired_callbacks\":%u,\"healed_damage\":%u,\"drain_clock_advanced\":%s,\"floater_expiry_checks\":%u,\"skills_accepted\":%u,\"skills_rejected\":%u,\"items_accepted\":%u,\"items_rejected\":%u}\n",
        good ? "true" : "false", p->completed, p->member, native, counts ? "true" : "false", views, p->replays, p->denials, p->finish_native, spots, p->feedback_replays, p->reentrant, p->retired, p->healed_damage, p->drain_clock_advanced ? "true" : "false", p->floater_expiry_checks,
        coop_skills_executed(coop_gameplay_skills(p->game)), coop_skills_rejected(coop_gameplay_skills(p->game)), coop_items_executed(coop_gameplay_items(p->game)), coop_items_rejected(coop_gameplay_items(p->game)));
    return fclose(f) == 0 && good;
}
void coop_skills_probe_destroy(CoopSkillsProbe* p)
{
    skill_invocation_observer_set(NULL, NULL);
    free(p);
}
