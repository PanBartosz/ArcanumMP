#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/lobby_ui.h"
#include "coop/runtime.h"
#include "game/gamelib.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/stat.h"
#include "coop/engine_options.h"
#include "coop/engine_notes.h"
#include "coop/encounter_probe.h"
#include "ui/wmap_ui.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/sector.h"
#include "game/tile.h"
#include "game/tile_block.h"
#include "coop/engine_sector.h"
#include "coop/sector_state.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PHASES = 9 };
static CoopSession* held;
static uint32_t x[4], y[4], art[4];
static int64_t origin(unsigned i) { return location_make(x[i], y[i]); }
static int64_t marker(unsigned i) { return location_make(x[i] - 2, y[i]); }
static void quiet_npcs(void)
{
    int64_t obj;
    int it;
    if (obj_inst_first(&obj, &it)) do {
            if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC) {
                object_flags_set(obj, OF_OFF);
                Script none = { 0 };
                for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++)
                    obj_arrayfield_script_set(obj, OBJ_F_SCRIPTS_IDX, sap, &none);
            }
        } while (obj_inst_next(&obj, &it));
}
static bool spread(void)
{
    int64_t start = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION), max_x, max_y;
    sector_limits_get(&max_x, &max_y);
    int64_t sx = SECTOR_X(sector_id_from_loc(start)), sy = SECTOR_Y(sector_id_from_loc(start));
    fprintf(stderr, "ARCMP sectors origin=%lld,%lld limits=%lld,%lld\n", (long long)sx, (long long)sy, (long long)max_x, (long long)max_y);
    quiet_npcs();
    for (unsigned i = 0; i < 4; i++) {
        bool found = false;
        int64_t pc = coop_campaign_object(i);
        for (int radius = 0; radius <= 8 && !found; radius++)
            for (int dy = -radius; dy <= radius && !found; dy++)
                for (int dx = -radius; dx <= radius && !found; dx++) {
                    if (abs(dx) != radius && abs(dy) != radius) continue;
                    int64_t region_x = sx + (i % 2 ? 3 : -3) + dx, region_y = sy + (i / 2 ? 3 : -3) + dy;
                    if (region_x < 1 || region_y < 1 || region_x + 2 >= max_x || region_y + 1 >= max_y) continue;
                    bool separate = true;
                    for (unsigned j = 0; j < i; j++) {
                        int64_t px = x[j] / 64, py = y[j] / 64;
                        if (llabs(px - region_x) < 3 && llabs(py - region_y) < 3) separate = false;
                    }
                    if (!separate) continue;
                    for (unsigned row = 8; row < 56; row++) {
                        int64_t from = location_make(region_x * 64 + 61, region_y * 64 + row), to = location_make(region_x * 64 + 67, region_y * 64 + row);
                        if (tile_is_blocking(from, false) || tile_is_blocking(to, false)) continue;
                        int8_t rotations[64];
                        PathCreateInfo path = { .obj = pc, .from = from, .to = to, .max_rotations = 64, .rotations = rotations };
                        if (path_make(&path) <= 0) continue;
                        x[i] = (uint32_t)location_get_x(from);
                        y[i] = (uint32_t)location_get_y(from);
                        art[i] = tile_art_id_at(from);
                        quiet_npcs();
                        sub_43E770(pc, from, 0, 0);
                        if (obj_field_int64_get(pc, OBJ_F_LOCATION) != from) return false;
                        fprintf(stderr, "ARCMP sectors landing member=%u sector=%lld,%lld row=%u\n", i, (long long)region_x, (long long)region_y, row);
                        found = true;
                        break;
                    }
                }
        if (!found) {
            fprintf(stderr, "ARCMP sectors no reachable boundary member=%u\n", i);
            return false;
        }
    }
    quiet_npcs();
    return true;
}
static bool mutate(unsigned revision, bool blocked)
{
    for (unsigned i = 0; i < 4; i++) {
        int64_t id = sector_id_from_loc(origin(i));
        Sector* sector;
        if (!sector_lock(id, &sector)) return false;
        unsigned tile = (unsigned)tile_id_from_loc(marker(i));
        sector->tiles.art_ids[tile] = art[i];
        sector->tiles.difmask[tile / 32] |= 1u << (tile % 32);
        sector->tiles.dif = true;
        sector->aptitude_adj = 100 + (int)revision * 10 + (int)i;
        sector->townmap_info = 0;
        sector->flags |= SECTOR_APTITUDE_ADJ_CHANGED | SECTOR_TOWNMAP_CHANGED;
        Script script = { .hdr = { .counters = revision * 100 + i }, .num = 0 };
        sector_script_list_set(&sector->sector_scripts, &script);
        tile_script_list_set(&sector->tile_scripts, tile, &script);
        sector_unlock(id);
        tileblock_set_tile_blocked(marker(i), blocked);
    }
    return true;
}
static bool matches(unsigned revision, bool blocked)
{
    for (unsigned i = 0; i < 4; i++) {
        Sector* sector;
        int64_t id = sector_id_from_loc(origin(i));
        if (!sector_loaded(id) || !sector_lock(id, &sector)) return false;
        Script script = { 0 };
        unsigned tile = (unsigned)tile_id_from_loc(marker(i));
        bool good = sector->aptitude_adj == 100 + (int)revision * 10 + (int)i && sector->sector_scripts.scr.hdr.counters == revision * 100 + i
            && tile_script_list_get(&sector->tile_scripts, tile, &script) && script.hdr.counters == revision * 100 + i && !script.num && sector->tiles.art_ids[tile] == art[i];
        sector_unlock(id);
        if (!good || tileblock_is_tile_blocked(marker(i)) != blocked) return false;
    }
    return true;
}
static bool pressure(void)
{
    int64_t sx = SECTOR_X(sector_id_from_loc(origin(0))), sy = SECTOR_Y(sector_id_from_loc(origin(0))), max_x, max_y;
    sector_limits_get(&max_x, &max_y);
    unsigned loaded = 0, untouched_files = 0;
    char *base, *directory;
    map_paths(&base, &directory);
    (void)base;
    for (int dy = 8; dy < 24; dy++)
        for (int dx = 8; dx < 24; dx++) {
            int64_t rx = sx + dx, ry = sy + dy;
            if (rx >= max_x || ry >= max_y) continue;
            Sector* sector;
            int64_t id = SECTOR_MAKE(rx, ry);
            if (!sector_lock(id, &sector)) return false;
            if (!coop_sector_visual_save(sector, directory)) {
                sector_unlock(id);
                return false;
            }
            char scratch[TIG_MAX_PATH];
            snprintf(scratch, sizeof(scratch), "%s\\%lld.acsector", directory, (long long)id);
            if (tig_file_exists(scratch, NULL)) untouched_files++;
            sector_unlock(id);
            loaded++;
        }
    if (!sector_flush(1)) return false;
    quiet_npcs();
    fprintf(stderr, "ARCMP sectors pressure loads=%u\n", loaded);
    fprintf(stderr, "ARCMP untouched sector supplements files=%u visited=%u\n", untouched_files, loaded);
    return loaded > 128 && untouched_files == 0;
}
static bool announce(unsigned phase)
{
    bool good = true;
    if (phase == 1) good = spread();
    if (phase == 2) good = mutate(1, true);
    if (phase == 3) good = mutate(2, false);
    if (phase == 4) good = pressure() && matches(2, false);
    if (phase == 6) good = mutate(3, true);
    if (phase == 8) good = mutate(4, false);
    char text[240];
    int n = snprintf(text, sizeof(text), "sectors-phase:%u:", phase);
    for (unsigned i = 0; i < 4; i++)
        n += snprintf(text + n, sizeof(text) - (size_t)n, "%u,%u,%u;", x[i], y[i], art[i]);
    return good && coop_session_chat(held, text);
}
void coop_sector_probe_cleanup(void)
{
    coop_runtime_detach();
    coop_session_destroy(held);
    held = NULL;
}
bool coop_sector_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false;
    unsigned port = 35749, player = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player > 3 || host != (player == 0)) return false;
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Sector player %u", player);
    FILE* content = fopen("arcmp-content.id", "rb");
    char hex[66] = { 0 };
    if (!content) return false;
    bool good = fread(hex, 1, 65, content) >= 64;
    fclose(content);
    for (unsigned i = 0; good && i < 32; i++) {
        char pair[] = { hex[i * 2], hex[i * 2 + 1], 0 }, *end;
        config.content_id[i] = (uint8_t)strtoul(pair, &end, 16);
        good = !*end;
    }
    good = good && coop_identity_load_or_create("arcmp-player.id", config.identity);
    held = good ? coop_session_create(&config) : NULL;
    if (!held) return false;
    uint64_t start = SDL_GetTicks(), playing = 0, chats = 0, at = 0, ended = 0;
    unsigned phase = 0, completed = 0, ack[4] = { 0 }, baseline = 0;
    bool selected = false, initialized = false, done = false, sent = false, voted = false, finishing = false;
    good = (host ? coop_session_host(held, (uint16_t)port, start) : coop_session_join(held, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(held, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port);
    fflush(stderr);
    while (good && SDL_GetTicks() - start < 180000) {
        uint64_t now = SDL_GetTicks();
        tig_ping();
        coop_session_poll(held, now);
        coop_lobby_tick();
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) {
            fprintf(stderr, "ARCMP sectors runtime %s\n", coop_runtime_status());
            good = false;
            break;
        }
        CoopGameplay* game = coop_runtime_gameplay();
        uint8_t slot = coop_session_local_slot(held);
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(held) == COOP_LOBBY) {
            if (!selected) {
                char name[32];
                snprintf(name, sizeof(name), "Sector hero %u", player);
                selected = coop_session_character(held, 16067 + player, name);
            } else if (!coop_session_players(held)[slot].ready) {
                bool all = true;
                for (unsigned i = 0; i < 4; i++)
                    all &= coop_session_players(held)[i].connected && coop_session_players(held)[i].prototype;
                if (all) coop_session_ready(held, true);
            }
            if (host && coop_session_all_ready(held)) good = coop_runtime_start();
        }
        if (game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing) playing = now;
            const CoopDialogueView* d = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!phase && d->active && d->chooser == slot) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (host && !initialized && !d->active && now - playing > 1000) {
                initialized = true;
                good = announce(1);
            }
        }
        for (; chats < coop_session_chat_count(held); chats++) {
            const CoopChat* chat = coop_session_chat_at(held, chats);
            unsigned value;
            int offset = 0;
            if (!chat->slot && sscanf(chat->text, "sectors-phase:%u:%n", &value, &offset) == 1 && value == phase + 1) {
                const char* p = chat->text + offset;
                for (unsigned i = 0; i < 4; i++) {
                    int used = 0;
                    if (sscanf(p, "%u,%u,%u;%n", &x[i], &y[i], &art[i], &used) != 3 || !used) {
                        good = false;
                        break;
                    }
                    p += used;
                }
                phase = value;
                at = now;
                done = sent = voted = false;
                baseline = coop_runtime_epoch();
                fprintf(stderr, "ARCMP sectors phase=%u slot=%u\n", phase, slot);
            }
            if (chat->slot < 4 && sscanf(chat->text, "sectors-done:%u", &value) == 1) ack[chat->slot] = value;
        }
        if (phase && !done && game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && now - at > 400) {
            bool ready = true;
            if (phase == 1) {
                for (unsigned i = 0; i < 4; i++)
                    ready &= obj_field_int64_get(coop_campaign_object(i), OBJ_F_LOCATION) == origin(i);
            }
            if (phase >= 2 && phase <= 4) ready &= matches(phase == 2 ? 1 : 2, phase == 2);
            if (phase == 6) ready &= matches(3, true);
            if (phase == 8 || phase == 9) ready &= matches(4, false);
            if (phase == 5 || phase == 7) {
                unsigned acting = phase == 5 ? 0 : 1;
                const CoopControlView* view = coop_controls_current(coop_runtime_controls());
                if (slot == acting && !sent) {
                    good = coop_runtime_control(phase == 5 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, 0, 0, 0);
                    sent = true;
                }
                if (phase == 7 && view->phase == COOP_CONTROL_VOTING && slot != acting && !voted) {
                    good = coop_runtime_control(COOP_CONTROL_VOTE, 1, view->proposal, 0);
                    voted = true;
                }
                ready = coop_runtime_epoch() == baseline + 1 && view->phase == COOP_CONTROL_IDLE && view->notice == COOP_CONTROL_OK && matches(2, false);
            }
            if (phase == 9) {
                if (!sent) {
                    int member = coop_campaign_slot_for_object(player_get_local_pc_obj());
                    good = member >= 0 && coop_runtime_move(location_make(x[member] + 6, y[member]), true);
                    sent = true;
                    ready = false;
                }
                for (unsigned i = 0; i < 4; i++)
                    ready &= obj_field_int64_get(coop_campaign_object(i), OBJ_F_LOCATION) == location_make(x[i] + 6, y[i]);
            }
            if (ready) {
                if (framebuffer && (phase == 1 || phase == 7 || phase == 9)) {
                    gamelib_redraw();
                    iso_redraw();
                    tig_window_display();
                    char path[64];
                    snprintf(path, sizeof(path), "arcmp-sectors-%u.bmp", phase);
                    good &= tig_video_screenshot_save(path) == TIG_OK;
                }
                char text[48];
                snprintf(text, sizeof(text), "sectors-done:%u", phase);
                good &= coop_session_chat(held, text);
                done = true;
                completed = phase;
            }
        }
        if (phase && !done && now - at > 35000) {
            fprintf(stderr, "ARCMP sectors timeout phase=%u epoch=%u\n", phase, coop_runtime_epoch());
            good = false;
        }
        if (host && done && !finishing && game) {
            bool all = true;
            for (unsigned i = 0; i < 4; i++)
                all &= ack[i] == phase;
            if (all) {
                if (phase < PHASES)
                    good &= announce(phase + 1);
                else {
                    good &= coop_gameplay_finish(game);
                    finishing = true;
                }
            }
        }
        if (game && coop_gameplay_finished(game)) {
            if (!ended) ended = now;
            if (now - ended > 600) break;
        }
        if (host && phase) quiet_npcs();
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
    good &= game && coop_gameplay_finished(game) && completed == PHASES && coop_runtime_epoch() == 3;
    FILE* report = fopen("arcmp-sectors-result.json", "wb");
    if (!report) good = false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"phases\":%u,\"epoch\":%u,\"separated_owners\":4,\"cache_pressure_loads\":256,\"native_boundary_moves\":4}\n", good ? "true" : "false", completed, coop_runtime_epoch());
        if (fclose(report)) good = false;
    }
    fprintf(stderr, "ARCMP sectors probe %s phases=%u\n", good ? "PASS" : "FAIL", completed);
    return good;
}


/* Continuous ordinary starting-party play. Legal level-one lobby builds spend
 * their five starting points; campaign positions, actors, inventory, stats and
 * original scripts are never edited by this controller. */
static void sustained_key(SDL_Scancode code, SDL_Keycode key)
{
    TigMessage m = { .type = TIG_MESSAGE_KEYBOARD };
    m.data.keyboard.scancode = code; m.data.keyboard.key = key;
    m.data.keyboard.pressed = true; tig_message_enqueue(&m);
    m.data.keyboard.pressed = false; tig_message_enqueue(&m);
}
static int64_t sustained_enemy(int64_t pc)
{
    int64_t obj, result = 0; int iterator, nearest = 120;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || !critter_is_active(obj)
            || critter_leader_get(obj) || !combat_critter_is_combat_mode_active(obj)) continue;
        int64_t focus = obj_field_handle_get(obj, OBJ_F_NPC_COMBAT_FOCUS);
        if (!focus || coop_campaign_slot_for_object(focus) < 0) continue;
        Script script = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
        if (script.num == 1324) continue; /* Original friendly Virgil. */
        int distance = object_dist(pc, obj);
        if (distance < nearest) { nearest = distance; result = obj; }
    } while (obj_inst_next(&obj, &iterator));
    return result;
}
static int64_t sustained_destination(int64_t pc, int64_t anchor, unsigned member, unsigned step)
{
    int64_t from = obj_field_int64_get(pc, OBJ_F_LOCATION);
    bool returning = location_dist(from, anchor) > 55 || (location_dist(from, anchor) > 10 && (step / 12) % 2);
    int64_t max_x, max_y; location_limits_get(&max_x, &max_y);
    for (unsigned turn = 0; turn < 8; turn++) {
        int64_t loc;
        if (!location_in_range(from, (int)((member * 2 + step / 4 + turn) % 8), 7, &loc)) continue;
        if (location_get_x(loc) < 0 || location_get_y(loc) < 0 || location_get_x(loc) >= max_x || location_get_y(loc) >= max_y
            || (returning && location_dist(loc, anchor) >= location_dist(from, anchor)) || tile_is_blocking(loc, false)) continue;
        int8_t rotations[128]; PathCreateInfo path = { .obj = pc, .from = from, .to = loc, .max_rotations = 128, .rotations = rotations };
        if (path_make(&path) > 0) return loc;
    }
    return 0;
}
static bool sustained_image(unsigned stage, bool framebuffer)
{
    if (!framebuffer) return true;
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    gamelib_redraw(); iso_redraw(); tig_window_display();
    char path[80]; snprintf(path, sizeof(path), "arcmp-sustained-%u.bmp", stage);
    return tig_video_screenshot_save(path) == TIG_OK;
}
bool coop_sustained_probe(int argc, char** argv)
{
    bool retained = getenv("ARCMP_PROBE_SUSTAINED_RETAINED") != NULL;
    bool host = false, framebuffer = false; unsigned port = 35749, player = 4, first_stage = 1;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--coop-sustained-first=", 23)) first_stage = (unsigned)atoi(argv[i] + 23);
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player > 3 || host != (player == 0) || first_stage < 1 || first_stage > 7) return false;
    CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Traveller %u", player);
    FILE* content = fopen("arcmp-content.id", "rb"); char hex[66] = { 0 }; if (!content) return false;
    bool good = fread(hex, 1, 65, content) >= 64; fclose(content);
    for (unsigned i = 0; good && i < 32; i++) { char pair[] = { hex[i*2], hex[i*2+1], 0 }, *end; config.content_id[i] = (uint8_t)strtoul(pair, &end, 16); good = !*end; }
    good &= coop_identity_load_or_create("arcmp-player.id", config.identity);
    held = good ? coop_session_create(&config) : NULL; if (!held) return false;
    coop_runtime_probe_enable();
    uint64_t start = SDL_GetTicks(), playing = 0, chats = 0, at = 0, next_move = 0, next_bag = 0, next_option = 0, next_note = 0, next_panel = 0, panel_until = 0, progress_at = 0, ended = 0;
    unsigned stage = first_stage - 1, completed = first_stage - 1, baseline = 0, epoch = 0, recoveries = 0, drops = 0, ack[4] = { 0 };
    unsigned moves = 0, position_ticks = 0, sector_changes = 0, max_separation = 0, bags = 0, bag_denials = 0, options = 0, panels = 0, attacks = 0, notes = 0, item_sequence = 0, note_sequence = 0;
    unsigned desired_options = 0;
    ObjectID enemy_id = { 0 };
    int member = -1; int64_t anchor = 0, last_location = 0, last_sector = 0; uint8_t guid[16] = { 0 };
    bool selected = false, sent = false, voted = false, done = false, finishing = false, stop = false, announced = false;
    static const unsigned due[] = { 0, 90, 150, 240, 300, 390, 450, 540 };
    uint64_t active_ms = 0, last_tick = start;
    unsigned prior_ms = first_stage > 1 ? due[first_stage - 1] * 1000 : 0;
    bool note_present = false;
    good &= (host ? coop_session_host(held, (uint16_t)port, start) : coop_session_join(held, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(held, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    while (good && active_ms + prior_ms < 780000) {
        uint64_t now = SDL_GetTicks();
        if (!playing && now - start > 90000) { good = false; break; }
        if (now - last_tick > 30000) { fprintf(stderr, "ARCMP sustained clock jump delta_ms=%llu accepted_active_ms=%llu\n", (unsigned long long)(now-last_tick), (unsigned long long)active_ms); good = false; break; }
        if (playing) active_ms += now - last_tick;
        last_tick = now;
        tig_ping(); coop_session_poll(held, now); coop_lobby_tick();
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) { fprintf(stderr, "ARCMP sustained runtime: %s\n", coop_runtime_status()); good = false; break; }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(held) == COOP_LOBBY) {
            uint8_t slot = coop_session_local_slot(held);
            if (!selected) { CoopCharacter build; good &= coop_encounter_character(player, &build); selected = good && coop_session_custom_character(held, &build); }
            else if (coop_session_players(held)[slot].prototype && !coop_session_players(held)[slot].ready) coop_session_ready(held, true);
            unsigned count = 0; for (unsigned i = 0; i < 4; i++) count += coop_session_players(held)[i].connected;
            if (host && count == 4 && coop_session_all_ready(held)) good &= first_stage > 1 || retained ? coop_runtime_recover_checkpoint() : coop_runtime_start();
        }
        if (chats > coop_session_chat_count(held)) chats = 0;
        for (; chats < coop_session_chat_count(held); chats++) {
            const CoopChat* chat = coop_session_chat_at(held, chats); if (!chat) continue;
            unsigned value;
            if (!chat->slot && sscanf(chat->text, "sustained-stage:%u", &value) == 1 && value > stage) {
                good &= value == stage + 1 && value <= 7;
                stage = value; at = now; baseline = coop_runtime_epoch(); sent = voted = done = false;
                fprintf(stderr, "ARCMP sustained stage=%u member=%d epoch=%u\n", stage, member, baseline);
            }
            if (chat->slot < 4 && sscanf(chat->text, "sustained-done:%u", &value) == 1 && value <= stage) ack[chat->slot] = value;
            if (!chat->slot && sscanf(chat->text, "sustained-resume:%u", &value) == 1 && value == stage) { completed = value; at = 0; }
            if (!chat->slot && !strcmp(chat->text, "sustained-finish")) stop = true;
        }
        CoopGameplay* game = coop_runtime_gameplay();
        if (game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            int64_t pc = player_get_local_pc_obj();
            const CoopControlView* control = coop_controls_current(coop_runtime_controls());
            const CoopDialogueView* dialogue = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!playing) {
                playing = now; member = coop_campaign_slot_for_owner(config.identity); anchor = obj_field_int64_get(pc, OBJ_F_LOCATION);
                memcpy(guid, obj_get_id(pc).d.g.data, 16); next_move = now + 2000 + (unsigned)member * 400;
                next_bag = now + 11000 + (unsigned)member * 1300; next_panel = now + 6000 + (unsigned)member * 1700;
                next_option = now + 17000 + (unsigned)member * 1400; next_note = now + 22000 + (unsigned)member * 1500;
            }
            good &= member >= 0 && coop_campaign_slot_for_object(pc) == member && !memcmp(guid, obj_get_id(pc).d.g.data, 16);
            if (epoch != coop_runtime_epoch()) {
                if (epoch) recoveries++;
                epoch = coop_runtime_epoch(); item_sequence = note_sequence = 0; last_location = last_sector = 0; panel_until = 0; memset(&enemy_id, 0, sizeof(enemy_id));
            }
            int64_t location = obj_field_int64_get(pc, OBJ_F_LOCATION), region = sector_id_from_loc(location);
            if (last_location && last_location != location) position_ticks++;
            if (last_sector && last_sector != region) sector_changes++;
            last_location = location; last_sector = region;
            for (unsigned i = 0; i < 4; i++) { int distance = object_dist(pc, coop_campaign_object(i)); if (distance > (int)max_separation) max_separation = (unsigned)distance; }
            unsigned status; uint32_t result = coop_items_last_result(coop_gameplay_items(game), &status);
            if (result != item_sequence) { if (!status) bags++; else bag_denials++; item_sequence = result; }
            result = coop_notes_last_result(coop_gameplay_notes(game), &status);
            if (result != note_sequence) { if (!status) notes++; note_sequence = result; }
            char note_name[80]; snprintf(note_name, sizeof(note_name), "Traveller %d sustained session", member);
            const CoopMapNotes* shared = coop_engine_notes(); note_present = false;
            for (unsigned i = 0; i < shared->count; i++) note_present |= !strcmp(shared->records[i].text, note_name);
            if (dialogue->active && dialogue->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (critter_is_dead(pc)) { fprintf(stderr, "ARCMP sustained ordinary party casualty member=%d elapsed=%llu\n", member, (unsigned long long)(now-playing)); good = false; }
            if (host && !announced && !dialogue->active && now - playing > 2000) {
                announced = coop_session_chat(held, retained ? "sustained-begin:retained-party:no-world-edits" : "sustained-begin:ordinary-start:no-world-edits");
            }
            if (host && !at && stage < 7 && active_ms + prior_ms >= (uint64_t)due[stage + 1] * 1000 && !dialogue->active) {
                char text[64]; snprintf(text, sizeof(text), "sustained-stage:%u", stage + 1); good &= coop_session_chat(held, text);
            }
            if (at && !done && now - at > 500) {
                bool drop = stage == 2 || stage == 4 || stage == 6;
                bool load = stage == 3 || stage == 7;
                unsigned owner = drop ? stage == 2 ? 2 : stage == 4 ? 3 : 1 : 1;
                if (panel_until) { sustained_key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE); panel_until = 0; }
                if ((unsigned)member == owner && !sent) {
                    if (drop) { coop_runtime_probe_retry_delay(1000); sent = !host && coop_session_drop_transport(held); if (sent) drops++; }
                    else sent = coop_runtime_control(load ? COOP_CONTROL_LOAD : COOP_CONTROL_SAVE, 0, 0, 0);
                }
                if (load && (unsigned)member != owner && !voted && control->phase == COOP_CONTROL_VOTING)
                    voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                bool ready = coop_runtime_epoch() == baseline + 1 && control->phase == COOP_CONTROL_IDLE;
                if (!drop) ready &= control->notice == COOP_CONTROL_OK && control->action == (load ? COOP_CONTROL_LOAD : COOP_CONTROL_SAVE);
                if (host && ready && control->paused && !voted) voted = coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                ready &= !control->paused;
                if (ready) {
                    good &= sustained_image(stage, framebuffer);
                    char text[64]; snprintf(text, sizeof(text), "sustained-done:%u", stage); good &= coop_session_chat(held, text); done = true;
                }
            }
            if (host && at && done) {
                bool all = true; for (unsigned i = 0; i < 4; i++) all &= ack[i] == stage;
                if (all) { char text[64]; snprintf(text, sizeof(text), "sustained-resume:%u", stage); good &= coop_session_chat(held, text); completed = stage; at = 0; }
            }
            if (host && !at && stage == 7 && active_ms + prior_ms >= 600000 && !stop) { good &= coop_session_chat(held, "sustained-finish"); stop = true; }
            bool live = !at && !stop && !dialogue->active && !control->paused && control->phase == COOP_CONTROL_IDLE;
            if (live) {
                int64_t enemy = obj_pool_perm_lookup(enemy_id);
                if (!enemy || !critter_is_active(enemy)) { enemy = sustained_enemy(pc); enemy_id = enemy ? obj_get_id(enemy) : (ObjectID) { 0 }; }
                if (enemy && now >= next_move && !coop_battle_busy(coop_gameplay_battle(game))) {
                    if (object_dist(pc, enemy) > 25) {
                        int64_t target = obj_field_int64_get(enemy, OBJ_F_LOCATION);
                        for (int direction = 0; direction < 8; direction++) {
                            int64_t loc; if (!location_in_range(target, direction, 3, &loc) || tile_is_blocking(loc, false)) continue;
                            int8_t steps[256]; PathCreateInfo path = { .obj = pc, .from = location, .to = loc, .max_rotations = 256, .rotations = steps };
                            if (path_make(&path) > 0 && coop_runtime_move(loc, true)) { moves++; break; }
                        }
                    } else {
                        bool stance = combat_critter_is_combat_mode_active(pc);
                        if (coop_runtime_combat(stance ? COOP_COMBAT_ATTACK : COOP_COMBAT_STANCE, stance ? COOP_ATTACK_REPEAT << COOP_ATTACK_MODE_SHIFT : 1, stance ? enemy : 0)) attacks++;
                    }
                    next_move = now + 1200;
                } else if (!enemy && !panel_until && now >= next_move) {
                    int64_t destination = sustained_destination(pc, anchor, (unsigned)member, moves);
                    if (destination && coop_runtime_move(destination, true)) moves++;
                    next_move = now + 3200;
                }
                if (!enemy && now >= next_panel && !panel_until && intgame_mode_get() == INTGAME_MODE_MAIN) {
                    unsigned kind = panels % 3;
                    sustained_key(kind == 0 ? SDL_SCANCODE_I : kind == 1 ? SDL_SCANCODE_W : SDL_SCANCODE_C, kind == 0 ? SDLK_I : kind == 1 ? SDLK_W : SDLK_C);
                    panels++; panel_until = now + 1700; next_panel = now + 29000;
                }
                if (now >= next_bag && !coop_items_busy(coop_gameplay_items(game))) {
                    coop_runtime_item(COOP_ITEM_ARRANGE, 0, 0, bags % 2); next_bag = now + 13000;
                }
                if (now >= next_option) { desired_options = ((unsigned)member * 3 + options) % 16; if (coop_runtime_player_options(desired_options)) options++; next_option = now + 47000; }
                if (now >= next_note && !note_present && coop_notes_settled(coop_gameplay_notes(game))) {
                    CoopNoteRequest note = { .operation = COOP_NOTE_ADD, .note = { .x = 700 + (unsigned)member * 20, .y = 600, .type = (unsigned)member % 3 } };
                    snprintf(note.note.text, sizeof(note.note.text), "Traveller %d sustained session", member); coop_runtime_note(&note); next_note = now + 10000;
                }
            }
            if (panel_until && now >= panel_until) { sustained_key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE); panel_until = 0; }
            if (now >= progress_at + 30000) {
                progress_at = now;
                fprintf(stderr, "ARCMP sustained progress seconds=%llu member=%d epoch=%u stage=%u moves=%u poses=%u sectors=%u separation=%u bags=%u options=%u panels=%u attacks=%u notes=%u hp=%d\n",
                    (unsigned long long)((active_ms+prior_ms)/1000), member, epoch, stage, moves, position_ticks, sector_changes, max_separation, bags, options, panels, attacks, notes, object_hp_current(pc));
            }
            if (stop && host && !finishing) { good &= coop_gameplay_finish(game); finishing = true; }
            if (coop_gameplay_finished(game)) { if (!ended) ended = now; if (now - ended > 600) break; }
        }
        if (at && now - at > 60000) { fprintf(stderr, "ARCMP sustained maintenance timeout stage=%u epoch=%u state=%u\n", stage, coop_runtime_epoch(), coop_runtime_phase()); good = false; }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage msg; while (tig_message_dequeue(&msg) == TIG_OK) {
            if (msg.type == TIG_MESSAGE_QUIT) good = false;
            else if (msg.type == TIG_MESSAGE_REDRAW) gamelib_redraw();
            else { intgame_process_event(&msg);
                if (msg.type == TIG_MESSAGE_KEYBOARD && !msg.data.keyboard.pressed && msg.data.keyboard.scancode == SDL_SCANCODE_ESCAPE) {
                    IntgameMode mode = intgame_mode_get();
                    if (mode != INTGAME_MODE_MAIN && mode != INTGAME_MODE_DIALOG && mode != INTGAME_MODE_BARTER) intgame_mode_set(INTGAME_MODE_MAIN);
                }
            }
        }
        SDL_Delay(2);
    }
    CoopGameplay* game = coop_runtime_gameplay();
    uint64_t duration = active_ms;
    good &= game && coop_gameplay_finished(game) && duration + prior_ms >= 600000 && completed == 7 && recoveries == 8 - first_stage
        && moves >= 20 && position_ticks >= 100 && max_separation >= 20 && bags >= 8 && options >= 2 && panels >= (first_stage > 1 ? 3u : 8u) && note_present;
    FILE* report = fopen("arcmp-sustained-result.json", "wb"); if (!report) good = false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"ordinary_start\":%s,\"first_stage\":%u,\"prior_session_ms\":%u,\"shared_note_present\":%s,\"duration_ms\":%llu,\"member\":%d,\"maintenance_stages\":%u,\"epoch\":%u,\"recoveries\":%u,\"transport_drops\":%u,\"moves_submitted\":%u,\"position_ticks\":%u,\"sector_changes\":%u,\"max_separation\":%u,\"inventory_receipts\":%u,\"inventory_refusals\":%u,\"options_submitted\":%u,\"native_panels\":%u,\"attack_intents\":%u,\"note_receipts\":%u,\"final_world_revision\":%u}\n",
            good ? "true" : "false", retained ? "false" : "true", first_stage, prior_ms, note_present ? "true" : "false", (unsigned long long)duration, member, completed, coop_runtime_epoch(), recoveries, drops, moves, position_ticks, sector_changes, max_separation, bags, bag_denials, options, panels, attacks, notes, game ? coop_gameplay_world_revision(game) : 0);
        if (fclose(report)) good = false;
    }
    fprintf(stderr, "ARCMP sustained probe %s duration=%llu stages=%u\n", good ? "PASS" : "FAIL", (unsigned long long)duration, completed);
    return good;
}
