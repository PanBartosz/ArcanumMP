#include "coop/battle_probe.h"
#include "coop/modern.h"
#include "coop/display.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_combat.h"
#include "coop/engine_environment.h"
#include "coop/engine_options.h"
#include "coop/lobby_ui.h"
#include "coop/party_ui.h"
#include "coop/engine_object.h"
#include "coop/runtime.h"
#include "coop/encounter_probe.h"
#include "coop/weapon_probe.h"
#include "coop/magic_probe.h"
#include "coop/audio_probe.h"
#include "coop/use_probe.h"
#include "coop/skills_probe.h"
#include "coop/progression_probe.h"
#include "game/anim.h"
#include "game/anim_private.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/hrp.h"
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
#include "game/spell.h"
#include "game/stat.h"
#include "game/target.h"
#include "game/timeevent.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/skill_ui.h"
#include "ui/spell_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { TARGET_TAG = 795001,
    LAST_STEP = 24 };
static CoopSession* probe_session;
static CoopWeaponProbe* weapon_probe;
static CoopMagicProbe* magic_probe;
static CoopAudioProbe* audio_probe;
static CoopUseProbe* use_probe;
static CoopSkillsProbe* skills_probe;
static CoopProgressionProbe* progression_probe;
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
static int64_t target(void)
{
    int64_t obj;
    int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
            if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && obj_field_int32_get(obj, OBJ_F_NPC_PAD_I_1) == TARGET_TAG) return obj;
        } while (obj_inst_next(&obj, &iterator));
    return OBJ_HANDLE_NULL;
}
static bool idle(int64_t pc)
{
    AnimID current, fidget;
    return !sub_423300(pc, &current) || (anim_is_current_goal_type(pc, AG_ANIM_FIDGET, &fidget) && anim_id_is_equal(&current, &fidget));
}
static void key(SDL_Scancode code, SDL_Keycode symbol, bool pressed)
{
    TigMessage m = { .type = TIG_MESSAGE_KEYBOARD };
    m.data.keyboard.scancode = code;
    m.data.keyboard.key = symbol;
    m.data.keyboard.pressed = pressed;
    tig_message_enqueue(&m);
}
static void click(int x, int y)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE };
    m.data.mouse.x = x; m.data.mouse.y = y;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
}
static void immortality_click(CoopGameplay* game, unsigned member)
{
    coop_lobby_open();
    for (unsigned slot = 0; slot < 4; slot++)
        if (coop_gameplay_member_for_connection(game, slot) == (int)member)
            click((hrp_iso_window_width_get() - 800) / 2 + 530,
                (hrp_iso_window_height_get() - 600) / 2 + 260 + (int)slot * 25);
}
static bool attack_click(int64_t obj)
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
            tig_message_enqueue(&m);
            m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP;
            tig_message_enqueue(&m);
            return true;
        }
    return false;
}
static bool capture(const char* name, bool framebuffer)
{
    if (!framebuffer) return true;
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    iso_interface_refresh();
    intgame_show();
    coop_party_ui_tick(SDL_GetTicks());
    gamelib_redraw();
    iso_redraw();
    tig_window_display();
    return tig_video_screenshot_save(name) == TIG_OK;
}
/* One local presentation check per peer after the combined party-UI flow.
 * Input is sent through the same wheel dispatcher used by the game. */
static bool display_projection_probe(bool framebuffer)
{
    int width=hrp_iso_window_width_get(), height=hrp_iso_window_height_get();
    TigMessage wheel = { .type=TIG_MESSAGE_MOUSE };
    wheel.data.mouse.event=TIG_MESSAGE_MOUSE_WHEEL;
    wheel.data.mouse.x=width/2; wheel.data.mouse.y=height/3;
    wheel.data.mouse.dy=1;
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(),OBJ_F_LOCATION));
    int64_t anchor;
    if (!coop_view_location_at(wheel.data.mouse.x,wheel.data.mouse.y,&anchor)) return false;
    for (unsigned i=0;i<3;i++) {
        if (!coop_view_wheel(&wheel) || coop_view_zoom()<=100) return false;
        int64_t after;
        if (!coop_view_location_at(wheel.data.mouse.x,wheel.data.mouse.y,&after) || location_dist(anchor,after)>1) return false;
        if (framebuffer) {
            char name[80]; snprintf(name,sizeof(name),"arcmp-display-zoom-%d.bmp",coop_view_zoom());
            coop_party_ui_tick(SDL_GetTicks()+1000); gamelib_redraw(); iso_redraw(); tig_window_display();
            if (tig_video_screenshot_save(name)!=TIG_OK) return false;
        }
    }
    if (!coop_view_zoom_set(100,wheel.data.mouse.x,wheel.data.mouse.y)) return false;
    wheel.data.mouse.dy=-1;
    if (!coop_view_wheel(&wheel) || coop_view_zoom()>=100) return false;
    gamelib_redraw(); iso_redraw(); tig_window_display();
    if (framebuffer && tig_video_screenshot_save("arcmp-display-zoom-out.bmp")!=TIG_OK) return false;
    if (!coop_view_zoom_set(100,wheel.data.mouse.x,wheel.data.mouse.y)) return false;
    FILE* result=fopen("arcmp-display-result.json","wt"); if (!result) return false;
    fprintf(result,"{\"verified\":true,\"zoom_steps\":4,\"anchor_preserved\":true,\"width\":%d,\"height\":%d,\"status0\":%u,\"status1\":%u,\"status3\":%u}\n",
        width,height,coop_party_ui_statuses(coop_campaign_object(0)),coop_party_ui_statuses(coop_campaign_object(1)),coop_party_ui_statuses(coop_campaign_object(3)));
    return fclose(result)==0;
}
static bool setup(int64_t* original_target)
{
    int64_t obj;
    int iterator;
    /* This controlled fixture excludes quest-NPC heartbeats and retaliation;
     * actual campaign encounters/AI remain separate acceptance work. */
    if (obj_inst_first(&obj, &iterator)) do {
            if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && !critter_is_dead(obj)) object_flags_set(obj, OF_OFF);
        } while (obj_inst_next(&obj, &iterator));
    int64_t origin = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        sub_43E770(pc, location_make(location_get_x(origin) + 2 * i, location_get_y(origin) + 3), 0, 0);
        stat_base_set(pc, STAT_DEXTERITY, 20);
        basic_skill_points_set(pc, BASIC_SKILL_MELEE, 20);
        stat_base_set(pc, STAT_STRENGTH, 18);
        int64_t weapon = item_wield_get(pc, ITEM_INV_LOC_WEAPON);
        if (weapon && !sub_464C80(weapon)) return false;
        obj_field_int32_set(pc, OBJ_F_HP_PTS, 200);
        obj_field_int32_set(pc, OBJ_F_HP_DAMAGE, 0);
    }
    int64_t wolf = 0;
    *original_target = location_make(location_get_x(origin) + 3, location_get_y(origin) + 6);
    if (!object_create(sub_4685A0(BP_WOLF), *original_target, &wolf)) return false;
    Script none = { 0 };
    for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++)
        obj_arrayfield_script_set(wolf, OBJ_F_SCRIPTS_IDX, sap, &none);
    obj_field_int32_set(wolf, OBJ_F_NPC_PAD_I_1, TARGET_TAG);
    obj_field_int32_set(wolf, OBJ_F_HP_PTS, 1000);
    obj_field_int32_set(wolf, OBJ_F_HP_DAMAGE, 0);
    obj_field_int32_set(wolf, OBJ_F_CRITTER_FLAGS, obj_field_int32_get(wolf, OBJ_F_CRITTER_FLAGS) | OCF_PARALYZED);
    return sub_4B6C90(false);
}
static bool announce(CoopSession* session, unsigned step, int actor, uint32_t generation)
{
    char message[80];
    snprintf(message, sizeof(message), "battle-step:%u:%d:%u", step, actor, generation);
    return coop_session_chat(session, message);
}
void coop_battle_probe_cleanup(void)
{
    coop_audio_probe_destroy(audio_probe);
    audio_probe = NULL;
    coop_use_probe_destroy(use_probe);
    use_probe = NULL;
    coop_skills_probe_destroy(skills_probe);
    skills_probe = NULL;
    coop_progression_probe_destroy(progression_probe);
    progression_probe = NULL;
    coop_magic_probe_destroy(magic_probe);
    magic_probe = NULL;
    coop_weapon_probe_destroy(weapon_probe);
    weapon_probe = NULL;
    if (!probe_session) return;
    coop_runtime_detach();
    coop_session_destroy(probe_session);
    probe_session = NULL;
}
static bool modern_coordination_check(uint64_t now)
{
    static uint64_t start;
    static bool world_sent,map_sent;
    if (!start) start=now;
    if (coop_authority_can_simulate() && coop_play_pause_policy()!=PLAY_PAUSE_CRITICAL)
        coop_play_pref_set(PLAY_PREF_AUTOPAUSE,PLAY_PAUSE_CRITICAL);
    int member=coop_campaign_slot_for_object(player_get_local_pc_obj());
    if (!world_sent) world_sent=coop_play_ping(obj_field_int64_get(player_get_local_pc_obj(),OBJ_F_LOCATION));
    if (!map_sent && now-start>1000) map_sent=coop_play_map_ping(0,100+(unsigned)member,100);
    unsigned world=0,map=0;
    for (uint32_t n=coop_play_event_first();n<coop_play_event_end();n++) {
        const PlayEvent* e=coop_play_event_at(n);
        if (e->kind==PLAY_PING) world|=e->members;
        if (e->kind==PLAY_MAP_PING) map|=e->members;
    }
    return world_sent && map_sent && world==15 && map==15 && coop_play_pause_policy()==PLAY_PAUSE_CRITICAL;
}
bool coop_battle_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false, loss = false, replay_fault = false, weapons = false, weapons_started = false, magic = false, magic_started = false, use = false, use_started = false, skills = false, skills_started = false, progression = false, progression_started = false;
    bool player_reports = getenv("ARCMP_PLAYER_REPORTS") != NULL;
    bool party_ui = getenv("ARCMP_PARTY_UI") != NULL;
    bool display_features = party_ui && getenv("ARCMP_DISPLAY_FEATURES") != NULL, display_checked = false;
    unsigned final_step = party_ui ? 31 : LAST_STEP;
    unsigned port = 35742, player = COOP_MAX_PLAYERS;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-combat-loss")) loss = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-combat-replay")) replay_fault = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-weapons")) weapons = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-magic")) magic = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-use")) use = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-skills")) skills = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-progression")) progression = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= 4 || host != (player == 0)) return false;
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Battle player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    CoopSession* session = coop_session_create(&config);
    if (!session) return false;
    probe_session = session;
    uint64_t start = SDL_GetTicks(), playing_at = 0, step_at = 0, finished_at = 0, chats = 0;
    bool good = (host ? coop_session_host(session, (uint16_t)port, start) : coop_session_join(session, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(session, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port);
    fflush(stderr);
    bool selected = false, initialized = false, sent = false, done = false, finishing = false, fault_sent = false, stance_preparing = false;
    unsigned step = 0, completed = 0, ack[4] = { 0 }, attacked = 0, tb_attacks = 0, rt_attacks = 0, denials = 0;
    unsigned expected_status = 0, replay_count = 0;
    uint32_t expected = 0, stage_generation = 0, legacy_expected = 0;
    int local = -1, actor = -1, tb_damage = 0;
    int64_t target_origin = 0;
    bool target_far = false, replica_guard = false;
    unsigned stale_noncombat = 0, report_frames = 0;
    uint64_t visual_at = 0;
    unsigned preview_phase = 0;
    uint64_t preview_at = 0;
    uint32_t magic_expected = 0;
    bool fresh_item = false;
    CoopCombatRequest last_request = { 0 };
    uint64_t duration_limit = magic || progression ? 240000 : use ? 180000 : 120000;
    while (good && SDL_GetTicks() - start < duration_limit) {
        uint64_t now = SDL_GetTicks();
        tig_ping();
        coop_session_poll(session, now);
        coop_runtime_tick(now);
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) {
            if (loss || replay_fault) break;
            fprintf(stderr, "ARCMP battle runtime failure: %s\n", coop_runtime_status());
            good = false;
            break;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (!selected) {
                char name[32];
                snprintf(name, sizeof(name), "Battle hero %u", player);
                if (weapons || magic || use || party_ui) {
                    CoopCharacter build;
                    selected = ((magic || party_ui) ? coop_magic_probe_character(player, &build) : coop_encounter_character(player, &build)) && coop_session_custom_character(session, &build);
                } else selected = coop_session_character(session, 16067 + player, name);
            } else if (coop_session_players(session)[coop_session_local_slot(session)].prototype && !coop_session_players(session)[coop_session_local_slot(session)].ready)
                coop_session_ready(session, true);
            unsigned count = 0;
            for (unsigned i = 0; i < 4; i++)
                count += coop_session_players(session)[i].connected;
            if (host && count == 4 && coop_session_all_ready(session)) good = coop_runtime_start();
        }
        CoopGameplay* game = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing_at) {
                playing_at = now;
                local = coop_campaign_slot_for_owner(config.identity);
                if (weapons) {
                    weapon_probe = coop_weapon_probe_create(session, game, local, framebuffer);
                    good = good && weapon_probe;
                }
                if (magic) {
                    magic_probe = coop_magic_probe_create(session, game, local, framebuffer);
                    audio_probe = coop_audio_probe_create(session, game, local, framebuffer);
                    good = good && magic_probe && audio_probe;
                }
                if (use) {
                    use_probe = coop_use_probe_create(session, game, local, framebuffer);
                    good = good && use_probe;
                }
                if (skills) {
                    skills_probe = coop_skills_probe_create(session, game, local, framebuffer);
                    good = good && skills_probe;
                }
                if (progression) {
                    progression_probe = coop_progression_probe_create(session, game, local, framebuffer);
                    good = good && progression_probe;
                }
            }
            CoopBattle* battle = coop_gameplay_battle(game);
            const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (view->active && view->chooser == coop_session_local_slot(session)) key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, true);
            if (host && !initialized && now - playing_at > 1500 && !view->active) {
                initialized = setup(&target_origin);
                good = good && initialized;
                if (good) good = announce(session, 1, 0, combat_turn_generation_get());
            }
            for (; chats < coop_session_chat_count(session); chats++) {
                const CoopChat* chat = coop_session_chat_at(session, chats);
                if (!chat) continue;
                unsigned value, generation;
                int selected_actor;
                if (!chat->slot && sscanf(chat->text, "battle-step:%u:%d:%u", &value, &selected_actor, &generation) == 3) {
                    step = value;
                    actor = selected_actor;
                    stage_generation = generation;
                    step_at = now;
                    sent = done = false;
                    stance_preparing = false;
                    unsigned status;
                    expected = coop_battle_last_result(battle, &status) + 1;
                    if (step == 14 && target()) tb_damage = obj_field_int32_get(target(), OBJ_F_HP_DAMAGE);
                    if (step == 19) replay_count = coop_battle_executed(battle);
                    fprintf(stderr, "ARCMP battle step=%u member=%d actor=%d generation=%u\n", step, local, actor, generation);
                }
                if (player_reports && !strcmp(chat->text, "battle-visual-begin")) visual_at = now;
                if (sscanf(chat->text, "battle-done:%u", &value) == 1 && chat->slot < 4) ack[chat->slot] = value;
            }
            int64_t wolf = target(), pc = player_get_local_pc_obj();
            if (player_reports && framebuffer && player == 2 && visual_at && report_frames < 24
                && now - visual_at >= report_frames * 100u) {
                char file[80]; snprintf(file, sizeof(file), "arcmp-reported-npc-%02u.bmp", report_frames++);
                /* Preserve incremental redraws: a forced full redraw would hide ghosting. */
                tig_video_screenshot_save(file);
            }
            bool attack_step = step >= 6 && step <= 12 && !(step & 1), end_step = step >= 7 && step <= 13 && (step & 1);
            bool acts = step == 3 || step == 4 || step == 5 || step == 15 || step == 17 || step == 18 || step == 20
                || (step == 1 && host) || (step == 2 && local == 1) || (attack_step && local == actor) || (end_step && local == actor)
                || (party_ui && (step == 26 || (host && (step == 25 || step == 30 || step == 31))));
            if (step && !done && wolf && !view->active) {
                bool ready = step < 2 || combat_turn_generation_get() >= stage_generation;
                if (attack_step && local == actor && ready && !sent) {
                    if (!combat_critter_is_combat_mode_active(pc) && !stance_preparing) {
                        key(SDL_SCANCODE_R, SDLK_R, true);
                        stance_preparing = true;
                    }
                    unsigned stance_status;
                    if (stance_preparing && coop_battle_last_result(battle, &stance_status) == expected) {
                        good = good && !stance_status && combat_critter_is_combat_mode_active(pc);
                        expected++;
                        stance_preparing = false;
                    }
                    ready = ready && !stance_preparing && combat_critter_is_combat_mode_active(pc);
                }
                if (step == 15 && player_reports && !sent) ready &= combat_is_party_enemy(wolf) && !combat_critter_is_combat_mode_active(pc);
                if (acts && !sent && ready) {
                    CoopCombatOperation op = COOP_COMBAT_STANCE;
                    uint8_t value = 1;
                    CoopObjectId id = { 0 };
                    const uint8_t* actor_id = coop_campaign_member((unsigned)local)->object_id.d.g.data;
                    uint32_t generation = combat_turn_generation_get();
                    expected_status = COOP_COMBAT_OK;
                    if (step == 1) {
                        op = COOP_COMBAT_POLICY;
                        value = 1;
                    } else if (step == 3) {
                        actor_id = coop_campaign_member((unsigned)((local + 1) % 4))->object_id.d.g.data;
                        expected_status = COOP_COMBAT_OWNERSHIP;
                    } else if (step == 4 || step == 20) {
                        op = COOP_COMBAT_END_TURN;
                        value = 0;
                        generation = stage_generation ? stage_generation - 1 : 1;
                        expected_status = COOP_COMBAT_STALE;
                    } else if (step == 5) {
                        op = COOP_COMBAT_POLICY;
                        value = host ? 1 : 0;
                        expected_status = host ? COOP_COMBAT_OK : COOP_COMBAT_HOST_ONLY;
                    } else if (attack_step || step == 15 || step == 17) {
                        op = COOP_COMBAT_ATTACK;
                        value = 0;
                        coop_object_id_from_engine(obj_get_id(wolf), &id);
                        if (step == 17) expected_status = COOP_COMBAT_DISTANCE;
                    } else if (end_step) {
                        op = COOP_COMBAT_END_TURN;
                        value = 0;
                    } else if (step == 18) {
                        op = COOP_COMBAT_ATTACK;
                        value = 0;
                        coop_object_id_from_engine(coop_campaign_member((unsigned)((local + 1) % 4))->object_id, &id);
                        expected_status = COOP_COMBAT_TARGET;
                    } else if (step == 25 || step == 26 || step == 31) {
                        op = COOP_COMBAT_IMMORTAL;
                        value = step == 31 ? 4 : 5;
                        generation = 0; /* Administration also survives stale turns. */
                        expected_status = host ? COOP_COMBAT_OK : COOP_COMBAT_HOST_ONLY;
                    } else if (step == 30) {
                        op = COOP_COMBAT_POLICY; value = 0;
                    }
                    if (end_step && combat_turn_generation_get() > stage_generation)
                        sent = true;
                    else {
                        last_request = (CoopCombatRequest) { .epoch = coop_runtime_epoch(), .sequence = expected, .map = (uint32_t)map_current_map(), .operation = op, .value = value, .target = id, .generation = generation };
                        memcpy(last_request.actor, actor_id, 16);
                        if (step == 25 || step == 31) {
                            immortality_click(game, 2); sent = true;
                        } else if (step == 30) {
                            key(SDL_SCANCODE_SPACE, SDLK_SPACE, true); sent = true;
                        } else if (step == 2) {
                            key(SDL_SCANCODE_R, SDLK_R, true);
                            sent = true;
                        } else if (attack_step || (step == 15 && player_reports)) {
                            if (step == 15) fprintf(stderr, "ARCMP reported real-time click member=%d stance=%d shared-enemy=%d\n", local,
                                combat_critter_is_combat_mode_active(pc), combat_is_party_enemy(wolf));
                            sent = getenv("ARCMP_HUD_UI") ? coop_party_ui_target_actor(wolf,true) : attack_click(wolf);
                        } else if (end_step) {
                            key(SDL_SCANCODE_E, SDLK_E, false);
                            sent = true;
                        } else
                            sent = coop_gameplay_combat_actor(game, actor_id, op, value, id, generation);
                    }
                }
                if (step == 14 && host && !sent) {
                    sent = coop_runtime_combat(COOP_COMBAT_POLICY, 0, OBJ_HANDLE_NULL);
                    expected_status = 0;
                }
                if (step == 16 && !sent && now - step_at > 200) {
                    bool ok = false;
                    legacy_expected = coop_gameplay_last_result(game, &ok) + 1;
                    sent = coop_runtime_halt(obj_field_int64_get(pc, OBJ_F_LOCATION));
                }
                if (step >= 21 && step <= 24 && !sent && (!host || step == 24)) {
                    CoopItems* items = coop_gameplay_items(game);
                    CoopCombatState current = { 0 }, stale;
                    good = good && coop_engine_combat_capture(&current);
                    stale = current;
                    if (step != 24) {
                        stale.generation--;
                        good = good && coop_engine_combat_install(&stale);
                    }
                    bool accepted;
                    legacy_expected = step < 23 ? coop_gameplay_last_result(game, &accepted) + 1
                                                : coop_items_last_result(items, &expected_status) + 1;
                    int64_t loc = obj_field_int64_get(pc, OBJ_F_LOCATION);
                    if (step == 21)
                        sent = coop_gameplay_move(game, location_make(location_get_x(loc) + 1, location_get_y(loc)), false);
                    else if (step == 22)
                        sent = coop_runtime_halt(loc);
                    else {
                        CoopObjectId empty = { 0 };
                        sent = coop_items_submit(items, map_current_map(), obj_get_id(pc).d.g.data, COOP_ITEM_ARRANGE, empty, 0, 1);
                    }
                    good = good && sent;
                    if (step != 24) good = good && coop_engine_combat_install(&current);
                }
                bool complete = !acts;
                unsigned status = 0;
                uint32_t result = coop_battle_last_result(battle, &status);
                if (acts && sent) {
                    complete = end_step && combat_turn_generation_get() > stage_generation;
                    if (result == expected) {
                        complete = status == expected_status;
                        if (!complete) {
                            fprintf(stderr, "ARCMP battle unexpected receipt step=%u status=%u expected=%u\n", step, status, expected_status);
                            good = false;
                        }
                    }
                }
                if (step == 1) complete = complete && combat_is_turn_based() && !combat_turn_based_is_active() && combat_turn_generation_get() > stage_generation;
                if (step == 2) complete = complete && combat_turn_based_is_active() && combat_turn_based_whos_turn_get() == coop_campaign_object(1);
                if (attack_step) complete = complete && combat_turn_based_is_active();
                if (end_step) complete = complete && combat_turn_generation_get() > stage_generation;
                if (step == 14) complete = (!host || (sent && result == expected && status == 0)) && !combat_is_turn_based() && !combat_turn_based_is_active();
                if (step == 15) complete = complete && obj_field_int32_get(wolf, OBJ_F_HP_DAMAGE) > tb_damage && now - step_at > 700;
                if (step == 16) {
                    bool accepted = false;
                    uint32_t sequence = coop_gameplay_last_result(game, &accepted);
                    complete = sent && sequence == legacy_expected && accepted;
                    if (sent && sequence == legacy_expected && !accepted) sent = false;
                }
                if (step >= 21 && step <= 24) {
                    complete = host && step != 24;
                    if (sent) {
                        if (step < 23) {
                            bool accepted = true;
                            complete = coop_gameplay_last_result(game, &accepted) == legacy_expected && !accepted;
                        } else {
                            unsigned item_status;
                            uint32_t sequence = coop_items_last_result(coop_gameplay_items(game), &item_status);
                            complete = sequence == legacy_expected && item_status == (step == 24 ? 0 : COOP_ITEM_UNAVAILABLE);
                        }
                    }
                }
                if (step == 19) {
                    if (!sent) {
                        uint8_t bytes[COOP_COMBAT_REQUEST_BYTES];
                        good = coop_combat_request_encode(&last_request, bytes);
                        if (good) sent = host ? coop_battle_message(battle, 0, COOP_COMBAT_REQUEST, bytes, sizeof(bytes)) : coop_session_send(session, 0, COOP_COMBAT_REQUEST, bytes, sizeof(bytes));
                        good = good && sent;
                    }
                    complete = sent && coop_battle_executed(battle) == replay_count;
                }
                if (party_ui && step >= 25) {
                    if (step == 25 || step == 26) complete &= coop_engine_options_immortal(coop_campaign_object(2));
                    if (step == 27) complete = coop_engine_options_immortal(coop_campaign_object(2)) && object_hp_current(coop_campaign_object(2)) == 1;
                    if (step == 28) {
                        complete = object_hp_current(coop_campaign_object(2)) > 1;
                        if (!sent) {
                            if (!preview_phase) {
                                sub_579FA0(pc, 1);
                                good &= intgame_mode_get() == INTGAME_MODE_SKILL && iso_interface_window_get() == ROTWIN_TYPE_MSG;
                                preview_at = now; preview_phase = 1;
                            } else if (preview_phase == 1 && now - preview_at > 800) {
                                good &= iso_interface_window_get() == ROTWIN_TYPE_MSG;
                                if (framebuffer) good &= tig_video_screenshot_save("arcmp-party-skill-description.bmp") == TIG_OK;
                                skill_ui_cancel();
                                good &= spell_ui_activate(pc, SPELL_MINOR_HEALING) == SPELL_UI_ACTIVATE_OK;
                                good &= intgame_mode_get() == INTGAME_MODE_SPELL && iso_interface_window_get() == ROTWIN_TYPE_MSG;
                                preview_phase = 2; preview_at = now;
                                tig_mouse_set_position(32, 44 + local * 88 + 24);
                            } else if (preview_phase == 2 && now - preview_at > 1000) {
                                good &= iso_interface_window_get() == ROTWIN_TYPE_MSG;
                                if (framebuffer) good &= tig_video_screenshot_save("arcmp-party-spell-tooltip.bmp") == TIG_OK;
                                if (local == 2) {
                                    unsigned status; magic_expected = coop_magic_last_result(coop_gameplay_magic(game), &status) + 1;
                                    click(32, 44 + local * 88 + 24);
                                } else spell_ui_cancel();
                                sent = true;
                            }
                        }
                        if (local == 2 && sent) {
                            unsigned status;
                            complete &= coop_magic_last_result(coop_gameplay_magic(game), &status) == magic_expected && !status;
                        }
                        complete &= sent;
                    }
                    if (step == 29) complete = critter_is_dead(coop_campaign_object(0)) && combat_turn_based_is_active();
                    if (step == 30) complete &= critter_is_dead(coop_campaign_object(0)) && !combat_is_turn_based() && !combat_turn_based_is_active();
                    if (step == 31) {
                        complete &= !coop_engine_options_immortal(coop_campaign_object(2));
                        if (display_features) {
                            complete &= coop_party_ui_statuses(coop_campaign_object(0)) == COOP_STATUS_DEAD
                                && (coop_party_ui_statuses(coop_campaign_object(1)) & (COOP_STATUS_POISONED|COOP_STATUS_TIRED)) == (COOP_STATUS_POISONED|COOP_STATUS_TIRED)
                                && (coop_party_ui_statuses(coop_campaign_object(3)) & (COOP_STATUS_BLINDED|COOP_STATUS_STUNNED)) == (COOP_STATUS_BLINDED|COOP_STATUS_STUNNED);
                        }
                    }
                    if (host && complete && coop_lobby_is_open()) {
                        if (framebuffer && step == 25) good &= tig_video_screenshot_save("arcmp-party-immortality.bmp") == TIG_OK;
                        key(SDL_SCANCODE_F12, SDLK_F12, true);
                    }
                    if (framebuffer && complete && step == 29) good &= capture("arcmp-party-dead-host-turn.bmp", true);
                    if (display_features && step==31 && complete && good) {
                        if (coop_lobby_is_open()) complete = false;
                        else if (!display_checked) {
                            good &= display_projection_probe(framebuffer); display_checked = true;
                            fprintf(stderr,"ARCMP display projection member=%d passed=%d\n",local,good);
                        }
                    }
                }
                if (complete && good && step==final_step && getenv("ARCMP_MODERN_UI")) complete=modern_coordination_check(now);
                if (complete && good) {
                    if (step >= 21 && step <= 23 && !host) stale_noncombat++;
                    if (step == 24) fresh_item = true;
                    if (acts && attack_step) tb_attacks++;
                    if (acts && step == 15) rt_attacks++;
                    if (acts && expected_status) denials++;
                    if (step == 2) {
                        if (party_ui) {
                            CoopCombatState order;
                            good &= coop_engine_combat_capture(&order) && order.order_count > 0
                                && !memcmp(&order.actor, &order.order[0], sizeof(order.actor));
                        }
                        if (coop_authority_get() == COOP_AUTHORITY_REPLICA) {
                            uint32_t generation = combat_turn_generation_get();
                            int points = combat_action_points_get();
                            int flags = obj_field_int32_get(pc, OBJ_F_CRITTER_FLAGS);
                            bool denied = !combat_consume_action_points(pc, 1) && !anim_goal_attack(pc, wolf);
                            combat_turn_based_end_critter_turn(pc);
                            combat_critter_activate_combat_mode(pc);
                            replica_guard = denied && generation == combat_turn_generation_get() && points == combat_action_points_get() && flags == obj_field_int32_get(pc, OBJ_F_CRITTER_FLAGS);
                            good = good && replica_guard;
                        }
                        good = good && capture("arcmp-battle-turn.bmp", framebuffer);
                    }
                    if (step == 15) good = capture("arcmp-battle-realtime.bmp", framebuffer);
                    char text[40];
                    snprintf(text, sizeof(text), "battle-done:%u", step);
                    good = good && coop_session_chat(session, text);
                    done = true;
                    completed = step;
                    fprintf(stderr, "ARCMP battle completed step=%u member=%d\n", step, local);
                }
                if (now - step_at > 20000) {
                    fprintf(stderr, "ARCMP battle timeout step=%u member=%d sent=%d result=%u expected=%u gen=%u target-damage=%d\n", step, local, sent, result, expected, combat_turn_generation_get(), obj_field_int32_get(wolf, OBJ_F_HP_DAMAGE));
                    good = false;
                }
            }
            if (host && step && !weapons_started && !magic_started && !use_started && !skills_started && !progression_started && now - step_at > 200) {
                bool all = true;
                for (unsigned i = 0; i < 4; i++)
                    all = all && ack[i] == step;
                if (all) {
                    if (loss && step == 2) {
                        coop_session_leave(session);
                        continue;
                    }
                    if (replay_fault && step == 19) {
                        if (!fault_sent) good = coop_session_chat(session, "battle-fault");
                        fault_sent = true;
                    } else if (step == final_step) {
                        if (progression) {
                            if (coop_battle_settled(battle)) {
                                object_flags_set(target(), OF_OFF);
                                progression_started = true;
                            }
                        } else if (skills) {
                            if (coop_battle_settled(battle)) {
                                object_flags_set(target(), OF_OFF);
                                skills_started = true;
                            }
                        } else if (use) {
                            if (coop_battle_settled(battle)) {
                                object_flags_set(target(), OF_OFF);
                                use_started = true;
                            }
                        } else if (magic) {
                            if (coop_battle_settled(battle)) {
                                object_flags_set(target(), OF_OFF);
                                good = coop_magic_probe_start(magic_probe);
                                magic_started = good;
                            }
                        } else if (weapons) {
                            if (coop_battle_settled(battle)) {
                                object_flags_set(target(), OF_OFF);
                                good = coop_weapon_probe_start(weapon_probe);
                                weapons_started = good;
                            }
                        } else if (!finishing && coop_battle_settled(battle)) {
                            if (player_reports && !visual_at) {
                                int64_t walker = 0;
                                good &= object_create(sub_4685A0(BP_WOLF), target_origin, &walker);
                                if (walker) {
                                    Script none = { 0 };
                                    for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(walker, OBJ_F_SCRIPTS_IDX, sap, &none);
                                    obj_field_int32_set(walker, OBJ_F_NPC_FLAGS, (obj_field_int32_get(walker, OBJ_F_NPC_FLAGS) & ~(ONF_FIGHTING | ONF_KOS)) | ONF_AI_WAIT_HERE);
                                    int64_t loc = obj_field_int64_get(walker, OBJ_F_LOCATION);
                                    bool walking = anim_goal_walk_to_tile(walker, location_make(location_get_x(loc) + 12, location_get_y(loc) + 4));
                                    fprintf(stderr, "ARCMP visual live-NPC walk accepted=%d hp=%d\n", walking, object_hp_current(walker));
                                    good &= walking && critter_is_active(walker);
                                }
                                visual_at = now;
                                good &= coop_session_chat(session, "battle-visual-begin");
                            }
                            if (!player_reports || now - visual_at > 3200) finishing = coop_gameplay_finish(game);
                        }
                    } else {
                        bool can_advance = true;
                        unsigned next = step + 1;
                        int next_actor = actor;
                        uint32_t generation = combat_turn_generation_get();
                        if (display_features && next == 31) {
                            int64_t poisoned=coop_campaign_object(1), stunned=coop_campaign_object(3);
                            stat_base_set(poisoned,STAT_POISON_LEVEL,15);
                            critter_fatigue_damage_set(poisoned,critter_fatigue_max(poisoned)*4/5);
                            obj_field_int32_set(stunned,OBJ_F_CRITTER_FLAGS,obj_field_int32_get(stunned,OBJ_F_CRITTER_FLAGS)|OCF_STUNNED|OCF_BLINDED);
                        }
                        if (party_ui && next == 27) {
                            good &= coop_engine_environment_save() && coop_engine_options_immortal_set(2, false)
                                && coop_engine_environment_load() && coop_engine_options_immortal(coop_campaign_object(2));
                            critter_kill(coop_campaign_object(2));
                            good &= object_hp_current(coop_campaign_object(2)) == 1;
                        }
                        if (party_ui && next == 29) {
                            /* Finish the preceding healing animation before
                             * asking native combat to change timing. */
                            for (unsigned i = 0; i < 4; i++) can_advance &= idle(coop_campaign_object(i));
                            if (can_advance) can_advance = sub_4B6C90(true);
                            if (can_advance) {
                                combat_critter_activate_combat_mode(coop_campaign_object(1));
                                critter_kill(coop_campaign_object(0));
                                good &= critter_is_dead(coop_campaign_object(0)) && combat_turn_based_is_active();
                                generation = combat_turn_generation_get();
                            }
                        }
                        if (attack_step) {
                            can_advance = idle(coop_campaign_object((unsigned)actor));
                            if (!can_advance && now - step_at > 20000) {
                                AnimID id;
                                AnimRunInfo* run = NULL;
                                int64_t object = coop_campaign_object((unsigned)actor);
                                if (sub_423300(object, &id)) anim_id_to_run_info(&id, &run);
                                fprintf(stderr, "ARCMP battle native goal stalled step=%u actor=%d turn=%d hp=%d fatigue=%d distance=%lld root=%d goal=%d flags=%x\n", step, actor,
                                    coop_campaign_slot_for_object(combat_turn_based_whos_turn_get()), object_hp_current(object), critter_fatigue_current(object), (long long)object_dist(object, wolf),
                                    run ? run->goals[0].type : -1, run ? run->cur_stack_data->type : -1, run ? run->flags : 0);
                                good = false;
                            }
                            attacked |= 1u << actor;
                            generation = stage_generation;
                        }
                        if (next >= 6 && next <= 12 && !(next & 1)) {
                            next_actor = coop_campaign_slot_for_object(combat_turn_based_whos_turn_get());
                            can_advance = next_actor >= 0 && !(attacked & (1u << next_actor));
                        }
                        if (step == 13) {
                            tb_damage = obj_field_int32_get(wolf, OBJ_F_HP_DAMAGE);
                            good = good && attacked == 15 && tb_damage > 0;
                        }
                        if (next == 17) {
                            sub_43E770(wolf, location_make(location_get_x(target_origin) + 80, location_get_y(target_origin)), 0, 0);
                            target_far = true;
                        }
                        if (step == 17 && target_far) {
                            sub_43E770(wolf, target_origin, 0, 0);
                            target_far = false;
                        }
                        if (can_advance && next == 15 && player_reports) {
                            for (unsigned i = 0; i < 4; i++) {
                                int64_t human = coop_campaign_object(i);
                                sub_424070(human, PRIORITY_HIGHEST, false, false);
                                combat_critter_deactivate_combat_mode(human);
                            }
                            obj_field_int32_set(wolf, OBJ_F_NPC_FLAGS, obj_field_int32_get(wolf, OBJ_F_NPC_FLAGS) | ONF_FIGHTING);
                            obj_field_handle_set(wolf, OBJ_F_NPC_COMBAT_FOCUS, coop_campaign_object(0));
                            generation = combat_turn_generation_get();
                        }
                        if (can_advance && good) good = announce(session, next, next_actor, generation);
                    }
                }
            }
            if (progression && good) {
                int result = coop_progression_probe_poll(progression_probe, now, host && progression_started);
                if (result < 0) good = false;
                if (host && result == 1 && !finishing) finishing = coop_gameplay_finish(game);
            }
            if (use && good) {
                int result = coop_use_probe_poll(use_probe, now, host && use_started);
                if (result < 0) good = false;
                if (host && result == 1 && !finishing) { finishing = coop_use_probe_finish(use_probe); good = finishing; }
            }
            if (weapons && good) {
                int result = coop_weapon_probe_poll(weapon_probe, now);
                if (result < 0) good = false;
                if (host && result == 1 && !finishing) finishing = coop_gameplay_finish(game);
            }
            if (skills && good) {
                int result = coop_skills_probe_poll(skills_probe, now, host && skills_started);
                if (result < 0) good = false;
                if (host && result == 1 && !finishing) {
                    int finish = coop_skills_probe_finish(skills_probe);
                    if (finish < 0) good = false;
                    else finishing = finish == 1;
                }
            }
            if (magic && good) {
                int result = coop_magic_probe_poll(magic_probe, now);
                if (result < 0) good = false;
                if (good) {
                    int audio_result = coop_audio_probe_poll(audio_probe, now, host && result == 1);
                    if (audio_result < 0) good = false;
                    if (host && audio_result == 1 && !finishing) finishing = coop_gameplay_finish(game);
                }
            }
            if (replay_fault && !host && !fault_sent && step == 19 && done) {
                for (uint64_t i = 0; i < coop_session_chat_count(session); i++) {
                    const CoopChat* c = coop_session_chat_at(session, i);
                    if (c && !c->slot && !strcmp(c->text, "battle-fault") && coop_session_local_slot(session) == 1) {
                        uint8_t bytes[51];
                        last_request.value ^= 1;
                        good = coop_combat_request_encode(&last_request, bytes) && coop_session_send(session, 0, COOP_COMBAT_REQUEST, bytes, sizeof(bytes));
                        fault_sent = true;
                        break;
                    }
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
    if (good && SDL_GetTicks() - start >= duration_limit)
        fprintf(stderr, "ARCMP battle total deadline elapsed=%" PRIu64 " limit=%" PRIu64 " step=%u member=%d\n", (uint64_t)(SDL_GetTicks() - start), duration_limit, step, local);
    CoopBattle* battle = game ? coop_gameplay_battle(game) : NULL;
    bool frozen = false;
    if (loss || replay_fault) {
        CoopCombatState before, after;
        DateTime clock = sub_45A7C0();
        good = good && coop_engine_combat_capture(&before);
        bool denied = !coop_runtime_combat(COOP_COMBAT_END_TURN, 0, OBJ_HANDLE_NULL);
        tig_timestamp_t timestamp;
        tig_timer_now(&timestamp);
        timeevent_ping(timestamp);
        gamelib_ping();
        DateTime next = sub_45A7C0();
        uint8_t first[COOP_COMBAT_STATE_BYTES], second[COOP_COMBAT_STATE_BYTES];
        good = good && coop_engine_combat_capture(&after) && coop_combat_state_encode(&before, first) && coop_combat_state_encode(&after, second);
        frozen = good && coop_runtime_phase() == COOP_RUNTIME_FAILED && denied && datetime_compare(&clock, &next) == 0 && !memcmp(first, second, sizeof(first));
        good = good && frozen && completed == (loss ? 2u : 19u) && game && !coop_gameplay_finishing(game);
    } else
        good = good && game && coop_gameplay_finished(game) && completed == final_step;
    if (weapons) good = good && coop_weapon_probe_report(weapon_probe);
    if (magic) good = good && coop_magic_probe_report(magic_probe);
    if (magic) good = good && coop_audio_probe_report(audio_probe);
    if (use) good = good && coop_use_probe_report(use_probe);
    if (skills) good = good && coop_skills_probe_report(skills_probe);
    if (progression) good = good && coop_progression_probe_report(progression_probe);
    coop_audio_probe_stop(audio_probe);
    FILE* file = fopen("arcmp-battle-result.json", "wb");
    if (file) {
        int64_t wolf = target();
        fprintf(file, "{\"verified\":%s,\"steps\":%u,\"local_member\":%d,\"turn_based_attacks\":%u,\"real_time_attacks\":%u,\"denials\":%u,\"executed\":%u,\"rejected\":%u,\"target_damage\":%d,\"stale_noncombat_rejections\":%u,\"fresh_item_after_turn\":%s,\"replica_mutation_guard_checked\":%s,\"loss_during_combat\":%s,\"replay_conflict\":%s,\"disconnect_freeze\":%s}\n", good ? "true" : "false", completed, local, tb_attacks, rt_attacks, denials, battle ? coop_battle_executed(battle) : 0, battle ? coop_battle_rejected(battle) : 0, wolf ? obj_field_int32_get(wolf, OBJ_F_HP_DAMAGE) : -1, stale_noncombat, fresh_item ? "true" : "false", replica_guard ? "true" : "false", loss ? "true" : "false", replay_fault ? "true" : "false", frozen ? "true" : "false");
        if (fclose(file)) good = false;
    } else
        good = false;
    fprintf(stderr, "ARCMP battle probe %s steps=%u member=%d tb-attacks=%u rt-attacks=%u\n", good ? "PASS" : "FAIL", completed, local, tb_attacks, rt_attacks);
    return good;
}
