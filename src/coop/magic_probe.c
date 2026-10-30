#include "coop/magic_probe.h"
#include "coop/campaign.h"
#include "coop/encounter_probe.h"
#include "coop/engine_character.h"
#include "coop/engine_magic.h"
#include "coop/engine_object.h"
#include "coop/runtime.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/location.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/script.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/stat.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/spell_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TARGET_TAG = 799000,
    LAST_PHASE = 16 };
struct CoopMagicProbe {
    CoopSession* session;
    CoopGameplay* game;
    int local;
    bool host, framebuffer, active, sent, done, complete, failed;
    unsigned phase, state, ack[4], denials, replayed, tb_spent;
    uint64_t chats, phase_at, quiet_at, old_token, new_token;
    uint32_t expected, end_expected, move_expected;
    CoopMagicRequest last_request;
    int base_strength[4], damage[4], hp_before[4];
    uint64_t old_runs[8];
    unsigned reused_slots;
    int64_t old_position;
    unsigned overlay_frames, overlay_clears;
    bool overlay_active, overlay_captured;
};
bool coop_magic_probe_character(unsigned player, CoopCharacter* build)
{
    if (!coop_encounter_character(player, build)) return false;
    build->stats[STAT_DEXTERITY] -= 2;
    build->stats[STAT_STRENGTH]--;
    build->skills[BASIC_SKILL_MELEE] = 0;
    const int colleges[] = { COLLEGE_EARTH, COLLEGE_FIRE, COLLEGE_FORCE, COLLEGE_NECROMANTIC_BLACK, COLLEGE_NECROMANTIC_WHITE };
    for (unsigned i = 0; i < SDL_arraysize(colleges); i++)
        build->colleges[colleges[i]] = 1;
    snprintf(build->name, sizeof(build->name), "Spell hero %u", player);
    return coop_engine_character_validate(build);
}
static int64_t target(unsigned member)
{
    int64_t obj;
    int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
            if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && obj_field_int32_get(obj, OBJ_F_NPC_PAD_I_1) == TARGET_TAG + (int)member) return obj;
        } while (obj_inst_next(&obj, &iterator));
    return OBJ_HANDLE_NULL;
}
static bool place_visible_target(int64_t pc, int64_t obj)
{
    static const int offsets[][2] = { { 0, 6 }, { 6, 0 }, { 0, -6 }, { -6, 0 }, { 6, 6 }, { -6, 6 }, { 6, -6 }, { -6, -6 }, { 0, 9 }, { 9, 0 }, { 0, -9 }, { -9, 0 } };
    int64_t origin = obj_field_int64_get(pc, OBJ_F_LOCATION);
    for (unsigned i = 0; i < SDL_arraysize(offsets); i++) {
        int64_t loc = location_make(location_get_x(origin) + offsets[i][0], location_get_y(origin) + offsets[i][1]);
        if (!coop_encounter_place_far(obj, loc)) continue;
        bool clear = true;
        for (unsigned member = 0; member < 4; member++)
            clear &= object_dist(coop_campaign_object(member), obj) >= 4;
        MagicTechInvocation invocation;
        magictech_invocation_init(&invocation, pc, SPELL_HARM);
        sub_4440E0(obj, &invocation.target_obj);
        if (clear && object_dist(pc, obj) <= magictech_get_range(SPELL_HARM) && magictech_check_los(&invocation)) return true;
    }
    return false;
}
static bool idle(int64_t pc)
{
    AnimID current, fidget;
    return !sub_423300(pc, &current) || (anim_is_current_goal_type(pc, AG_ANIM_FIDGET, &fidget) && anim_id_is_equal(&current, &fidget));
}
static bool settled(void)
{
    for (unsigned i = 0; i < 4; i++)
        if (!idle(coop_campaign_object(i))) return false;
    int64_t obj;
    int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
            if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_PROJECTILE
                && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return false;
        } while (obj_inst_next(&obj, &iterator));
    return true;
}
static bool announce(CoopMagicProbe* p, unsigned phase)
{
    char text[48];
    snprintf(text, sizeof(text), "magic-phase:%u", phase);
    return coop_session_chat(p->session, text);
}
static bool capture(CoopMagicProbe* p, const char* stage)
{
    if (!p->framebuffer) return true;
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    iso_interface_refresh();
    intgame_show();
    gamelib_redraw();
    iso_redraw();
    tig_window_display();
    char path[80];
    snprintf(path, sizeof(path), "arcmp-magic-%s.bmp", stage);
    return tig_video_screenshot_save(path) == TIG_OK;
}
static bool observe_overlays(CoopMagicProbe* p)
{
    bool local_active = false;
    for (unsigned member = 0; member < 4; member++) {
        int64_t pc = coop_campaign_object(member);
        unsigned expected = 0;
        for (unsigned slot = 0; slot < 7; slot++)
            if (obj_arrayfield_uint32_get(pc, OBJ_F_OVERLAY_FORE, slot) != TIG_ART_ID_INVALID
                || obj_arrayfield_uint32_get(pc, OBJ_F_OVERLAY_BACK, slot) != TIG_ART_ID_INVALID) expected |= OF_HAS_OVERLAYS;
        for (unsigned slot = 0; slot < 4; slot++)
            if (obj_arrayfield_uint32_get(pc, OBJ_F_UNDERLAY, slot) != TIG_ART_ID_INVALID) expected |= OF_HAS_UNDERLAYS;
        if (((unsigned)obj_field_int32_get(pc, OBJ_F_FLAGS) & (OF_HAS_OVERLAYS | OF_HAS_UNDERLAYS)) != expected) {
            fprintf(stderr, "ARCMP native overlay visibility mismatch member=%u phase=%u expected=%x\n", member, p->phase, expected);
            return false;
        }
        if (member == (unsigned)p->local) local_active = expected != 0;
    }
    if (local_active) {
        p->overlay_frames++;
        if (!p->overlay_captured) {
            if (!capture(p, "fx")) return false;
            p->overlay_captured = true;
            fprintf(stderr, "ARCMP native overlay visible member=%d phase=%u\n", p->local, p->phase);
        }
    } else if (p->overlay_active) {
        p->overlay_clears++;
    }
    p->overlay_active = local_active;
    return true;
}
static const CoopMagicEntry* maintained(CoopMagicProbe* p, int spell)
{
    const CoopMagicView* view = coop_engine_magic_view();
    ObjectID owner = obj_get_id(coop_campaign_object((unsigned)p->local));
    for (unsigned i = 0; i < view->count; i++)
        if (view->entries[i].spell == spell && !memcmp(view->entries[i].owner, owner.d.g.data, 16)) return &view->entries[i];
    return NULL;
}
static bool result(CoopMagicProbe* p, unsigned wanted)
{
    unsigned status;
    if (coop_magic_last_result(coop_gameplay_magic(p->game), &status) != p->expected) return false;
    if (status != wanted) {
        fprintf(stderr, "ARCMP magic unexpected phase=%u member=%d state=%u status=%u wanted=%u\n", p->phase, p->local, p->state, status, wanted);
        p->failed = true;
        return false;
    }
    return true;
}
static bool cast_ui(CoopMagicProbe* p, int spell, TargetDescriptor td)
{
    unsigned status;
    p->expected = coop_magic_last_result(coop_gameplay_magic(p->game), &status) + 1;
    if (spell == SPELL_ORCISH_CHAMPION) {
        int64_t pc = player_get_local_pc_obj(), origin = obj_field_int64_get(pc, OBJ_F_LOCATION);
        bool picked = false;
        for (int y = -3; y <= 3 && !picked; y++)
            for (int x = -3; x <= 3 && !picked; x++) {
                if (!x && !y) continue;
                TargetParams params = magictech_spells[spell].target_params[MAGICTECH_ACTION_BEGIN];
                TargetContext ctx = { 0 };
                target_context_init(&ctx, NULL, pc);
                ctx.params = &params;
                ctx.target_loc = ctx.orig_target_loc = location_make(location_get_x(origin) + x, location_get_y(origin) + y);
                MagicTechInvocation invocation;
                magictech_invocation_init(&invocation, pc, spell);
                invocation.target_loc = ctx.target_loc;
                if (target_context_evaluate(&ctx) && magictech_check_los(&invocation)) {
                    target_descriptor_set_loc(&td, ctx.target_loc);
                    picked = true;
                }
            }
        if (!picked) return false;
    }
    spell_ui_activate(player_get_local_pc_obj(), spell);
    if (!coop_magic_busy(coop_gameplay_magic(p->game))) spell_ui_apply(&td);
    bool submitted = coop_magic_busy(coop_gameplay_magic(p->game));
    spell_ui_cancel();
    return submitted;
}
static CoopMagicRequest request(CoopMagicProbe* p)
{
    int64_t pc = player_get_local_pc_obj();
    CoopMagicRequest r = { .epoch = coop_runtime_epoch(), .map = (uint32_t)map_current_map(), .generation = combat_turn_generation_get() };
    memcpy(r.actor, obj_get_id(pc).d.g.data, 16);
    coop_object_id_from_engine(obj_get_id(pc), &r.source);
    coop_object_id_from_engine(obj_get_id(pc), &r.target);
    return r;
}
static bool submit(CoopMagicProbe* p, CoopMagicRequest r)
{
    unsigned status;
    p->expected = coop_magic_last_result(coop_gameplay_magic(p->game), &status) + 1;
    r.sequence = p->expected;
    p->last_request = r;
    return coop_gameplay_magic_request(p->game, &r);
}
CoopMagicProbe* coop_magic_probe_create(CoopSession* session, CoopGameplay* game, int local, bool framebuffer)
{
    CoopMagicProbe* p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->session = session;
    p->game = game;
    p->local = local;
    p->host = coop_session_is_host(session);
    p->framebuffer = framebuffer;
    return p;
}
bool coop_magic_probe_start(CoopMagicProbe* p)
{
    if (!p || !p->host || p->active) return false;
    int64_t origin = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        if (!coop_encounter_place_far(pc, location_make(location_get_x(origin) + 8 * i, location_get_y(origin) + 2))) return false;
        stat_base_set(pc, STAT_INTELLIGENCE, 20);
        stat_base_set(pc, STAT_WILLPOWER, 20);
        critter_fatigue_pts_set(pc, 200);
        critter_fatigue_damage_set(pc, 0);
        p->base_strength[i] = stat_level_get(pc, STAT_STRENGTH);
        object_hp_damage_set(pc, 20);
        p->hp_before[i] = object_hp_damage_get(pc);
        int64_t t = 0, loc = obj_field_int64_get(pc, OBJ_F_LOCATION);
        loc = location_make(location_get_x(loc), location_get_y(loc) + 8);
        if (!object_create(sub_4685A0(BP_WOLF), loc, &t)) return false;
        Script none = { 0 };
        for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++)
            obj_arrayfield_script_set(t, OBJ_F_SCRIPTS_IDX, sap, &none);
        obj_field_int32_set(t, OBJ_F_NPC_PAD_I_1, TARGET_TAG + (int)i);
        obj_field_int32_set(t, OBJ_F_HP_PTS, 3000);
        obj_field_int32_set(t, OBJ_F_CRITTER_FLAGS, obj_field_int32_get(t, OBJ_F_CRITTER_FLAGS) | OCF_PARALYZED | OCF_FATIGUE_IMMUNE);
        if (!coop_encounter_place_far(t, loc)) return false;
    }
    for (unsigned i = 0; i < 4; i++)
        if (!place_visible_target(coop_campaign_object(i), target(i))) return false;
    return announce(p, 1);
}
static bool host_condition(CoopMagicProbe* p)
{
    const CoopMagicView* view = coop_engine_magic_view();
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        if (p->phase == 1 && object_hp_damage_get(pc) >= p->hp_before[i]) return false;
        if ((p->phase == 2 || p->phase == 3 || p->phase == 5 || p->phase == 6) && stat_level_get(pc, STAT_STRENGTH) <= p->base_strength[i]) return false;
        if ((p->phase == 4 || p->phase == 7) && stat_level_get(pc, STAT_STRENGTH) != p->base_strength[i]) return false;
        if ((p->phase == 8 || p->phase == 9 || p->phase == 13) && object_hp_damage_get(target(i)) <= p->damage[i]) return false;
    }
    if ((p->phase == 2 || p->phase == 3 || p->phase == 5 || p->phase == 6 || p->phase == 10) && view->count != 4) return false;
    if ((p->phase == 4 || p->phase == 7 || p->phase == 11) && view->count) return false;
    if (p->phase == 10) {
        for (unsigned i = 0; i < view->count; i++) {
            int64_t summoned = obj_pool_perm_lookup(coop_object_id_to_engine(view->entries[i].target));
            if (view->entries[i].spell != SPELL_ORCISH_CHAMPION || !summoned
                || obj_field_int32_get(summoned, OBJ_F_TYPE) != OBJ_TYPE_NPC
                || coop_campaign_slot_for_object(obj_field_handle_get(summoned, OBJ_F_NPC_LEADER)) < 0) return false;
        }
    }
    if (p->phase >= 15 && critter_is_dead(coop_campaign_object(0))) return false;
    return true;
}
int coop_magic_probe_poll(CoopMagicProbe* p, uint64_t now)
{
    if (!p || p->failed) return -1;
    for (; p->chats < coop_session_chat_count(p->session); p->chats++) {
        const CoopChat* c = coop_session_chat_at(p->session, p->chats);
        unsigned phase;
        if (!c->slot && sscanf(c->text, "magic-phase:%u", &phase) == 1) {
            if (phase != p->phase + 1 || phase > LAST_PHASE) return -1;
            p->phase = phase;
            p->phase_at = now;
            p->quiet_at = 0;
            p->state = 0;
            p->sent = p->done = false;
            p->active = true;
            p->old_position = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
            for (unsigned i = 0; i < 4; i++)
                p->damage[i] = object_hp_damage_get(target(i));
            if (phase == 1)
                for (unsigned i = 0; i < 4; i++)
                    p->base_strength[i] = stat_level_get(coop_campaign_object(i), STAT_STRENGTH);
            fprintf(stderr, "ARCMP magic phase=%u member=%d\n", phase, p->local);
        }
        if (sscanf(c->text, "magic-done:%u", &phase) == 1 && c->slot < 4) p->ack[c->slot] = phase;
    }
    if (!p->active) return 0;
    if (!observe_overlays(p)) return -1;
    if (now - p->phase_at > 30000) {
        fprintf(stderr, "ARCMP magic timeout phase=%u member=%d state=%u sent=%d view=%u\n", p->phase, p->local, p->state, p->sent, coop_engine_magic_view()->count);
        return -1;
    }
    int64_t pc = player_get_local_pc_obj();
    TargetDescriptor td;
    target_descriptor_set_obj(&td, pc);
    bool ready = false;
    if (!p->done) {
        if (p->phase == 1 || p->phase == 2 || p->phase == 5 || p->phase == 8 || p->phase == 9 || p->phase == 10) {
            int spell = p->phase == 1 ? SPELL_MINOR_HEALING : p->phase == 8 ? SPELL_HARM
                : p->phase == 9                                             ? SPELL_FIREFLASH
                : p->phase == 10                                            ? SPELL_ORCISH_CHAMPION
                                                                            : SPELL_STRENGTH_OF_EARTH;
            if (p->phase == 8) target_descriptor_set_obj(&td, target((unsigned)p->local));
            if (p->phase == 9) target_descriptor_set_loc(&td, obj_field_int64_get(target((unsigned)p->local), OBJ_F_LOCATION));
            if (!p->sent) p->sent = cast_ui(p, spell, td);
            ready = p->sent && result(p, COOP_MAGIC_OK);
            if (ready && (p->phase == 2 || p->phase == 5 || p->phase == 10)) {
                const CoopMagicEntry* e = maintained(p, spell);
                ready = e != NULL && (p->phase == 10 || stat_level_get(pc, STAT_STRENGTH) > p->base_strength[p->local]);
                if (ready && p->phase == 2) p->old_token = e->token;
                if (ready && p->phase == 5) {
                    p->new_token = e->token;
                    ready = p->new_token != p->old_token;
                }
                if (ready && p->phase == 2 && !capture(p, "buffed")) return -1;
            }
        } else if (p->phase == 3) {
            static const unsigned wanted[] = { COOP_MAGIC_OWNERSHIP, COOP_MAGIC_UNKNOWN, COOP_MAGIC_TARGET, COOP_MAGIC_STALE, COOP_MAGIC_SOURCE };
            if (!p->sent) {
                CoopMagicRequest r = request(p);
                r.spell = SPELL_STRENGTH_OF_EARTH;
                if (!p->state) memcpy(r.actor, obj_get_id(coop_campaign_object((p->local + 1) % 4)).d.g.data, 16);
                if (p->state == 1) r.spell = SPELL_FAMILIAR;
                if (p->state == 2) {
                    r.spell = SPELL_HARM;
                    coop_object_id_from_engine(obj_get_id(coop_campaign_object((p->local + 1) % 4)), &r.target);
                }
                if (p->state == 3) r.generation++;
                if (p->state == 4) coop_object_id_from_engine(obj_get_id(coop_campaign_object((p->local + 1) % 4)), &r.source);
                p->sent = submit(p, r);
            }
            if (p->sent && result(p, wanted[p->state])) {
                p->denials++;
                p->state++;
                p->sent = false;
                ready = p->state == SDL_arraysize(wanted);
            }
        } else if (p->phase == 4 || p->phase == 7 || p->phase == 11) {
            if (!p->sent) {
                if (p->phase == 11 && !capture(p, "summoned")) return -1;
                unsigned status;
                p->expected = coop_magic_last_result(coop_gameplay_magic(p->game), &status) + 1;
                spell_ui_maintain_click(0);
                p->sent = coop_magic_busy(coop_gameplay_magic(p->game));
            }
            ready = p->sent && result(p, COOP_MAGIC_OK);
            if (ready && p->phase != 11) ready = stat_level_get(pc, STAT_STRENGTH) == p->base_strength[p->local] && !maintained(p, SPELL_STRENGTH_OF_EARTH);
        } else if (p->phase == 6) {
            if (!p->sent) {
                CoopMagicRequest r = { .epoch = coop_runtime_epoch(), .operation = COOP_MAGIC_CANCEL, .map = (uint32_t)map_current_map(), .token = p->old_token, .generation = combat_turn_generation_get() };
                memcpy(r.actor, obj_get_id(pc).d.g.data, 16);
                p->sent = submit(p, r);
            }
            ready = p->sent && result(p, COOP_MAGIC_TARGET) && maintained(p, SPELL_STRENGTH_OF_EARTH)
                && stat_level_get(pc, STAT_STRENGTH) > p->base_strength[p->local];
            if (ready) {
                uint8_t bytes[COOP_MAGIC_REQUEST_BYTES];
                if (!coop_magic_request_encode(&p->last_request, bytes)) return -1;
                uint8_t slot = coop_session_local_slot(p->session);
                if (!(p->host ? coop_magic_message(coop_gameplay_magic(p->game), slot, COOP_MAGIC_REQUEST, bytes, sizeof(bytes)) : coop_session_send(p->session, 0, COOP_MAGIC_REQUEST, bytes, sizeof(bytes)))) return -1;
                p->replayed++;
            }
        } else if (p->phase == 12 || p->phase == 14) {
            bool tb = p->phase == 12;
            if (p->host && !p->sent) {
                unsigned status;
                p->end_expected = coop_battle_last_result(coop_gameplay_battle(p->game), &status) + 1;
                p->sent = coop_runtime_combat(COOP_COMBAT_POLICY, tb, 0);
            }
            unsigned status;
            ready = (!p->host || (p->sent && coop_battle_last_result(coop_gameplay_battle(p->game), &status) == p->end_expected && !status))
                && combat_is_turn_based() == tb && (!tb || combat_turn_based_is_active());
        } else if (p->phase == 13) {
            bool own = combat_turn_based_whos_turn_get() == pc;
            if (own && !p->sent) {
                target_descriptor_set_obj(&td, target((unsigned)p->local));
                p->tb_spent = (unsigned)combat_action_points_get();
                p->sent = cast_ui(p, SPELL_HARM, td);
            }
            if (own && p->sent && !p->state && result(p, COOP_MAGIC_OK) && idle(pc)) {
                unsigned after = (unsigned)combat_action_points_get();
                /* Receipt may precede the first native animation tick. */
                if (after == p->tb_spent) return 0;
                if (p->tb_spent < after + 4) {
                    fprintf(stderr, "ARCMP magic unexpected AP member=%d before=%u after=%u\n", p->local, p->tb_spent, after);
                    return -1;
                }
                p->tb_spent -= after;
                if (!capture(p, "turn")) return -1;
                unsigned status;
                p->end_expected = coop_battle_last_result(coop_gameplay_battle(p->game), &status) + 1;
                p->state = coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0) ? 1 : 0;
            }
            if (own && p->state && !coop_battle_busy(coop_gameplay_battle(p->game))) {
                unsigned status;
                if (coop_battle_last_result(coop_gameplay_battle(p->game), &status) == p->end_expected && status == COOP_COMBAT_BUSY) {
                    p->end_expected++;
                    coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                }
            }
            ready = p->state && !own;
        } else if (p->phase == 15) {
            ready = p->local != 1;
            if (p->local == 1) {
                target_descriptor_set_obj(&td, coop_campaign_object(0));
                if (!p->sent) p->sent = cast_ui(p, SPELL_RESURRECT, td);
                ready = p->sent && result(p, COOP_MAGIC_OK) && !critter_is_dead(coop_campaign_object(0));
            }
        } else if (p->phase == 16) {
            ready = p->local != 0;
            if (p->local == 0) {
                if (!p->sent) {
                    bool accepted;
                    p->move_expected = coop_gameplay_last_result(p->game, &accepted) + 1;
                    p->sent = coop_runtime_move(location_make(location_get_x(p->old_position) + 2, location_get_y(p->old_position)), false);
                }
                bool accepted;
                ready = p->sent && coop_gameplay_last_result(p->game, &accepted) == p->move_expected && accepted
                    && obj_field_int64_get(pc, OBJ_F_LOCATION) != p->old_position && idle(pc);
            }
            if (ready && !capture(p, "resurrected")) return -1;
        }
        if (ready) {
            char text[48];
            snprintf(text, sizeof(text), "magic-done:%u", p->phase);
            if (!coop_session_chat(p->session, text)) return -1;
            p->done = true;
        }
    }
    if (p->host) {
        bool all = true;
        for (unsigned i = 0; i < 4; i++)
            all &= p->ack[i] == p->phase;
        if (!all || !settled() || !host_condition(p)) {
            p->quiet_at = 0;
            return 0;
        }
        if (!p->quiet_at) p->quiet_at = now;
        if (now - p->quiet_at < 600) return 0;
        if (p->phase == 2 || p->phase == 5) {
            for (unsigned i = 0; i < 512; i++) {
                MagicTechRunInfo* run = &magictech_run_info[i];
                if ((run->flags & MAGICTECH_RUN_ACTIVE) && run->action == MAGICTECH_ACTION_MAINTAIN && run->spell == SPELL_STRENGTH_OF_EARTH) {
                    if (p->phase == 2)
                        p->old_runs[i / 64] |= UINT64_C(1) << (i % 64);
                    else if (p->old_runs[i / 64] & (UINT64_C(1) << (i % 64)))
                        p->reused_slots++;
                }
            }
            if (p->phase == 5 && !p->reused_slots) return -1;
        }
        if (p->phase == 7) {
            for (unsigned i = 0; i < 4; i++) {
                int64_t caster = coop_campaign_object(i);
                if (!spell_add(caster, SPELL_FIREFLASH, true) || !spell_add(caster, SPELL_ORCISH_CHAMPION, true)
                    || !spell_add(caster, SPELL_RESURRECT, true)) return -1;
            }
        }
        if (p->phase == 14) critter_kill(coop_campaign_object(0));
        fprintf(stderr, "ARCMP magic settled phase=%u executed=%u rejected=%u maintained=%u\n", p->phase,
            coop_magic_executed(coop_gameplay_magic(p->game)), coop_magic_rejected(coop_gameplay_magic(p->game)), coop_engine_magic_view()->count);
        if (p->phase == LAST_PHASE)
            p->complete = true;
        else if (!announce(p, p->phase + 1))
            return -1;
        p->ack[0] = 0;
    }
    return p->failed ? -1 : p->complete ? 1
                                        : 0;
}
bool coop_magic_probe_report(CoopMagicProbe* p)
{
    if (!p || p->failed || !p->done || p->phase != LAST_PHASE || p->denials != 5 || p->replayed != 1 || p->tb_spent < 4
        || !p->overlay_frames || !p->overlay_clears || !p->overlay_captured) return false;
    FILE* file = fopen("arcmp-magic-result.json", "wb");
    if (!file) return false;
    fprintf(file, "{\"verified\":true,\"phases\":%u,\"local_member\":%d,\"rejected_requests\":%u,\"exact_replays\":%u,\"turn_ap_spent\":%u,\"reused_native_slots\":%u,\"executed\":%u,\"rejected\":%u,\"overlay_frames\":%u,\"overlay_clear_transitions\":%u,\"overlay_visibility_verified\":true,\"members\":[",
        p->phase, p->local, p->denials, p->replayed, p->tb_spent, p->reused_slots, coop_magic_executed(coop_gameplay_magic(p->game)), coop_magic_rejected(coop_gameplay_magic(p->game)), p->overlay_frames, p->overlay_clears);
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        fprintf(file, "%s{\"member\":%u,\"alive\":%s,\"strength\":%d,\"target_damage\":%d}", i ? "," : "", i, critter_is_dead(pc) ? "false" : "true", stat_level_get(pc, STAT_STRENGTH), object_hp_damage_get(target(i)));
    }
    fprintf(file, "]}\n");
    return fclose(file) == 0;
}
void coop_magic_probe_destroy(CoopMagicProbe* p) { free(p); }
