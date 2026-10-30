#include "coop/encounter_probe.h"
#include "coop/campaign.h"
#include "coop/engine_character.h"
#include "coop/engine_object.h"
#include "coop/runtime.h"
#include "game/ai.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/level.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/obj_private.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/portrait.h"
#include "game/proto.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/tile.h"
#include "ui/anim_ui.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/skill_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static CoopSession* held_session;
static bool auto_attack_changed, saved_auto_attack;
static bool content(uint8_t id[32])
{
    FILE* file = fopen("arcmp-content.id", "rb");
    char hex[66] = { 0 };
    if (!file) return false;
    bool good = fread(hex, 1, 65, file) >= 64;
    fclose(file);
    for (unsigned i = 0; good && i < 32; i++) {
        char pair[] = { hex[i * 2], hex[i * 2 + 1], 0 }, *end;
        id[i] = (uint8_t)strtoul(pair, &end, 16);
        good = !*end;
    }
    return good;
}
bool coop_encounter_character(unsigned player, CoopCharacter* build)
{
    int64_t pc = 0;
    obj_create_inst(sub_4685A0(BP_GENERIC_PC), 0, &pc);
    if (!pc) return false;
    level_set(pc, 1);
    object_set_gender_and_race(pc, TIG_ART_CRITTER_BODY_TYPE_HUMAN, GENDER_MALE, RACE_HUMAN);
    int portrait = 0;
    bool good = portrait_find_first(pc, &portrait);
    obj_field_int32_set(pc, OBJ_F_CRITTER_PORTRAIT, portrait);
    char name[32];
    snprintf(name, sizeof(name), "Encounter hero %u", player);
    obj_field_string_set(pc, OBJ_F_PC_PLAYER_NAME, name);
    const int stats[] = { STAT_DEXTERITY, STAT_DEXTERITY, STAT_STRENGTH };
    for (unsigned i = 0; good && i < SDL_arraysize(stats); i++) {
        int value = stat_base_get(pc, stats[i]), cost = stat_cost(value + 1);
        int points = stat_level_get(pc, STAT_UNSPENT_POINTS);
        good = cost <= points && stat_base_set(pc, stats[i], value + 1) == value + 1;
        if (good) stat_base_set(pc, STAT_UNSPENT_POINTS, points - cost);
    }
    for (unsigned i = 0; good && i < 2; i++) {
        int before = basic_skill_points_get(pc, BASIC_SKILL_MELEE);
        skill_ui_inc_skill(pc, SKILL_MELEE);
        good = basic_skill_points_get(pc, BASIC_SKILL_MELEE) > before;
    }
    good = good && coop_engine_character_capture(pc, build) && coop_engine_character_validate(build);
    obj_deallocate(pc);
    return good;
}
static bool idle(int64_t obj)
{
    AnimID current, fidget;
    return !sub_423300(obj, &current) || (anim_is_current_goal_type(obj, AG_ANIM_FIDGET, &fidget) && anim_id_is_equal(&current, &fidget));
}
static int64_t find_enemy(int64_t anchor, int minimum)
{
    int64_t obj;
    int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
            if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || critter_is_dead(obj)
                || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
            int64_t proto = obj_field_handle_get(obj, OBJ_F_PROTOTYPE_HANDLE);
            int distance = object_dist(anchor, obj);
            if (proto && obj_get_id(proto).d.a == 28447 && distance > minimum && distance < 200) return obj;
        } while (obj_inst_next(&obj, &iterator));
    return OBJ_HANDLE_NULL;
}
bool coop_encounter_place_far(int64_t pc, int64_t desired)
{
    if (!pc) return false;
    int64_t max_x, max_y;
    location_limits_get(&max_x, &max_y);
    for (int radius = 0; radius <= 32; radius++)
        for (int y = -radius; y <= radius; y++)
            for (int x = -radius; x <= radius; x++) {
                if (abs(x) != radius && abs(y) != radius) continue;
                int64_t px = location_get_x(desired) + x, py = location_get_y(desired) + y;
                if (px < 0 || py < 0 || px >= max_x || py >= max_y) continue;
                int64_t loc = location_make(px, py);
                if (tile_art_id_at(loc) == TIG_ART_ID_INVALID || tile_is_blocking(loc, false)) continue;
                ObjectList objects;
                object_list_location(loc, OBJ_TM_SCENERY | OBJ_TM_WALL | OBJ_TM_PORTAL | OBJ_TM_CRITTER | OBJ_TM_CONTAINER, &objects);
                bool clear = true;
                for (ObjectNode* node = objects.head; node; node = node->next) {
                    unsigned flags = obj_field_int32_get(node->obj, OBJ_F_FLAGS);
                    if (node->obj != pc && !(flags & (OF_OFF | OF_DESTROYED | OF_EXTINCT | OF_NO_BLOCK))) clear = false;
                }
                object_list_destroy(&objects);
                if (clear) {
                    sub_43E770(pc, loc, 0, 0);
                    return true;
                }
            }
    return false;
}
static bool place_near_range(int64_t pc, int64_t enemy, int minimum, int maximum)
{
    int64_t origin = obj_field_int64_get(enemy, OBJ_F_LOCATION);
    for (int radius = minimum; radius <= maximum; radius++)
        for (int y = -radius; y <= radius; y++)
            for (int x = -radius; x <= radius; x++) {
                if (abs(x) != radius && abs(y) != radius) continue;
                int64_t loc = location_make(location_get_x(origin) + x, location_get_y(origin) + y);
                if (tile_art_id_at(loc) == TIG_ART_ID_INVALID || tile_is_blocking(loc, false)) continue;
                bool occupied = false;
                for (unsigned i = 0; i < 4; i++) {
                    int64_t other = coop_campaign_object(i);
                    occupied = occupied || (other != pc && obj_field_int64_get(other, OBJ_F_LOCATION) == loc);
                }
                if (occupied) continue;
                int8_t rotations[64];
                PathCreateInfo path = { .obj = pc, .from = origin, .to = loc, .max_rotations = SDL_arraysize(rotations), .rotations = rotations };
                if (path_make(&path) > 0) {
                    sub_43E770(pc, loc, 0, 0);
                    return true;
                }
            }
    return false;
}
bool coop_encounter_place_near(int64_t pc, int64_t enemy)
{
    return place_near_range(pc, enemy, 2, 8);
}
static int party_damage(void)
{
    int value = 0;
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        value += obj_field_int32_get(pc, OBJ_F_HP_DAMAGE) + critter_fatigue_damage_get(pc);
    }
    return value;
}
static bool capture(unsigned phase, bool framebuffer)
{
    if (!framebuffer) return true;
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    iso_interface_refresh();
    intgame_show();
    gamelib_redraw();
    iso_redraw();
    tig_window_display();
    char path[64];
    snprintf(path, sizeof(path), "arcmp-encounter-%u.bmp", phase);
    return tig_video_screenshot_save(path) == TIG_OK;
}
static bool announce(CoopSession* session, unsigned phase, int remote_member, int64_t enemy)
{
    CoopObjectId id;
    if (!coop_object_id_from_engine(obj_get_id(enemy), &id)) return false;
    char hex[35];
    for (unsigned i = 0; i < 17; i++)
        snprintf(hex + i * 2, 3, "%02x", id.bytes[i]);
    char text[80];
    snprintf(text, sizeof(text), "encounter:%u:%d:%s", phase, remote_member, hex);
    return coop_session_chat(session, text);
}
void coop_encounter_probe_cleanup(void)
{
    if (!held_session) return;
    if (auto_attack_changed) {
        combat_auto_attack_set(saved_auto_attack);
        auto_attack_changed = false;
    }
    coop_runtime_detach();
    coop_session_destroy(held_session);
    held_session = NULL;
}
bool coop_encounter_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false;
    unsigned port = 35742, player = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= 4 || host != (player == 0)) return false;
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Encounter player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    CoopSession* session = coop_session_create(&config);
    if (!session) return false;
    held_session = session;
    uint64_t start = SDL_GetTicks(), playing_at = 0, phase_at = 0, finished_at = 0, chats = 0, retry_at = 0, death_at = 0;
    bool good = (host ? coop_session_host(session, (uint16_t)port, start) : coop_session_join(session, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(session, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port);
    fflush(stderr);
    bool selected = false, initialized = false, done = false, sent = false, finishing = false, legal_fighters = false;
    bool rt_focus = false, rt_damage = false, tb_turn = false, tb_damage = false, host_death = false, survivor_control = false, dead_denied = false;
    unsigned phase = 0, completed = 0, ack[4] = { 0 }, rt_attacks = 0, tb_attacks = 0;
    uint32_t seen_result = 0, command = 0, request_generation = 0, attacked_generation = UINT32_MAX;
    CoopCombatOperation pending = COOP_COMBAT_STANCE;
    int local = -1, remote_member = -1, baseline_damage = 0, rt_baseline_hp = 0, rt_baseline_fatigue = 0;
    int initial_xp[4] = { 0 }, after_rt[4] = { 0 };
    int64_t enemy = 0, first_enemy = 0;
    while (good && SDL_GetTicks() - start < 120000) {
        uint64_t now = SDL_GetTicks();
        tig_ping();
        coop_session_poll(session, now);
        coop_runtime_tick(now);
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) {
            fprintf(stderr, "ARCMP encounter failed: %s\n", coop_runtime_status());
            good = false;
            break;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (!selected) {
                CoopCharacter build;
                good = coop_encounter_character(player, &build);
                selected = good && coop_session_custom_character(session, &build);
            } else if (coop_session_players(session)[coop_session_local_slot(session)].prototype && !coop_session_players(session)[coop_session_local_slot(session)].ready)
                coop_session_ready(session, true);
            unsigned count = 0;
            for (unsigned i = 0; i < 4; i++)
                count += coop_session_players(session)[i].connected;
            if (host && count == 4 && coop_session_all_ready(session)) good = coop_runtime_start();
        }
        CoopGameplay* game = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            CoopBattle* battle = coop_gameplay_battle(game);
            if (!playing_at) {
                playing_at = now;
                local = coop_campaign_slot_for_owner(config.identity);
                for (unsigned i = 0; i < 4; i++)
                    initial_xp[i] = stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS);
                legal_fighters = true;
                for (unsigned i = 0; i < 4; i++) {
                    int64_t member = coop_campaign_object(i);
                    legal_fighters = legal_fighters && member && stat_base_get(member, STAT_STRENGTH) == 9
                        && stat_base_get(member, STAT_DEXTERITY) == 10 && basic_skill_points_get(member, BASIC_SKILL_MELEE) == 2
                        && stat_base_get(member, STAT_UNSPENT_POINTS) == 0;
                }
                good = good && legal_fighters;
            }
            const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (view->active && view->chooser == coop_session_local_slot(session)) {
                TigMessage escape = { .type = TIG_MESSAGE_KEYBOARD };
                escape.data.keyboard.key = SDLK_ESCAPE;
                escape.data.keyboard.scancode = SDL_SCANCODE_ESCAPE;
                escape.data.keyboard.pressed = true;
                tig_message_enqueue(&escape);
            }
            if (host && !initialized && now - playing_at > 1500 && !view->active) {
                int64_t pc = coop_campaign_object(0);
                for (unsigned i = 1; i < 4; i++)
                    if (!strcmp(coop_campaign_member(i)->character, "Encounter hero 1")) remote_member = (int)i;
                good = remote_member >= 1;
                saved_auto_attack = combat_auto_attack_get(pc);
                auto_attack_changed = true;
                combat_auto_attack_set(false);
                first_enemy = enemy = find_enemy(pc, 40);
                good = good && enemy && sub_4B6C90(false);
                int64_t origin = obj_field_int64_get(pc, OBJ_F_LOCATION);
                for (unsigned i = 0; good && i < 4; i++)
                    if ((int)i != remote_member)
                        good = coop_encounter_place_far(coop_campaign_object(i), location_make(location_get_x(origin) + 3 * i, location_get_y(origin)));
                good = good && coop_encounter_place_near(coop_campaign_object((unsigned)remote_member), enemy) && object_dist(pc, enemy) > 30;
                rt_baseline_hp = obj_field_int32_get(coop_campaign_object((unsigned)remote_member), OBJ_F_HP_DAMAGE);
                rt_baseline_fatigue = critter_fatigue_damage_get(coop_campaign_object((unsigned)remote_member));
                initialized = true;
                if (good) good = announce(session, 1, remote_member, enemy);
            }
            for (; chats < coop_session_chat_count(session); chats++) {
                const CoopChat* chat = coop_session_chat_at(session, chats);
                unsigned value;
                char hex[35];
                if (!chat->slot && sscanf(chat->text, "encounter:%u:%d:%34s", &value, &remote_member, hex) == 3) {
                    CoopObjectId id = { 0 };
                    if (strlen(hex) != 34 || remote_member < 1 || remote_member > 3) {
                        good = false;
                        break;
                    }
                    for (unsigned i = 0; i < 17; i++) {
                        char part[] = { hex[i * 2], hex[i * 2 + 1], 0 };
                        id.bytes[i] = (uint8_t)strtoul(part, NULL, 16);
                    }
                    enemy = obj_pool_perm_lookup(coop_object_id_to_engine(id));
                    phase = value;
                    phase_at = now;
                    done = sent = false;
                    command = 0;
                    retry_at = now;
                    if (phase == 3) baseline_damage = party_damage();
                    if (phase == 3 || phase == 4) seen_result = coop_battle_last_result(battle, &value);
                    fprintf(stderr, "ARCMP encounter phase=%u member=%d enemy=%llx\n", phase, local, (unsigned long long)enemy);
                }
                if (sscanf(chat->text, "encounter-done:%u", &value) == 1 && chat->slot < 4) ack[chat->slot] = value;
            }
            int64_t pc = player_get_local_pc_obj();
            unsigned status = 0;
            uint32_t result = coop_battle_last_result(battle, &status);
            if (result > seen_result) {
                if (!status && pending == COOP_COMBAT_ATTACK) {
                    if (phase == 2) rt_attacks++;
                    if (phase == 4) tb_attacks++;
                }
                if (phase == 4 && pending == COOP_COMBAT_ATTACK && (status == COOP_COMBAT_OK || status == COOP_COMBAT_REFUSED)) attacked_generation = request_generation;
                if (status && status != COOP_COMBAT_BUSY && status != COOP_COMBAT_REFUSED && status != COOP_COMBAT_STALE && status != COOP_COMBAT_TURN && status != COOP_COMBAT_TARGET) good = false;
                seen_result = result;
                retry_at = now + (phase == 2 ? 20 : 350);
            }
            if (enemy && phase) {
                if (host && phase == 2 && coop_battle_continued(battle)) combat_auto_attack_set(true);
                if (host && phase == 1) {
                    rt_focus = rt_focus || obj_field_handle_get(enemy, OBJ_F_NPC_COMBAT_FOCUS) == coop_campaign_object((unsigned)remote_member);
                    rt_damage = rt_damage || obj_field_int32_get(coop_campaign_object((unsigned)remote_member), OBJ_F_HP_DAMAGE) > rt_baseline_hp || critter_fatigue_damage_get(coop_campaign_object((unsigned)remote_member)) > rt_baseline_fatigue;
                }
                if (host && phase == 3 && combat_turn_based_is_active()) {
                    tb_turn = tb_turn || combat_turn_based_whos_turn_get() == enemy;
                    tb_damage = tb_damage || party_damage() > baseline_damage;
                }
                if (phase == 3 && host && !coop_battle_busy(battle) && now >= retry_at && !combat_is_turn_based()) {
                    pending = COOP_COMBAT_POLICY;
                    good = coop_runtime_combat(pending, 1, OBJ_HANDLE_NULL);
                    retry_at = now + 500;
                } else if (phase == 3 && host && !coop_battle_busy(battle) && now >= retry_at && !combat_turn_based_is_active()) {
                    pending = COOP_COMBAT_STANCE;
                    good = coop_runtime_combat(pending, 1, OBJ_HANDLE_NULL);
                    retry_at = now + 500;
                } else if (!coop_battle_busy(battle) && now >= retry_at && !view->active && !critter_is_dead(pc)) {
                    bool attack = (phase == 2 && local == remote_member && rt_attacks < 2 && !critter_is_dead(enemy))
                        || (phase == 4 && combat_turn_based_whos_turn_get() == pc && !critter_is_dead(enemy) && attacked_generation != combat_turn_generation_get());
                    bool end = (phase == 3 || phase == 4) && combat_turn_based_is_active() && combat_turn_based_whos_turn_get() == pc;
                    if (attack) {
                        pending = combat_critter_is_combat_mode_active(pc) ? COOP_COMBAT_ATTACK : COOP_COMBAT_STANCE;
                        request_generation = combat_turn_generation_get();
                        good = coop_runtime_combat(pending, pending == COOP_COMBAT_ATTACK ? 0 : 1, pending == COOP_COMBAT_ATTACK ? enemy : OBJ_HANDLE_NULL);
                        retry_at = now + (phase == 2 ? 20 : 700);
                    } else if (end) {
                        pending = COOP_COMBAT_END_TURN;
                        good = coop_runtime_combat(pending, 0, OBJ_HANDLE_NULL);
                        retry_at = now + 500;
                    }
                }
                if (phase == 5 && !sent && (host || local == (remote_member == 3 ? 2 : 3)) && critter_is_dead(coop_campaign_object(0)) && !combat_is_turn_based()) {
                    bool accepted;
                    command = coop_gameplay_last_result(game, &accepted) + 1;
                    sent = coop_runtime_halt(obj_field_int64_get(pc, OBJ_F_LOCATION));
                    good = good && sent;
                }
                if (phase == 5 && sent) {
                    bool accepted;
                    uint32_t last = coop_gameplay_last_result(game, &accepted);
                    if (last == command) {
                        if (host)
                            dead_denied = !accepted;
                        else
                            survivor_control = accepted;
                    }
                }
                bool ready = phase == 1 ? object_dist(coop_campaign_object(0), enemy) > 30 && object_dist(coop_campaign_object((unsigned)remote_member), enemy) < 10
                    : phase == 2        ? critter_is_dead(enemy) && (local != remote_member || rt_attacks >= 2)
                    : phase == 3        ? combat_is_turn_based() && combat_turn_based_is_active()
                    : phase == 4        ? critter_is_dead(enemy)
                                        : phase == 5 && critter_is_dead(coop_campaign_object(0)) && !combat_is_turn_based() && (!host || dead_denied) && (local != (remote_member == 3 ? 2 : 3) || survivor_control);
                if (phase == 2 || phase == 4)
                    for (unsigned i = 0; i < 4; i++) {
                        int xp = stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS);
                        ready = ready && xp > (phase == 2 ? initial_xp[i] : after_rt[i]);
                    }
                if (ready && !done) {
                    if (phase == 2)
                        for (unsigned i = 0; i < 4; i++)
                            after_rt[i] = stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS);
                    if (phase == 1 || phase == 3 || phase == 5) good = good && capture(phase, framebuffer);
                    char text[40];
                    snprintf(text, sizeof(text), "encounter-done:%u", phase);
                    good = good && coop_session_chat(session, text);
                    done = true;
                    completed = phase;
                }
                if (host) {
                    bool all = true;
                    for (unsigned i = 0; i < 4; i++)
                        all = all && ack[i] == phase;
                    if (all && now - phase_at > 250) {
                        if (phase == 1 && rt_focus && rt_damage) {
                            /* Leave time for two real request/receipt cycles before
                             * the approach reaches native melee impact. */
                            good = place_near_range(coop_campaign_object((unsigned)remote_member), enemy, 12, 16)
                                && announce(session, 2, remote_member, enemy);
                        }
                        if (phase == 2) {
                            enemy = find_enemy(coop_campaign_object((unsigned)remote_member), 0);
                            good = enemy && enemy != first_enemy;
                            for (unsigned i = 0; good && i < 4; i++)
                                good = coop_encounter_place_near(coop_campaign_object(i), enemy);
                            if (good) good = announce(session, 3, remote_member, enemy);
                        }
                        if (phase == 3 && tb_turn && tb_damage) good = announce(session, 4, remote_member, enemy);
                        if (phase == 4) {
                            bool quiet = true;
                            for (unsigned i = 0; i < 4; i++)
                                quiet = quiet && idle(coop_campaign_object(i));
                            if (quiet && !coop_battle_busy(battle) && combat_is_turn_based() && now >= retry_at) {
                                pending = COOP_COMBAT_POLICY;
                                good = coop_runtime_combat(pending, 0, OBJ_HANDLE_NULL);
                                retry_at = now + 500;
                            }
                            if (quiet && !combat_is_turn_based() && !coop_battle_busy(battle)) {
                                critter_kill(coop_campaign_object(0));
                                host_death = true;
                                death_at = now;
                                anim_ui_event_add_delay(ANIM_UI_EVENT_TYPE_END_DEATH, -1, 0);
                                good = announce(session, 5, remote_member, enemy);
                            }
                        }
                        if (phase == 5 && now - death_at > 6000 && !finishing) finishing = coop_gameplay_finish(game);
                    }
                }
                if (now - phase_at > (phase == 4 ? 60000u : 30000u)) {
                    fprintf(stderr, "ARCMP encounter timeout phase=%u member=%d rt=%u tb=%u npc-turn=%d npc-damage=%d enemy-dead=%d hp=%d fatigue=%d result=%u status=%u\n", phase, local, rt_attacks, tb_attacks, tb_turn, tb_damage, critter_is_dead(enemy), object_hp_current(enemy), critter_fatigue_current(enemy), result, status);
                    good = false;
                }
            }
            if (coop_gameplay_finished(game)) {
                if (!finished_at) finished_at = now;
                if (now - finished_at > 600) break;
            }
        }
        gamelib_ping();
        iso_redraw();
        tig_window_display();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT)
                good = false;
            else if (message.type == TIG_MESSAGE_REDRAW)
                gamelib_redraw();
            else
                intgame_process_event(&message);
        }
        SDL_Delay(2);
    }
    CoopGameplay* game = coop_runtime_gameplay();
    good = good && game && coop_gameplay_finished(game) && completed == 5 && coop_campaign_count() == 4 && critter_is_dead(coop_campaign_object(0));
    FILE* file = fopen("arcmp-encounter-result.json", "wb");
    if (file) {
        fprintf(file, "{\"verified\":%s,\"phases\":%u,\"local_member\":%d,\"remote_member\":%d,\"native_prototype\":28447,\"legal_starting_fighters\":%s,\"existing_attack_confirmations\":%u,\"real_time_attacks\":%u,\"turn_based_attacks\":%u,\"off_host_native_ai_focus\":%s,\"off_host_native_ai_damage\":%s,\"native_enemy_turn\":%s,\"native_turn_damage\":%s,\"host_character_died\":%s,\"dead_controller_denied\":%s,\"survivor_control_after_host_death\":%s,\"shared_world_preserved\":%s}\n", good ? "true" : "false", completed, local, remote_member, legal_fighters ? "true" : "false", game ? coop_battle_continued(coop_gameplay_battle(game)) : 0, rt_attacks, tb_attacks, rt_focus ? "true" : "false", rt_damage ? "true" : "false", tb_turn ? "true" : "false", tb_damage ? "true" : "false", host_death ? "true" : "false", dead_denied ? "true" : "false", survivor_control ? "true" : "false", good ? "true" : "false");
        if (fclose(file)) good = false;
    } else
        good = false;
    fprintf(stderr, "ARCMP encounter probe %s phases=%u member=%d\n", good ? "PASS" : "FAIL", completed, local);
    return good;
}
