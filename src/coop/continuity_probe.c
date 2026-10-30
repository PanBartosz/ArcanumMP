#include "coop/continuity_probe.h"
#include "coop/runtime.h"
#include "coop/campaign.h"
#include "coop/lobby_ui.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_object.h"
#include "coop/social.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/dialog.h"
#include "game/effect.h"
#include "game/gamelib.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/player.h"
#include "game/path.h"
#include "game/quest.h"
#include "game/reputation.h"
#include "game/script.h"
#include "game/stat.h"
#include "ui/follower_ui.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/logbook_ui.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PHASES = 17, TAG = 188800 };
static CoopSession* held;
static int bindings[4] = { -1, -1, -1, -1 };
static int xp_expected[4], alignment_expected[4];
static int64_t walk_destination;
static int64_t pc(unsigned slot)
{
    int member = coop_session_is_host(held) ? coop_campaign_slot_for_owner(coop_session_players(held)[slot].identity) : bindings[slot];
    return member < 0 ? 0 : coop_campaign_object((unsigned)member);
}
static int64_t follower(unsigned slot)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && obj_field_int32_get(obj, OBJ_F_NPC_PAD_I_1) == TAG + (int)slot) return obj;
    } while (obj_inst_next(&obj, &it));
    return 0;
}
static bool content(uint8_t id[32])
{
    FILE* f = fopen("arcmp-content.id", "rb"); char hex[66] = { 0 };
    if (!f) return false;
    bool good = fread(hex, 1, 65, f) >= 64; fclose(f);
    for (unsigned i = 0; good && i < 32; i++) { char pair[] = { hex[2*i], hex[2*i+1], 0 }, *end; id[i] = (uint8_t)strtoul(pair, &end, 16); good = !*end; }
    return good;
}
static void click(int x, int y, bool right)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE }; m.data.mouse.x = x; m.data.mouse.y = y;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE; tig_message_enqueue(&m);
    m.data.mouse.event = right ? TIG_MESSAGE_MOUSE_RIGHT_BUTTON_DOWN : TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event = right ? TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP : TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
}
static bool image(unsigned phase, bool framebuffer)
{
    if (!framebuffer) return true;
    gamelib_redraw(); iso_redraw(); tig_window_display(); char name[80]; snprintf(name, sizeof(name), "arcmp-continuity-%u.bmp", phase);
    return tig_video_screenshot_save(name) == TIG_OK;
}
static bool prepare(void)
{
    int64_t obj, prototype = 0; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
        Script script = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
        if (script.num == 1324) prototype = obj_field_handle_get(obj, OBJ_F_PROTOTYPE_HANDLE);
        object_flags_set(obj, OF_OFF);
    } while (obj_inst_next(&obj, &it));
    if (!prototype) return false;
    for (unsigned i = 0; i < 4; i++) {
        int64_t actor = pc(i), loc = obj_field_int64_get(actor, OBJ_F_LOCATION), npc;
        if (!object_create(prototype, location_make(location_get_x(loc) + 2, location_get_y(loc) + 2), &npc)) return false;
        Script none = { 0 };
        for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(npc, OBJ_F_SCRIPTS_IDX, sap, &none);
        obj_field_int32_set(npc, OBJ_F_NPC_PAD_I_1, TAG + (int)i);
        obj_field_int32_set(npc, OBJ_F_NPC_FLAGS, obj_field_int32_get(npc, OBJ_F_NPC_FLAGS) | ONF_FORCED_FOLLOWER);
        if (!critter_follow(npc, actor, true)) return false;
        stat_base_set(actor, STAT_ALIGNMENT, (int)i - 2);
    }
    script_global_var_set(0, 54001); return sub_4B6C90(true) && sub_4B6C90(false);
}
static int gain(int64_t actor, int base)
{
    int difficulty = gamelib_game_difficulty_get();
    if (difficulty == GAME_DIFFICULTY_EASY) base += base / 2;
    if (difficulty == GAME_DIFFICULTY_HARD) base -= base / 2;
    return critter_is_dead(actor) ? 0 : effect_adjust_xp_gain(actor, base);
}
static bool announce(unsigned phase)
{
    char text[100];
    if (phase == 1 || phase == 8) for (unsigned slot = 0; slot < 4; slot++) {
        snprintf(text, sizeof(text), "continuity-bind:%u:%d", slot, coop_campaign_slot_for_owner(coop_session_players(held)[slot].identity));
        if (!coop_session_chat(held, text)) return false;
    }
    if (phase == 2 && quest_state_set(pc(2), 1000, QUEST_STATE_ACCEPTED, 0) != QUEST_STATE_ACCEPTED) return false;
    if (phase == 3 && quest_state_set(pc(3), 1000, QUEST_STATE_COMPLETED, 0) != QUEST_STATE_COMPLETED) return false;
    if (phase == 4) {
        for (unsigned i = 0; i < 4; i++) {
            xp_expected[i] = stat_base_get(pc(i), STAT_EXPERIENCE_POINTS) + gain(pc(i), quest_get_xp(1));
            alignment_expected[i] = stat_base_get(pc(i), STAT_ALIGNMENT) + 2;
        }
        DialogState state = { .pc_obj = pc(1), .npc_obj = follower(1) }; char actions[] = "xp1 al+2 rp1000";
        if (!dialog_execute_actions(&state, actions, 0)) return false;
    }
    if (phase == 5) {
        DialogState state = { .pc_obj = pc(2), .npc_obj = follower(2) }; char actions[] = "rp-1000 al>5 al<3";
        if (!dialog_execute_actions(&state, actions, 0)) return false;
        for (unsigned i = 0; i < 4; i++) alignment_expected[i] = 3;
    }
    if (phase == 6) {
        for (unsigned i = 0; i < 4; i++) xp_expected[i] += gain(pc(i), quest_get_xp(1));
        Script reward = { .num = 999997 };
        ScriptInvocation invocation = { .script = &reward, .triggerer_obj = pc(3), .attachment_point = SAP_USE };
        script_execute(&invocation);
        /* Duplicate terminal quest completions must not share XP a second time. */
        for (unsigned i = 0; i < 4; i++) quest_state_set(pc(i), 1000, QUEST_STATE_COMPLETED, 0);
    }
    if (phase >= 4 && phase <= 6) for (unsigned i = 0; i < 4; i++) {
        snprintf(text, sizeof(text), "continuity-expect:%u:%d:%d", i, xp_expected[i], alignment_expected[i]);
        if (!coop_session_chat(held, text)) return false;
    }
    if (phase == 12) {
        int64_t npc = follower(1), from = obj_field_int64_get(npc, OBJ_F_LOCATION);
        bool found = false;
        for (int dy = -3; dy <= 3 && !found; dy++) for (int dx = -3; dx <= 3 && !found; dx++) {
            if (abs(dx) != 3 && abs(dy) != 3) continue;
            int64_t target = location_make(location_get_x(from) + dx, location_get_y(from) + dy);
            int8_t rotations[128]; PathCreateInfo path = { .obj = npc, .from = from, .to = target, .max_rotations = 128, .rotations = rotations };
            if (path_make(&path) > 0) { walk_destination = target; found = true; }
        }
        if (!found) return false;
        snprintf(text, sizeof(text), "continuity-walk:%lld:%lld", (long long)location_get_x(walk_destination), (long long)location_get_y(walk_destination));
        if (!coop_session_chat(held, text)) return false;
    }
    snprintf(text, sizeof(text), "continuity-phase:%u", phase); return coop_session_chat(held, text);
}
void coop_continuity_probe_cleanup(void)
{
    if (held) { coop_runtime_detach(); coop_session_destroy(held); held = NULL; }
}
bool coop_continuity_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false, ui_tail = false; unsigned port = 35744, player = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-companion-ui")) ui_tail = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= 4 || host != (player == 0)) return false;
    CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Continuity player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    held = coop_session_create(&config); if (!held) return false;
    uint64_t start = SDL_GetTicks(), playing = 0, chats = 0, at = 0, ended = 0;
    bool good = (host ? coop_session_host(held, (uint16_t)port, start) : coop_session_join(held, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(held, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    bool selected = false, initialized = false, sent = false, done = false, finishing = false, replayed = false, guards = false, arrived = false;
    unsigned phase = 0, completed = 0, ack[4] = { 0 }, expected_result = 0, denials = 0, replays = 0, menu_step = 0; uint32_t journal_revision = 0;
    uint64_t menu_at = 0;
    int64_t walk = 0; CoopFollowerRequest replay = { 0 };
    while (good && SDL_GetTicks() - start < 180000) {
        uint64_t now = SDL_GetTicks(); tig_ping(); coop_session_poll(held, now); coop_lobby_tick();
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) { fprintf(stderr, "ARCMP continuity runtime: %s\n", coop_runtime_status()); good = false; break; }
        CoopGameplay* game = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(held) == COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name, sizeof(name), "Continuity hero %u", player); selected = coop_session_character(held, 16067 + player, name); }
            else if (coop_session_players(held)[coop_session_local_slot(held)].prototype && !coop_session_players(held)[coop_session_local_slot(held)].ready) coop_session_ready(held, true);
            unsigned count = 0; for (unsigned i = 0; i < 4; i++) count += coop_session_players(held)[i].connected;
            if (host && count == 4 && coop_session_all_ready(held)) good = coop_runtime_start();
        }
        if (game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing) playing = now;
            const CoopDialogueView* d = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!phase && d->active && d->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (!initialized && host && !d->active && now - playing > 1200) {
                initialized = true; good = prepare();
                if (ui_tail) for (unsigned i = 0; i < 4; i++) stat_base_set(pc(i), STAT_ALIGNMENT, 3);
                good = good && announce(ui_tail ? 8 : 1);
            }
        }
        CoopFollowers* orders = game ? coop_gameplay_followers(game) : NULL;
        for (; chats < coop_session_chat_count(held); chats++) {
            const CoopChat* chat = coop_session_chat_at(held, chats); unsigned value, slot, member; int xp, alignment; long long x, y;
            if (!chat->slot && sscanf(chat->text, "continuity-walk:%lld:%lld", &x, &y) == 2) walk_destination = location_make(x, y);
            if (!chat->slot && !strcmp(chat->text, "continuity-arrived")) arrived = true;
            if (!chat->slot && sscanf(chat->text, "continuity-bind:%u:%u", &slot, &member) == 2) {
                if (slot >= 4 || member >= 4) { good = false; break; } bindings[slot] = (int)member;
            }
            if (!chat->slot && sscanf(chat->text, "continuity-expect:%u:%d:%d", &slot, &xp, &alignment) == 3) {
                if (slot >= 4) { good = false; break; } xp_expected[slot] = xp; alignment_expected[slot] = alignment;
            }
            if (!chat->slot && sscanf(chat->text, "continuity-phase:%u", &value) == 1) {
                if (value != (!phase && ui_tail ? 8 : phase + 1) || value > PHASES) { good = false; break; }
                phase = value; at = now; sent = done = replayed = false; menu_step = 0; menu_at = now;
                unsigned status; expected_result = coop_followers_last_result(orders, &status) + 1;
                fprintf(stderr, "ARCMP continuity phase=%u slot=%u\n", phase, coop_session_local_slot(held));
            }
            if (chat->slot < 4 && sscanf(chat->text, "continuity-done:%u", &value) == 1) ack[chat->slot] = value;
        }
        uint8_t slot = coop_session_local_slot(held);
        bool visible_party = true;
        if (ui_tail && phase == 8) for (unsigned i = 0; i < 4; i++) visible_party &= follower(i) && pc(i);
        if (host && phase == 12 && !arrived && follower(1) && location_dist(obj_field_int64_get(follower(1), OBJ_F_LOCATION), walk_destination) <= 1) {
            good &= coop_session_chat(held, "continuity-arrived"); arrived = true;
        }
        if (phase && !done && game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && now - at > 350 && visible_party) {
            bool ready = true;
            if (phase == 1) {
                for (unsigned i = 0; i < 4; i++) ready &= follower(i) && critter_pc_leader_get(follower(i)) == pc(i);
                if (ready && !sent) { logbook_ui_open_quests(player_get_local_pc_obj()); sent = true; }
                int tab, page, count; ready &= logbook_ui_view(&tab, &page, &count, &journal_revision) && tab == 1;
            } else if (phase == 2 || phase == 3) {
                bool found = false; int tab, page, count; uint32_t revision;
                ready &= logbook_ui_view(&tab, &page, &count, &revision) && revision > journal_revision && tab == 1;
                if (ready) for (int i = 0; i < count; i++) { int id, state; if (logbook_ui_entry(i, &id, &state) && id == 1000 && state == (phase == 2 ? QUEST_STATE_ACCEPTED : QUEST_STATE_COMPLETED)) found = true; }
                ready &= found;
                if (ready) { journal_revision = revision; good &= image(phase, framebuffer); }
            } else if (phase >= 4 && phase <= 6) {
                if (phase == 4 && !sent) { click(710, 185, false); sent = true; }
                for (unsigned i = 0; i < 4; i++) ready &= stat_base_get(pc(i), STAT_EXPERIENCE_POINTS) == xp_expected[i] && stat_base_get(pc(i), STAT_ALIGNMENT) == alignment_expected[i]
                    && reputation_has(pc(i), 1000) == (phase == 4);
                int tab, page, count; uint32_t revision; ready &= logbook_ui_view(&tab, &page, &count, &revision) && tab == 2;
                if (phase == 4) { bool found = false; for (int i = 0; i < count; i++) { int id, state; if (logbook_ui_entry(i, &id, &state) && id == 1000) found = true; } ready &= found; }
                if (ready && phase == 5) good &= image(phase, framebuffer);
                if (ready && phase == 6) { logbook_ui_close(); follower_ui_refresh(); }
            } else if (phase >= 7 && phase <= 15) {
                /* Owner 1 exercises native portrait orders. Other owners probe
                 * cross-owner refusal and stale combat-generation refusal. */
                unsigned acting = phase == 7 ? 2 : 1;
                CoopFollowerOrder order = phase == 8 ? COOP_FOLLOW_SPREAD : phase == 9 ? COOP_FOLLOW_CLOSE : phase == 10 ? COOP_FOLLOW_WAIT
                    : phase == 11 ? COOP_FOLLOW_RESUME : phase == 12 ? COOP_FOLLOW_WALK : phase == 13 ? COOP_FOLLOW_BACK : COOP_FOLLOW_CLOSE;
                if (slot == acting && !sent) {
                    int64_t npc = follower(1);
                    walk = walk_destination;
                    replay = (CoopFollowerRequest) { .epoch = coop_runtime_epoch(), .sequence = expected_result, .map = (uint32_t)map_current_map(), .order = order, .generation = combat_turn_generation_get() + (phase == 15 ? 1 : 0) };
                    memcpy(replay.actor, obj_get_id(player_get_local_pc_obj()).d.g.data, 16); coop_object_id_from_engine(obj_get_id(npc), &replay.follower);
                    if (phase == 12) { replay.x = (uint32_t)location_get_x(walk); replay.y = (uint32_t)location_get_y(walk); }
                    if (ui_tail && phase >= 8 && phase <= 11) {
                        if (!menu_step) {
                            if (!follower_ui_is_visible()) click(30, 435, false);
                            menu_step++; menu_at = now;
                        } else if (menu_step == 1 && now - menu_at > 250) {
                            click(30, 70, true); menu_step++; menu_at = now;
                        } else if (menu_step == 2 && now - menu_at > 250) {
                            if (phase == 11) good &= image(90, framebuffer);
                            unsigned index = phase == 8 ? 3 : phase == 9 ? 2 : 5;
                            click(80, 50 + 3 + (int)index * 18 + 7, false); sent = true;
                        }
                    } else { good = coop_followers_submit(orders, &replay); sent = true; }
                }
                if (slot == acting) {
                    unsigned status; ready &= coop_followers_last_result(orders, &status) == expected_result;
                    unsigned expected_status = phase == 7 ? COOP_FOLLOW_OWNER : phase == 15 ? COOP_FOLLOW_STALE : COOP_FOLLOW_OK;
                    ready &= status == expected_status;
                    if (ready && phase == 14 && !replayed) {
                        uint8_t bytes[COOP_FOLLOWER_REQUEST_BYTES]; good &= coop_follower_encode(&replay, bytes) && coop_session_send(held, 0, COOP_FOLLOWER_REQUEST, bytes, sizeof(bytes));
                        replayed = true; replays++; ready = false;
                    }
                    if (ready && (phase == 7 || phase == 15)) denials++;
                }
                int flags = obj_field_int32_get(follower(1), OBJ_F_NPC_FLAGS);
                if (phase == 8) ready &= (flags & ONF_AI_SPREAD_OUT) != 0;
                if (phase == 9) ready &= (flags & ONF_AI_SPREAD_OUT) == 0;
                if (phase == 10) ready &= (flags & ONF_AI_WAIT_HERE) != 0;
                if (phase == 11) ready &= (flags & ONF_AI_WAIT_HERE) == 0;
                if (phase == 12) ready &= arrived;
                if (phase == 10 && ready) good &= image(phase, framebuffer);
            } else if (phase == 16) {
                if (!guards && !host) {
                    int xp = stat_base_get(player_get_local_pc_obj(), STAT_EXPERIENCE_POINTS);
                    coop_social_xp(player_get_local_pc_obj(), 9999); coop_social_reputation(player_get_local_pc_obj(), 1000, false); coop_social_alignment(player_get_local_pc_obj(), '=', -1000);
                    guards = stat_base_get(player_get_local_pc_obj(), STAT_EXPERIENCE_POINTS) == xp && !reputation_has(player_get_local_pc_obj(), 1000) && stat_base_get(player_get_local_pc_obj(), STAT_ALIGNMENT) == 3;
                    good &= guards;
                }
                ready &= host || guards;
            } else if (phase == 17) ready &= coop_followers_settled(orders);
            if (ready && good) {
                char text[64]; snprintf(text, sizeof(text), "continuity-done:%u", phase); good = coop_session_chat(held, text); done = true; completed = phase;
            }
        }
        if (phase && !done && now - at > 25000) {
            int64_t npc = follower(1), loc = npc ? obj_field_int64_get(npc, OBJ_F_LOCATION) : 0;
            fprintf(stderr, "ARCMP continuity timeout phase=%u slot=%u sent=%d result=%u npc=%lld,%lld target=%lld,%lld arrived=%d\n", phase, slot, sent, expected_result,
                (long long)location_get_x(loc), (long long)location_get_y(loc), (long long)location_get_x(walk_destination), (long long)location_get_y(walk_destination), arrived);
            image(99, framebuffer); good = false;
        }
        if (host && done && !finishing && game) {
            bool all = true; for (unsigned i = 0; i < 4; i++) all &= ack[i] == phase;
            if (all) { if (phase < PHASES) good = good && announce(phase + 1); else { good = good && coop_gameplay_finish(game); finishing = true; } }
        }
        if (game && coop_gameplay_finished(game)) { if (!ended) ended = now; if (now - ended > 600) break; }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage message; while (tig_message_dequeue(&message) == TIG_OK) { if (message.type == TIG_MESSAGE_QUIT) good = false; else if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&message); }
        SDL_Delay(2);
    }
    CoopGameplay* game = coop_runtime_gameplay(); CoopFollowers* orders = game ? coop_gameplay_followers(game) : NULL;
    good &= game && coop_gameplay_finished(game) && completed == PHASES;
    FILE* report = fopen("arcmp-continuity-result.json", "wb"); if (!report) good = false;
    if (report) { fprintf(report, "{\"verified\":%s,\"phases\":%u,\"first_phase\":%u,\"native_ui\":%s,\"executed\":%u,\"rejected\":%u,\"local_denials\":%u,\"replays\":%u,\"journal_revision\":%u,\"replica_guard\":%s}\n", good ? "true" : "false", completed, ui_tail ? 8 : 1, ui_tail ? "true" : "false", orders ? coop_followers_executed(orders) : 0, orders ? coop_followers_rejected(orders) : 0, denials, replays, journal_revision, guards ? "true" : "false"); if (fclose(report)) good = false; }
    fprintf(stderr, "ARCMP continuity probe %s phases=%u\n", good ? "PASS" : "FAIL", completed); return good;
}
