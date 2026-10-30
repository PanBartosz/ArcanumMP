#include "coop/use_probe.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "coop/engine_magic.h"
#include "coop/encounter_probe.h"
#include "coop/runtime.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/magictech.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/stat.h"
#include "game/script.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/item_ui.h"
#include "ui/inven_ui.h"
#include "ui/types.h"
#include "ui/written_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PHASES = 21, TAG = 798000, APPROACH_TARGET = 798500 };
struct CoopUseProbe {
    CoopSession* session; CoopGameplay* game;
    int member; bool host, framebuffer, started, sent, done, complete, replayed;
    unsigned phase, ack[4], denials, replays, completed, baseline_presentations, cancellations, view_replays, clock_checks;
    unsigned dispatch[PHASES + 2][4], null_dispatch[PHASES + 2][4], handled[PHASES + 2][4];
    unsigned actor_mismatches, stale_callbacks, reentrant_callbacks, interrupted, turn_changes, old_tokens[4];
    bool interrupt_ready, stop_sent, stale_injected[4];
    uint32_t stop_expected;
    bool pool_reserved[512], pool_failed, pool_active;
    unsigned pool_reserved_count, pool_checks, pool_results;
    uint64_t chats, at, quiet, opened_at;
    DateTime opened_clock;
    uint32_t expected;
    CoopMagicRequest request;
    int before, strength;
};
static bool find_approach(int64_t actor, int64_t item, AnimID* id, AnimRunInfo** run)
{
    for (int slot = anim_find_first(actor); slot != -1; slot = anim_find_next(slot, actor)) {
        AnimRunInfo* candidate = &anim_run_info[slot];
        if (candidate->goals[0].type != AG_USE_ITEM_ON_OBJECT
            || candidate->goals[0].params[AGDATA_SCRATCH_OBJ].obj != item
            || candidate->goals[0].params[AGDATA_SKILL_DATA].data <= 0) continue;
        *id = candidate->id; *run = candidate;
        return true;
    }
    return false;
}
static void release_pool(CoopUseProbe* p)
{
    for (unsigned i = 0; i < 512; i++) if (p->pool_reserved[i]) {
        if (magictech_run_info[i].id == (int)i && !(magictech_run_info[i].flags & MAGICTECH_RUN_ACTIVE)) magictech_run_info[i].id = -1;
        p->pool_reserved[i] = false;
    }
    p->pool_active = false;
}
static void observe_completion(void* context, int64_t source, int64_t item, int64_t target, bool accepted)
{
    (void)source; (void)item; (void)target;
    CoopUseProbe* p = context;
    if (!p->pool_active) return;
    if (accepted) p->pool_failed = true;
    else p->pool_results++;
    release_pool(p);
}
static void observe_use(void* context, int64_t source, int64_t item, int64_t target, bool allow_default)
{
    CoopUseProbe* p = context;
    if (!obj_handle_is_valid(item) || !obj_type_is_item(obj_field_int32_get(item, OBJ_F_TYPE))) return;
    int tag = obj_field_int32_get(item, OBJ_F_ITEM_PAD_I_1) - TAG;
    if (tag < 4 || tag >= (PHASES + 2) * 4) return;
    unsigned phase = (unsigned)tag / 4, member = (unsigned)tag % 4;
    if (phase == 4 && p->phase == 6) phase = 6;
    if (source != coop_campaign_object(member)) p->actor_mismatches++;
    p->dispatch[phase][member]++;
    if (!target) p->null_dispatch[phase][member]++;
    if (!allow_default) p->handled[phase][member]++;
    if (p->host && phase == 20) {
        AnimID id; AnimRunInfo* run;
        if (!find_approach(source, item, &id, &run)) {
            fprintf(stderr, "ARCMP use pressure could not find tagged approach member=%u\n", member);
            for (unsigned slot = 0; slot < 216; slot++) {
                AnimRunInfo* candidate = &anim_run_info[slot];
                if (candidate->anim_obj == source && (candidate->flags & 1))
                    fprintf(stderr, "ARCMP use pressure slot=%u root=%d flags=%x current=%d token=%d same-item=%d\n", slot, candidate->goals[0].type, candidate->flags, candidate->current_goal, candidate->goals[0].params[AGDATA_SKILL_DATA].data, candidate->goals[0].params[AGDATA_SCRATCH_OBJ].obj == item);
            }
            p->pool_failed = true; return;
        }
        /* Reenter while the native script/default boundary is live. The
         * claimed completion must neither dispatch again nor replace status. */
        unsigned token = (unsigned)run->goals[0].params[AGDATA_SKILL_DATA].data;
        p->reentrant_callbacks++;
        coop_engine_use_reached(token, &id, source, item, target, 0, false);
        if (p->dispatch[phase][member] != 1) p->pool_failed = true;
        p->pool_active = true;
        /* Reserve free native IDs without creating effects or timers. Only
         * these inactive reservations are restored after the pressure case. */
        for (unsigned i = 0; i < 512; i++) if (magictech_run_info[i].id == -1) {
            magictech_run_info[i].id = (int)i;
            if (!p->pool_reserved[i]) p->pool_reserved_count++;
            p->pool_reserved[i] = true;
        }
        MagicTechInvocation invocation;
        magictech_invocation_init(&invocation, item, obj_field_int32_get(item, OBJ_F_ITEM_SPELL_1));
        sub_4440E0(source, &invocation.target_obj);
        unsigned before = magictech_allocation_refusals();
        bool accepted = magictech_invocation_submit(&invocation);
        if (accepted || magictech_allocation_refusals() != before + 1 || magictech_can_allocate()) {
            fprintf(stderr, "ARCMP use pool check accepted=%d refusal_delta=%u\n", accepted, magictech_allocation_refusals() - before);
            p->pool_failed = true;
        } else p->pool_checks++;
    }
}
static int64_t approach_target(unsigned member)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC
            && obj_field_int32_get(obj, OBJ_F_NPC_PAD_I_1) == APPROACH_TARGET + (int)member) return obj;
    } while (obj_inst_next(&obj, &it));
    return 0;
}
static unsigned item_phase(unsigned phase) { return phase == 5 || phase == 6 ? 4 : phase; }
static int64_t find_item(unsigned phase, unsigned member)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_type_is_item(obj_field_int32_get(obj, OBJ_F_TYPE))
            && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))
            && obj_field_int32_get(obj, OBJ_F_ITEM_PAD_I_1) == TAG + (int)item_phase(phase) * 4 + (int)member) return obj;
    } while (obj_inst_next(&obj, &it));
    return 0;
}
static int prototype(unsigned phase)
{
    if (phase == 20) return BP_SCROLL_OF_AGILITY_OF_FIRE;
    if (phase == 2) return BP_LIGHT_FATIGUE_RESTORE;
    if (phase == 3 || phase == 15) return BP_SCROLL_OF_STRENGTH_OF_EARTH;
    if ((phase >= 8 && phase <= 11) || phase == 17) return phase == 11 ? BP_VENDIGROTH_NEWSPAPER : BP_GORADS_NOTE;
    if (phase == 12 || phase == 13) return BP_COMPOUND_BOW_SCHEMATIC;
    if (phase == 14) return BP_HEALING_SALVE;
    return BP_HEAL_LESSER_WOUNDS;
}
static bool prepare(CoopUseProbe* p, unsigned phase)
{
    if (phase != 5 && phase != 6) for (unsigned i = 0; i < 4; i++) {
        if (phase >= 8 && phase <= 11 && i != phase - 8) continue;
        int64_t pc = coop_campaign_object(i), item;
        if (!object_create(sub_4685A0(prototype(phase)), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)) return false;
        /* The bare newspaper prototype has line 0 (the EXTRA! decoration).
         * Real Vendigroth copies carry an article; 1280 is its retail story. */
        if (phase == 11) obj_field_int32_set(item, OBJ_F_WRITTEN_TEXT_START_LINE, 1280);
        obj_field_int32_set(item, OBJ_F_ITEM_PAD_I_1, TAG + (int)phase * 4 + (int)i);
        if (!item_transfer(item, pc)) return false;
        if (phase == 20) obj_field_int32_set(item, OBJ_F_ITEM_FLAGS, obj_field_int32_get(item, OBJ_F_ITEM_FLAGS) | OIF_NO_RANGED_USE);
        if (phase == 16) {
            /* A real retail potion script handles use and suppresses default.
             * Unusable default metadata must not prevent that script. */
            obj_field_int32_set(item, OBJ_F_ITEM_FLAGS, obj_field_int32_get(item, OBJ_F_ITEM_FLAGS) | OIF_IS_MAGICAL);
            obj_field_int32_set(item, OBJ_F_ITEM_SPELL_1, 10000);
        }
        if (phase == 18) {
            int64_t target, origin = obj_field_int64_get(pc, OBJ_F_LOCATION);
            int64_t loc = location_make(location_get_x(origin) + 16, location_get_y(origin) - 16);
            if (!object_create(sub_4685A0(BP_WOLF), loc, &target)) return false;
            Script none = {0};
            for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(target, OBJ_F_SCRIPTS_IDX, sap, &none);
            obj_field_int32_set(target, OBJ_F_NPC_PAD_I_1, APPROACH_TARGET + (int)i);
            obj_field_int32_set(target, OBJ_F_CRITTER_FLAGS, obj_field_int32_get(target, OBJ_F_CRITTER_FLAGS) | OCF_PARALYZED | OCF_FATIGUE_IMMUNE);
            if (!coop_encounter_place_far(target, loc)) return false;
        }
        if (phase == 15) {
            obj_field_int32_set(item, OBJ_F_ITEM_SPELL_MANA_STORE, 0);
            obj_field_int32_set(item, OBJ_F_ITEM_FLAGS, obj_field_int32_get(item, OBJ_F_ITEM_FLAGS) | OIF_IS_MAGICAL);
        }
        if (phase == 14) {
            unsigned before = obj_field_int32_get(item, OBJ_F_ITEM_FLAGS);
            int charges = obj_field_int32_get(item, OBJ_F_ITEM_SPELL_MANA_STORE);
            int spell = obj_field_int32_get(item, OBJ_F_ITEM_SPELL_1);
            if (!magictech_can_charge_spell_fatigue(item, spell)
                || before != (unsigned)obj_field_int32_get(item, OBJ_F_ITEM_FLAGS)
                || charges != obj_field_int32_get(item, OBJ_F_ITEM_SPELL_MANA_STORE)) return false;
        }
        fprintf(stderr, "ARCMP use supply phase=%u member=%u proto=%d flags=%x spell=%d charges=%d\n", phase, i, prototype(phase), obj_field_int32_get(item, OBJ_F_ITEM_FLAGS), obj_field_int32_get(item, OBJ_F_ITEM_SPELL_1), obj_field_int32_get(item, OBJ_F_ITEM_SPELL_MANA_STORE));
        if (phase == 1 || phase == 4 || phase == 7 || phase == 14 || phase == 16 || phase == 18 || phase == 19 || phase == 21) obj_field_int32_set(pc, OBJ_F_HP_DAMAGE, 10);
        if (phase == 2) critter_fatigue_damage_set(pc, critter_fatigue_max(pc) / 2);
    }
    char text[48]; snprintf(text, sizeof(text), "use-phase:%u", phase);
    return coop_session_chat(p->session, text);
}
CoopUseProbe* coop_use_probe_create(CoopSession* s, CoopGameplay* g, int member, bool framebuffer)
{
    CoopUseProbe* p = calloc(1, sizeof(*p));
    if (p) {
        p->session = s; p->game = g; p->member = member; p->host = coop_session_is_host(s); p->framebuffer = framebuffer; p->baseline_presentations = written_ui_coop_presentations();
        item_use_script_observer_set(observe_use, p);
        item_use_completion_observer_set(observe_completion, p);
        if (p->host) {
            int candidates[] = { BP_KERGHANS_DEATH_SCROLL, BP_KERGHANS_ATTACK_SCROLL, BP_SCROLL_OF_EXITING, BP_CRYSTAL_BALL, BP_HEARTSTONE, BP_PAMPHLET };
            for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
                int64_t proto = sub_4685A0(candidates[i]); Script sap = {0};
                if (!proto) continue;
                obj_arrayfield_script_get(proto, OBJ_F_SCRIPTS_IDX, SAP_USE, &sap);
                fprintf(stderr, "ARCMP quest item proto=%d use-script=%d flags=%x spell=%d charges=%d\n", candidates[i], sap.num, obj_field_int32_get(proto, OBJ_F_ITEM_FLAGS), obj_field_int32_get(proto, OBJ_F_ITEM_SPELL_1), obj_field_int32_get(proto, OBJ_F_ITEM_SPELL_MANA_STORE));
            }
        }
    }
    return p;
}
static bool capture(CoopUseProbe* p)
{
    if (!p->framebuffer) return true;
    iso_interface_refresh(); gamelib_redraw(); iso_redraw(); tig_window_display();
    char path[80];
    if (p->phase == 17) snprintf(path, sizeof(path), "arcmp-use-untargeted.bmp");
    else snprintf(path, sizeof(path), "arcmp-use-reading-%u.bmp", p->phase - 8);
    return tig_video_screenshot_save(path) == TIG_OK;
}
static bool schema_known(int64_t pc, int schematic)
{
    for (int i = 0; i < obj_arrayfield_length_get(pc, OBJ_F_PC_SCHEMATICS_FOUND_IDX); i++)
        if ((int)obj_arrayfield_uint32_get(pc, OBJ_F_PC_SCHEMATICS_FOUND_IDX, i) == schematic) return true;
    return false;
}
int coop_use_probe_poll(CoopUseProbe* p, uint64_t now, bool start)
{
    for (; p->chats < coop_session_chat_count(p->session); p->chats++) {
        const CoopChat* c = coop_session_chat_at(p->session, p->chats); unsigned phase;
        if (!c->slot && sscanf(c->text, "use-phase:%u", &phase) == 1) {
            if (phase != p->phase + 1 || phase > PHASES) return -1;
            p->phase = phase; p->at = now; p->quiet = p->opened_at = 0; p->sent = p->done = p->replayed = false;
            p->interrupt_ready = p->stop_sent = false;
            unsigned status; p->expected = coop_magic_last_result(coop_gameplay_magic(p->game), &status) + 1;
            written_ui_coop_stop(); inven_ui_coop_stop();
            fprintf(stderr, "ARCMP use phase=%u member=%d\n", phase, p->member);
        }
        if (sscanf(c->text, "use-done:%u", &phase) == 1 && c->slot < 4) p->ack[c->slot] = phase;
        if (!c->slot && !strcmp(c->text, "use-interrupt-ready")) p->interrupt_ready = true;
    }
    if (p->host && start && !p->started) { p->started = true; if (!prepare(p, 1)) return -1; }
    if (!p->phase) return 0;
    if (now - p->at > 20000) {
        unsigned status; uint32_t result = coop_magic_last_result(coop_gameplay_magic(p->game), &status);
        fprintf(stderr, "ARCMP use timeout phase=%u member=%d sent=%d done=%d result=%u status=%u expected=%u hp=%d fatigue=%d mode=%d\n", p->phase, p->member, p->sent, p->done, result, status, p->expected, obj_field_int32_get(player_get_local_pc_obj(), OBJ_F_HP_DAMAGE), critter_fatigue_damage_get(player_get_local_pc_obj()), intgame_mode_get());
        return -1;
    }
    if (!p->done && now - p->at > 1200) {
        int64_t pc = player_get_local_pc_obj(), item = find_item(p->phase, (unsigned)p->member);
        bool reading = (p->phase >= 8 && p->phase <= 11) || p->phase == 17;
        bool reader = reading && (p->phase == 17 || (unsigned)p->member == p->phase - 8);
        if (!p->sent && (!reading || reader) && item) {
            p->before = p->phase == 2 ? critter_fatigue_damage_get(pc) : obj_field_int32_get(pc, OBJ_F_HP_DAMAGE);
            p->strength = stat_level_get(pc, STAT_STRENGTH);
            p->request = (CoopMagicRequest){ .epoch = 1, .sequence = p->expected, .map = (uint32_t)map_current_map(), .operation = COOP_MAGIC_USE_ITEM, .generation = combat_turn_generation_get() };
            int member = coop_campaign_slot_for_object(pc);
            memcpy(p->request.actor, coop_campaign_member((unsigned)member)->object_id.d.g.data, 16);
            int64_t source = p->phase == 5 ? find_item(4, ((unsigned)p->member + 1) % 4) : item;
            if (!source || !coop_object_id_from_engine(obj_get_id(source), &p->request.source) || !coop_object_id_from_engine(obj_get_id(pc), &p->request.target)) return -1;
            if (p->phase == 18 || p->phase == 21) {
                int64_t target = approach_target((unsigned)p->member);
                if (!target || !coop_object_id_from_engine(obj_get_id(target), &p->request.target)) return -1;
            }
            if (p->phase == 17) memset(&p->request.target, 0, sizeof(p->request.target));
            if (p->phase == 4) p->request.generation = p->request.generation ? p->request.generation - 1 : 1;
            TargetDescriptor td; target_descriptor_set_obj(&td, pc);
            if (p->phase == 1) {
                if (!inven_ui_open(pc, 0, INVEN_UI_MODE_INVENTORY)) return -1;
                sub_573630(item);
                gamelib_redraw(); iso_redraw(); tig_window_display();
                if (p->framebuffer && tig_video_screenshot_save("arcmp-use-inventory.bmp") != TIG_OK) return -1;
                TigMessage msg = { .type = TIG_MESSAGE_MOUSE };
                msg.data.mouse.x = 710; msg.data.mouse.y = 340; msg.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN;
                tig_message_enqueue(&msg); msg.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP;
                tig_message_enqueue(&msg); p->sent = true;
            } else if (p->phase == 3) {
                item_ui_activate(pc, item);
                if (intgame_mode_get() != INTGAME_MODE_ITEM || coop_magic_busy(coop_gameplay_magic(p->game))) return -1;
                item_ui_deactivate(); p->cancellations++;
                if (coop_magic_busy(coop_gameplay_magic(p->game)) || !find_item(p->phase, (unsigned)p->member)) return -1;
                item_ui_activate(pc, item); item_ui_apply(&td); p->sent = coop_magic_busy(coop_gameplay_magic(p->game));
            } else if (p->phase == 14) {
                Hotkey hotkey = { .type = HOTKEY_ITEM };
                sub_4440E0(item, &hotkey.item_obj); intgame_hotkey_activate(&hotkey);
                if (intgame_mode_get() == INTGAME_MODE_ITEM) item_ui_apply(&td);
                p->sent = coop_magic_busy(coop_gameplay_magic(p->game));
            } else if (p->phase == 2 || reading) p->sent = item_ui_coop_use(item, p->phase != 17);
            else p->sent = coop_gameplay_magic_request(p->game, &p->request);
            if (!p->sent) return -1;
        }
        if (p->phase == 1 && p->sent && intgame_mode_get() == INTGAME_MODE_ITEM && !coop_magic_busy(coop_gameplay_magic(p->game))) {
            TargetDescriptor td; target_descriptor_set_obj(&td, pc); item_ui_apply(&td);
        }
        if ((p->phase == 18 || p->phase == 21) && p->host && !p->interrupt_ready) {
            bool all = true;
            for (unsigned i = 0; i < 4; i++) {
                AnimID id; AnimRunInfo* run;
                if (!find_approach(coop_campaign_object(i), find_item(p->phase, i), &id, &run)) { all = false; continue; }
                p->old_tokens[i] = (unsigned)run->goals[0].params[AGDATA_SKILL_DATA].data;
            }
            if (all) {
                if (p->phase == 21) {
                    /* A normal policy change interrupts the native approach
                     * and invalidates every admitted action's generation. */
                    unsigned generation = combat_turn_generation_get();
                    if (combat_is_turn_based() || !sub_4B6C90(true) || !sub_4B6C90(false)
                        || generation == combat_turn_generation_get()) return -1;
                    p->turn_changes++;
                } else if (!coop_session_chat(p->session, "use-interrupt-ready")) return -1;
                p->interrupt_ready = true;
            }
        }
        if (p->phase == 18 && p->sent && p->interrupt_ready && !p->stop_sent) {
            bool accepted;
            p->stop_expected = coop_gameplay_last_result(p->game, &accepted) + 1;
            p->stop_sent = coop_runtime_halt(obj_field_int64_get(pc, OBJ_F_LOCATION));
        }
        if (p->phase == 19 && p->host) {
            for (unsigned i = 0; i < 4; i++) {
                AnimID id; AnimRunInfo* run; int64_t actor = coop_campaign_object(i), source = find_item(19, i);
                if (p->stale_injected[i] || !source || !find_approach(actor, source, &id, &run)) continue;
                if (!p->old_tokens[i] || (unsigned)run->goals[0].params[AGDATA_SKILL_DATA].data == p->old_tokens[i] || p->dispatch[19][i]) return -1;
                /* All current native fields match; only the retired action
                 * token is wrong. It must not invoke SAP_USE. */
                coop_engine_use_reached(p->old_tokens[i], &id, actor, source, actor, 0, false);
                if (p->dispatch[19][i]) return -1;
                p->stale_injected[i] = true; p->stale_callbacks++;
            }
        }
        unsigned status = 0;
        bool received = coop_magic_last_result(coop_gameplay_magic(p->game), &status) == p->expected;
        bool ready = false;
        if (reading) {
            unsigned type = 0; int num = 0; bool vendi = false;
            bool open = written_ui_coop_state(&type, &num, &vendi);
            if (reader && received && !status && open && item) {
                if (type != (unsigned)obj_field_int32_get(item, OBJ_F_WRITTEN_SUBTYPE) || num != obj_field_int32_get(item, OBJ_F_WRITTEN_TEXT_START_LINE) || vendi != (p->phase == 11)) return -1;
                if (!p->opened_at) { p->opened_at = now; p->opened_clock = sub_45A7C0(); }
                if (now - p->opened_at >= 700) {
                    DateTime clock = sub_45A7C0();
                    if (datetime_compare(&clock, &p->opened_clock) <= 0) return -1;
                    p->clock_checks++;
                    ready = capture(p); if (!ready) return -1;
                }
            } else if (!reader && now - p->at > 2200) { if (open) return -1; ready = capture(p); if (!ready) return -1; }
        } else if (received) {
            unsigned wanted = (p->phase == 4 || p->phase == 21) ? COOP_MAGIC_STALE : p->phase == 5 ? COOP_MAGIC_SOURCE : (p->phase == 15 || p->phase == 18 || p->phase == 20) ? COOP_MAGIC_REFUSED : COOP_MAGIC_OK;
            if (status != wanted) { fprintf(stderr, "ARCMP use unexpected phase=%u status=%u wanted=%u\n", p->phase, status, wanted); return -1; }
            if (p->phase == 18) {
                bool accepted = false;
                if (!p->interrupt_ready || !p->stop_sent) return -1;
                ready = coop_gameplay_last_result(p->game, &accepted) == p->stop_expected && accepted && item && obj_field_int32_get(pc, OBJ_F_HP_DAMAGE) == p->before;
                if (ready) { p->denials++; p->interrupted++; }
            } else if (p->phase == 4 || p->phase == 5 || p->phase == 15 || p->phase == 20 || p->phase == 21) { ready = item && obj_field_int32_get(pc, OBJ_F_HP_DAMAGE) == p->before; if (ready) p->denials++; }
            else if (p->phase == 3) ready = !item && stat_level_get(pc, STAT_STRENGTH) > p->strength;
            else if (p->phase == 12 || p->phase == 13) {
                int64_t proto = sub_4685A0(prototype(p->phase));
                ready = schema_known(pc, obj_field_int32_get(proto, OBJ_F_WRITTEN_TEXT_START_LINE)) && (p->phase == 12 ? !item : item != 0);
            } else ready = !item && (p->phase == 2 ? critter_fatigue_damage_get(pc) < p->before : obj_field_int32_get(pc, OBJ_F_HP_DAMAGE) < p->before);
            if (ready && p->phase == 7 && !p->replayed) {
                uint8_t bytes[COOP_MAGIC_REQUEST_BYTES];
                if (!coop_magic_request_encode(&p->request, bytes) || !(p->host ? coop_magic_message(coop_gameplay_magic(p->game), 0, COOP_MAGIC_REQUEST, bytes, sizeof(bytes)) : coop_session_send(p->session, 0, COOP_MAGIC_REQUEST, bytes, sizeof(bytes)))) return -1;
                p->replayed = true; p->replays++; p->quiet = now; ready = false;
            }
            if (p->phase == 7 && now - p->quiet < 700) ready = false;
        }
        if (ready) {
            written_ui_coop_stop(); inven_ui_coop_stop();
            char text[48]; snprintf(text, sizeof(text), "use-done:%u", p->phase);
            if (!coop_session_chat(p->session, text)) return -1;
            p->done = true; p->completed++;
        }
    }
    if (p->host && p->done) {
        bool all = true; for (unsigned i = 0; i < 4; i++) all &= p->ack[i] == p->phase;
        if (!all) return 0;
        if (p->phase >= 9 && p->phase <= 11 && !p->replayed) {
            uint8_t bytes[COOP_WRITTEN_BYTES];
            if (!coop_magic_written_last(coop_gameplay_magic(p->game), (uint8_t)(p->phase - 8), bytes)
                || !coop_session_send(p->session, (uint8_t)(p->phase - 8), COOP_WRITTEN_VIEW, bytes, sizeof(bytes))) return -1;
            p->view_replays++; p->replayed = true; p->quiet = now;
        }
        if (!p->quiet) p->quiet = now;
        if (now - p->quiet < 700) return 0;
        if (p->phase == PHASES) {
            release_pool(p);
            if (!magictech_can_allocate()) return -1;
            p->complete = true; return 1;
        }
        if (!prepare(p, p->phase + 1)) return -1;
        p->ack[0] = 0;
    }
    return p->complete ? 1 : 0;
}
bool coop_use_probe_report(CoopUseProbe* p)
{
    unsigned views = written_ui_coop_presentations() - p->baseline_presentations;
    uint8_t last[COOP_WRITTEN_BYTES];
    bool personal = coop_magic_written_last(coop_gameplay_magic(p->game), coop_session_local_slot(p->session), last)
        && coop_object_read32(last + 4) == 4 && last[32] == WRITTEN_TYPE_NOTE && !last[33];
    bool drained = !find_item(PHASES + 1, 0) && obj_field_int32_get(coop_campaign_object(0), OBJ_F_HP_DAMAGE) < 10 && !coop_gameplay_draining_native(p->game);
    bool scripts = !p->actor_mismatches;
    unsigned dispatches = 0, nulls = 0, handled = 0;
    for (unsigned phase = 1; phase <= PHASES + 1; phase++) for (unsigned member = 0; member < 4; member++) {
        unsigned expected = 0;
        if (p->host) {
            if (phase == PHASES + 1) expected = member == 0;
            else if (phase == 4 || phase == 5 || phase == 18 || phase == 21) expected = 0;
            else if (phase >= 8 && phase <= 11) expected = member == phase - 8;
            else expected = 1;
        }
        if (p->dispatch[phase][member] != expected) {
            fprintf(stderr, "ARCMP use dispatch mismatch phase=%u member=%u count=%u expected=%u\n", phase, member, p->dispatch[phase][member], expected);
            scripts = false;
        }
        dispatches += p->dispatch[phase][member];
        if (phase == 17) nulls += p->null_dispatch[phase][member];
        if (phase == 16) handled += p->handled[phase][member];
    }
    bool good = p->completed == PHASES && drained && p->denials == 6 && p->interrupted == 1 && p->replays == 1
        && p->cancellations == 1 && p->clock_checks == 2 && personal && views == 2 && scripts
        && nulls == (p->host ? 4u : 0u) && handled == (p->host ? 4u : 0u) && p->stale_callbacks == (p->host ? 4u : 0u)
        && !p->pool_failed && p->pool_checks == (p->host ? 4u : 0u) && p->pool_results == p->pool_checks
        && p->turn_changes == (p->host ? 1u : 0u) && p->reentrant_callbacks == (p->host ? 4u : 0u) && (!p->host || p->view_replays == 3);
    FILE* f = fopen("arcmp-use-result.json", "wb"); if (!f) return false;
    fprintf(f, "{\"verified\":%s,\"phases\":%u,\"member\":%d,\"rejected_requests\":%u,\"exact_replays\":%u,\"target_cancellations\":%u,\"reading_replays_sent\":%u,\"reading_clock_checks\":%u,\"personal_reading_views\":%u,\"personal_views\":%u,\"native_use_drained\":%s,\"script_dispatches\":%u,\"script_dispatch_counts_verified\":%s,\"null_target_dispatches\":%u,\"script_handled_invalid_default\":%u,\"interrupted_approaches\":%u,\"stale_completion_callbacks\":%u,\"reentrant_completion_callbacks\":%u,\"pool_pressure_checks\":%u,\"pool_reserved_slots\":%u,\"turn_policy_changes\":%u,\"executed\":%u,\"rejected\":%u}\n",
        good ? "true" : "false", p->completed, p->member, p->denials, p->replays, p->cancellations, p->view_replays,
        p->clock_checks, views, personal ? 4u : 0u, drained ? "true" : "false", dispatches, scripts ? "true" : "false",
        nulls, handled, p->interrupted, p->stale_callbacks, p->reentrant_callbacks, p->pool_checks, p->pool_reserved_count, p->turn_changes, coop_magic_executed(coop_gameplay_magic(p->game)), coop_magic_rejected(coop_gameplay_magic(p->game)));
    return fclose(f) == 0 && good;
}
void coop_use_probe_destroy(CoopUseProbe* p)
{
    if (p) { item_use_script_observer_set(NULL, NULL); item_use_completion_observer_set(NULL, NULL); release_pool(p); }
    free(p);
}
bool coop_use_probe_finish(CoopUseProbe* p)
{
    int64_t pc = coop_campaign_object(0), item;
    if (!p->host || !object_create(sub_4685A0(BP_HEAL_LESSER_WOUNDS), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)) return false;
    obj_field_int32_set(item, OBJ_F_ITEM_PAD_I_1, TAG + (PHASES + 1) * 4);
    obj_field_int32_set(pc, OBJ_F_HP_DAMAGE, 10);
    if (!item_transfer(item, pc) || !anim_goal_use_item_on_obj(pc, pc, item, 0)) return false;
    return !coop_engine_use_settled() && coop_gameplay_finish(p->game) && coop_gameplay_draining_native(p->game);
}
