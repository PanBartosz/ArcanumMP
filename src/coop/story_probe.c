#include "coop/story_probe.h"
#include "coop/runtime.h"
#include "coop/lobby_ui.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "game/anim.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/gmovie.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/script.h"
#include "game/sector.h"
#include "game/timeevent.h"
#include "game/tile.h"
#include "game/townmap.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/slide_ui.h"
#include "ui/mainmenu_ui.h"
#include "ui/gameuilib.h"
#include "tig/message.h"
#include "tig/movie.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PHASES = 9, TAG = 188700, SCENES = 5 };
static CoopSession* held;
static int bindings[4] = { -1, -1, -1, -1 };
static int64_t pc(unsigned slot)
{
    int member = coop_session_is_host(held) ? coop_campaign_slot_for_owner(coop_session_players(held)[slot].identity) : bindings[slot];
    return member < 0 ? 0 : coop_campaign_object((unsigned)member);
}
static int64_t tagged(void)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_type_is_item(obj_field_int32_get(obj, OBJ_F_TYPE)) && obj_field_int32_get(obj, OBJ_F_ITEM_WORTH) == TAG) return obj;
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
static void escape(void)
{
    TigMessage m = { .type = TIG_MESSAGE_KEYBOARD }; m.data.keyboard.scancode = SDL_SCANCODE_ESCAPE; m.data.keyboard.key = SDLK_ESCAPE;
    m.data.keyboard.pressed = true; tig_message_enqueue(&m); m.data.keyboard.pressed = false; tig_message_enqueue(&m);
}
static void agree(void)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE }; m.data.mouse.x = 535; m.data.mouse.y = 565;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
}
typedef struct StoryObserver {
    bool live, host, framebuffer, good, skipped[SCENES], captured[SCENES], was_playing[SCENES];
    unsigned base, slot;
    uint64_t at[SCENES], elapsed[SCENES], frozen, freeze_at;
    ArcMovieInfo movies[SCENES];
    bool ending, terminal;
    unsigned ending_sequence, ending_slides, terminal_id;
    uint64_t ending_at;
} StoryObserver;
static void observer(void* context)
{
    StoryObserver* o = context;
    const CoopPresentView* view = coop_present_current(coop_runtime_presentation());
    if (o->live && o->ending && view && view->sequence > o->base) {
        if (o->ending_sequence != view->sequence) {
            o->ending_sequence = view->sequence; o->ending_at = SDL_GetTicks();
            if (view->kind == COOP_PRESENT_SLIDE) o->ending_slides++;
        }
        if (view->kind == COOP_PRESENT_END && view->phase == COOP_PRESENT_COMPLETE) {
            o->terminal = view->ready == 15 && view->done == 15; o->terminal_id = view->id;
        }
        if (view->kind != COOP_PRESENT_END && SDL_GetTicks() - o->ending_at > 1000) escape();
        if (o->framebuffer && view->kind == COOP_PRESENT_SLIDE && view->phase == COOP_PRESENT_PLAYING && slide_ui_coop_current() == (int)view->id
            && !o->captured[0] && SDL_GetTicks() - o->ending_at > 500) {
            o->good &= tig_video_screenshot_save("arcmp-ending-slide.bmp") == TIG_OK; o->captured[0] = true;
        }
        if (o->framebuffer && view->kind == COOP_PRESENT_MOVIE && tig_movie_is_playing()
            && !o->captured[1] && SDL_GetTicks() - o->ending_at > 500) {
            o->good &= tig_video_screenshot_save("arcmp-ending-movie.bmp") == TIG_OK; o->captured[1] = true;
        }
        return;
    }
    if (!o->live || !view || view->sequence <= o->base || view->sequence > o->base + SCENES) return;
    unsigned scene = view->sequence - o->base - 1;
    bool showing = view->kind == COOP_PRESENT_MOVIE ? tig_movie_is_playing() : slide_ui_is_active();
    if (!showing) return;
    uint64_t now = SDL_GetTicks();
    if (!o->at[scene]) o->at[scene] = now;
    o->elapsed[scene] = now - o->at[scene];
    if (view->kind == COOP_PRESENT_MOVIE) { tig_movie_progress(&o->movies[scene]); o->was_playing[scene] = true; }
    if (!scene && o->host && o->elapsed[scene] > 1200) {
        DateTime clock = sub_45A7C0();
        if (!o->freeze_at) { o->frozen = clock.value; o->freeze_at = now; }
        if (clock.value != o->frozen || !anim_is_current_goal_type(pc(1), AG_PICKUP_ITEM, NULL)) o->good = false;
    }
    if (!o->captured[scene] && o->elapsed[scene] > 500 && o->framebuffer && (scene < 3)) {
        char name[80]; snprintf(name, sizeof(name), "arcmp-cinema-%u.bmp", scene + 1);
        o->good &= tig_video_screenshot_save(name) == TIG_OK; o->captured[scene] = true;
    }
    /* Hold the fourth owner's vote beyond the transport heartbeat deadline.
     * The second movie must instead reach its real video/audio EOF. */
    if (scene != 1 && !o->skipped[scene] && o->elapsed[scene] > (scene == 0 && o->slot == 3 ? 9000u : scene == 0 ? 1000u : 1000u)) {
        escape(); o->skipped[scene] = true;
    }
}
static bool prepare(void)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC) object_flags_set(obj, OF_OFF);
    } while (obj_inst_next(&obj, &it));
    int64_t loc = obj_field_int64_get(pc(1), OBJ_F_LOCATION), item = 0;
    script_global_var_set(0, 53001);
    return object_create(sub_4685A0(BP_SWORD), location_make(location_get_x(loc) + 6, location_get_y(loc)), &item)
        && (obj_field_int32_set(item, OBJ_F_ITEM_WORTH, TAG), true);
}
static bool route(unsigned phase, int64_t* destination)
{
    int64_t from = obj_field_int64_get(pc(2), OBJ_F_LOCATION), sec = sector_id_from_loc(from), max_x, max_y;
    sector_limits_get(&max_x, &max_y);
    int8_t rotations[256];
    for (int radius = phase == 7 ? 2 : phase == 8 ? 4 : 8; radius < (phase == 9 ? 24 : 16); radius++) {
        for (int dir = 0; dir < 8; dir++) {
            static const int dx[] = { 1, 1, 0, -1, -1, -1, 0, 1 }, dy[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
            int64_t x = (int64_t)SECTOR_X(sec) + radius * dx[dir], y = (int64_t)SECTOR_Y(sec) + radius * dy[dir];
            if (x < 1 || y < 1 || x >= max_x || y >= max_y) continue;
            int64_t target = SECTOR_MAKE(x, y);
            if (sector_is_blocked(target) || (phase == 7 && townmap_get(target) != TOWNMAP_NONE)) continue;
            WmapPathInfo path = { .from = sec, .to = target, .max_rotations = 256, .rotations = rotations };
            int steps = wmap_path_make(&path);
            if (steps < (phase == 9 ? 8 : 2) || steps > (phase == 9 ? 64 : 16)) continue;
            int64_t corner = sector_loc_from_id(target);
            *destination = location_make(location_get_x(corner) + 32, location_get_y(corner) + 32);
            if (phase == 7) {
                bool landing = false;
                for (int offset = 0; offset < 16 && !landing; offset++) {
                    int64_t anchor = location_make(location_get_x(*destination) + offset, location_get_y(*destination));
                    if (tile_is_blocking(anchor, false)) continue;
                    unsigned exits = 0;
                    for (int direction = 0; direction < 8; direction++) {
                        int64_t to; location_in_dir(anchor, direction, &to);
                        PathCreateInfo walk = { .obj = pc(2), .from = anchor, .to = to, .max_rotations = 256, .rotations = rotations };
                        if (path_make(&walk) > 0) exits++;
                    }
                    if (exits >= 4) { *destination = anchor; landing = true; }
                }
                if (!landing) continue;
            }
            fprintf(stderr, "ARCMP story route phase=%u steps=%d to=%lld,%lld\n", phase, steps, (long long)location_get_x(*destination), (long long)location_get_y(*destination));
            return true;
        }
    }
    return false;
}
static bool announce(unsigned phase)
{
    char text[80];
    if (phase == 1 || phase == 7) for (unsigned slot = 0; slot < 4; slot++) {
        snprintf(text, sizeof(text), "cinema-bind:%u:%d", slot, coop_campaign_slot_for_owner(coop_session_players(held)[slot].identity));
        if (!coop_session_chat(held, text)) return false;
    }
    if (phase >= 7) {
        int64_t destination;
        if (!route(phase, &destination)) return false;
        snprintf(text, sizeof(text), "cinema-route:%lld:%lld", (long long)location_get_x(destination), (long long)location_get_y(destination));
        if (!coop_session_chat(held, text)) return false;
    }
    snprintf(text, sizeof(text), "cinema-phase:%u", phase); return coop_session_chat(held, text);
}
void coop_story_probe_cleanup(void)
{
    coop_runtime_present_observer(NULL, NULL);
    if (held) { coop_runtime_detach(); coop_session_destroy(held); held = NULL; }
}
bool coop_story_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false, tail = false, journey = false, ending = false, defeat = false; unsigned port = 35743, player = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-story-tail")) tail = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-journey")) journey = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-ending")) ending = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-defeat")) ending = defeat = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= 4 || host != (player == 0)) return false;
    CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Story player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    held = coop_session_create(&config); if (!held) return false;
    uint64_t start = SDL_GetTicks(), playing = 0, at = 0, chats = 0, ended = 0;
    bool good = (host ? coop_session_host(held, (uint16_t)port, start) : coop_session_join(held, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(held, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    StoryObserver o = { .host = host, .framebuffer = framebuffer, .good = true, .ending = ending };
    coop_runtime_present_observer(observer, &o);
    bool selected = false, initialized = false, sent = false, done = false, voted = false, cancelled = false, finishing = false;
    unsigned phase = 0, completed = 0, ack[4] = { 0 }, epoch = 0, hours = 0;
    int64_t origin = 0, destination = 0; uint64_t transport_time = 0;
    while (good && SDL_GetTicks() - start < 180000) {
        uint64_t now = SDL_GetTicks(); tig_ping(); coop_session_poll(held, now); coop_lobby_tick();
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) { fprintf(stderr, "ARCMP story runtime: %s\n", coop_runtime_status()); good = false; break; }
        if (ending && o.terminal && coop_runtime_phase() == COOP_RUNTIME_OFF) {
            /* The native menu creates its own preview character. No campaign
             * owner may remain bound, but that internal actor is legitimate. */
            good &= !coop_campaign_active() && coop_campaign_slot_for_object(player_get_local_pc_obj()) < 0 && o.terminal_id == (unsigned)defeat;
            good &= mainmenu_ui_is_active() && mainmenu_ui_window_type_get() == MM_WINDOW_MAINMENU;
            tig_window_display();
            if (framebuffer) good &= tig_video_screenshot_save("arcmp-probe.bmp") == TIG_OK;
            break;
        }
        CoopGameplay* game = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(held) == COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name, sizeof(name), "Story hero %u", player); selected = coop_session_character(held, 16067 + player, name); }
            else if (coop_session_players(held)[coop_session_local_slot(held)].prototype && !coop_session_players(held)[coop_session_local_slot(held)].ready) coop_session_ready(held, true);
            unsigned count = 0; for (unsigned i = 0; i < 4; i++) count += coop_session_players(held)[i].connected;
            if (host && count == 4 && coop_session_all_ready(held)) good = tail ? coop_runtime_recover_checkpoint() : coop_runtime_start();
        }
        if (game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing) playing = now;
            const CoopDialogueView* d = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!phase && d->active && d->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (!initialized && host && !d->active && now - playing > 1200) { initialized = true; good = (tail || prepare()) && announce(tail || journey ? 7 : 1); }
        }
        for (; chats < coop_session_chat_count(held); chats++) {
            const CoopChat* chat = coop_session_chat_at(held, chats); unsigned value, connection, member; long long x, y;
            if (!chat->slot && sscanf(chat->text, "cinema-bind:%u:%u", &connection, &member) == 2) {
                if (connection >= 4 || member >= 4) { good = false; break; } bindings[connection] = (int)member;
            }
            if (!chat->slot && sscanf(chat->text, "cinema-route:%lld:%lld", &x, &y) == 2) destination = location_make(x, y);
            if (!chat->slot && sscanf(chat->text, "cinema-phase:%u", &value) == 1) {
                if (value != (!phase && (tail || journey) ? 7 : phase + 1) || value > PHASES) { good = false; break; }
                phase = value; at = now; epoch = coop_runtime_epoch(); sent = done = voted = cancelled = false;
                if (phase == 1 || (!o.live && (tail || journey))) {
                    o.base = coop_present_current(coop_runtime_presentation())->sequence; o.slot = coop_session_local_slot(held); o.live = true;
                    tig_movie_probe_mode(false); origin = obj_field_int64_get(pc(0), OBJ_F_LOCATION);
                }
                fprintf(stderr, "ARCMP cinema phase=%u slot=%u epoch=%u\n", phase, o.slot, epoch);
            }
            if (chat->slot < 4 && sscanf(chat->text, "cinema-done:%u", &value) == 1) ack[chat->slot] = value;
        }
        const CoopControlView* control = coop_controls_current(coop_runtime_controls());
        bool live = game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && control && control->phase == COOP_CONTROL_IDLE;
        if (phase && !done && now - at > 400) {
            bool ready = live;
            int64_t item = tagged(), parent = 0; if (item) item_parent(item, &parent);
            if (phase == 1) ready &= item && script_global_var_get(0) == 53001;
            else if (phase == 2) {
                if (o.slot == 1 && !sent && live) { good = coop_runtime_item(COOP_ITEM_PICKUP, item, 0, -1); sent = true; }
                if (host && !sent && anim_is_current_goal_type(pc(1), AG_PICKUP_ITEM, NULL) && !parent) {
                    sent = true; script_global_var_set(0, script_global_var_get(0) + 1); gmovie_play(2, 0, 0);
                    script_global_var_set(0, script_global_var_get(0) + 1);
                }
                ready &= parent == pc(1) && script_global_var_get(0) == 53003 && o.elapsed[0] >= 8800 && o.was_playing[0];
            } else if (phase == 3 || phase == 4) {
                if (host && !sent && live) {
                    sent = true;
                    if (phase == 3) gmovie_play(8, 0, 0); else coop_runtime_present(COOP_PRESENT_SLIDE, 0, NULL, 0, 0);
                }
                if (phase == 3) {
                    ArcMovieInfo info; tig_movie_progress(&info);
                    ready &= o.was_playing[1] && info.decoded_frames == info.frames && info.decoded_frames > 100 && info.audio_samples > 0;
                    if (ready) o.movies[1] = info;
                } else ready &= o.elapsed[2] >= 900;
            } else if (phase >= 5 && phase <= 7) {
                if (host && !sent && live) {
                    TeleportData data = { .obj = pc(2), .map = phase == 5 ? map_by_type(MAP_TYPE_SHOPPING_MAP) : map_by_type(MAP_TYPE_START_MAP), .flags = TELEPORT_COOP_PARTY };
                    if (phase == 5) {
                        int64_t x, y; good = map_get_starting_location(data.map, &x, &y); data.loc = location_make(x, y);
                        data.flags |= TELEPORT_MOVIE1 | TELEPORT_MOVIE2 | TELEPORT_TIME; data.movie1 = data.movie2 = 8; data.time = 3600;
                        DateTime clock = sub_45A7C0(); transport_time = (uint64_t)clock.days * 86400000 + clock.milliseconds;
                    } else data.loc = phase == 6 ? origin : destination;
                    good = good && teleport_do(&data); sent = true;
                }
                ready &= coop_runtime_epoch() == epoch + 1;
                if (phase == 5) ready &= map_current_map() == map_by_type(MAP_TYPE_SHOPPING_MAP) && o.was_playing[3] && o.was_playing[4];
                else ready &= map_current_map() == map_by_type(MAP_TYPE_START_MAP);
                if (phase == 5 && host && ready) { DateTime clock = sub_45A7C0(); uint64_t current = (uint64_t)clock.days * 86400000 + clock.milliseconds; ready &= current - transport_time >= 3600000 && current - transport_time < 3615000; }
                if (phase == 7 && ready) ready &= townmap_get(sector_id_from_loc(obj_field_int64_get(pc(2), OBJ_F_LOCATION))) == TOWNMAP_NONE;
            } else {
                if (o.slot == 2 && !sent && live) { good = coop_runtime_journey(destination); sent = true; }
                if (control->phase == COOP_CONTROL_VOTING && !voted && now - at > 900) { agree(); voted = true; }
                if (phase == 9 && o.slot == 3 && control->phase == COOP_CONTROL_JOURNEY && control->completed >= 1 && !cancelled) {
                    good = coop_runtime_control(COOP_CONTROL_CANCEL, 0, control->proposal, 0); cancelled = true;
                }
                ready &= coop_runtime_epoch() == epoch + 1 && control->epoch == coop_runtime_epoch() && control->action == COOP_CONTROL_TRAVEL;
                ready &= control->notice == (phase == 8 ? COOP_CONTROL_OK : COOP_CONTROL_INTERRUPTED) && control->completed >= 1;
                if (ready) hours += control->completed;
            }
            if (ready && good && o.good) {
                char text[64]; snprintf(text, sizeof(text), "cinema-done:%u", phase);
                good = coop_session_chat(held, text); done = true; completed = phase;
            }
        }
        if (!o.good) { fprintf(stderr, "ARCMP story clock/goal/frame invariant failed\n"); good = false; }
        if (phase && !done && now - at > 40000) {
            fprintf(stderr, "ARCMP cinema timeout phase=%u sent=%d voted=%d epoch=%u view=%u status=%u hours=%u parent=%lld\n", phase, sent, voted, coop_runtime_epoch(), control ? control->phase : 99, control ? control->notice : 99, control ? control->completed : 0, (long long)(tagged() ? obj_field_handle_get(tagged(), OBJ_F_ITEM_PARENT) : 0)); good = false;
        }
        if (host && done && !finishing && live) {
            bool all = true; for (unsigned i = 0; i < 4; i++) all &= ack[i] == phase;
            if (all) {
                if (ending) {
                    finishing = true;
                    /* A short native movie prelude verifies caption rendering
                     * and presentation-to-ending sequencing in this UI probe. */
                    gmovie_play(8, 0, 0);
                    if (defeat) for (unsigned i = 0; i < 4; i++) critter_kill(coop_campaign_object(i));
                    else coop_runtime_end_campaign(false);
                } else if (phase < PHASES) good = good && announce(phase + 1);
                else { good = good && coop_gameplay_finish(game); finishing = true; }
            }
        }
        if (game && coop_gameplay_finished(game)) { if (!ended) ended = now; if (now - ended > 600) break; }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage message; while (tig_message_dequeue(&message) == TIG_OK) { if (message.type == TIG_MESSAGE_QUIT) good = false; else if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&message); }
        SDL_Delay(2);
    }
    CoopGameplay* game = coop_runtime_gameplay();
    if (ending) {
        good &= o.good && o.terminal && coop_runtime_phase() == COOP_RUNTIME_OFF && mainmenu_ui_is_active();
        FILE* result = fopen("arcmp-ending-result.json", "wb");
        if (!result) good = false;
        else {
            fprintf(result, "{\"verified\":%s,\"defeated\":%s,\"shared_slides\":%u,\"terminal_id\":%u,\"all_ready_done\":%s,\"native_menu\":%s,\"party_reset\":%s}\n", good ? "true" : "false", defeat ? "true" : "false", o.ending_slides, o.terminal_id, o.terminal ? "true" : "false", mainmenu_ui_is_active() ? "true" : "false", coop_campaign_active() ? "false" : "true");
            if (fclose(result)) good = false;
        }
        fprintf(stderr, "ARCMP ending probe %s defeated=%d slides=%u\n", good ? "PASS" : "FAIL", defeat, o.ending_slides); return good;
    }
    good &= o.good && game && coop_gameplay_finished(game) && completed == PHASES;
    coop_runtime_present_observer(NULL, NULL);
    FILE* report = fopen("arcmp-cinema-result.json", "wb"); if (!report) good = false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"phases\":%u,\"epoch\":%u,\"presentations\":%u,\"executed_journeys\":%u,\"travel_hours\":%u,\"held_movie_ms\":%llu,\"eof_frames\":%u,\"eof_samples\":%llu,\"clock_and_goal_frozen\":%s}\n", good ? "true" : "false", completed, coop_runtime_epoch(), coop_present_current(coop_runtime_presentation())->sequence - o.base, coop_controls_executed(coop_runtime_controls()), hours, (unsigned long long)o.elapsed[0], o.movies[1].decoded_frames, (unsigned long long)o.movies[1].audio_samples, o.good ? "true" : "false");
        if (fclose(report)) good = false;
    }
    fprintf(stderr, "ARCMP cinema probe %s phases=%u\n", good ? "PASS" : "FAIL", completed); return good;
}
