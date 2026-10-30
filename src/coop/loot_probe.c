#include "coop/runtime_probe.h"
#include "coop/runtime.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/timeevent.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/iso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { CHEST = 790001, LOCKED = 790002, BODY = 790003, MARKER = 791000, HEAVY = 791050, CONTESTED = 791060, CORPSE_ITEM = 791070 };
static bool content(uint8_t id[32])
{
    FILE* file = fopen("arcmp-content.id", "rb"); char hex[66] = { 0 };
    if (!file) return false;
    bool good = fread(hex, 1, 65, file) >= 64; fclose(file);
    for (unsigned i = 0; good && i < 32; i++) {
        char pair[] = { hex[i * 2], hex[i * 2 + 1], 0 }, *end;
        id[i] = (uint8_t)strtoul(pair, &end, 16); good = !*end;
    }
    return good;
}
static void key(SDL_Scancode code)
{
    TigMessage message = { .type = TIG_MESSAGE_KEYBOARD };
    message.data.keyboard.scancode = code; message.data.keyboard.pressed = true; tig_message_enqueue(&message);
}
static void click(int x, int y, TigMessageMouseEvent event)
{
    TigMessage message = { .type = TIG_MESSAGE_MOUSE };
    message.data.mouse.x = x; message.data.mouse.y = y; message.data.mouse.event = event; tig_message_enqueue(&message);
}
static bool world_click(int64_t target)
{
    location_origin_set(obj_field_int64_get(target, OBJ_F_LOCATION));
    TigRect rect; object_get_rect(target, 0, &rect);
    for (int y = rect.y; y < rect.y + rect.height; y += 2) for (int x = rect.x; x < rect.x + rect.width; x += 2) {
        TargetDescriptor picked;
        if (x < 0 || x >= 800 || y < 80 || y >= 430 || !target_pick_at_screen_xy(x, y, &picked, false) || picked.is_loc || picked.obj != target) continue;
        click(x, y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN); click(x, y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP); return true;
    }
    return false;
}
static int64_t tagged(int tag)
{
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        int type = obj_field_int32_get(obj, OBJ_F_TYPE);
        int field = type == OBJ_TYPE_CONTAINER ? OBJ_F_CONTAINER_PAD_I_1 : type == OBJ_TYPE_NPC ? OBJ_F_NPC_PAD_I_1 : item_is_item(obj) ? OBJ_F_ITEM_WORTH : -1;
        if (field >= 0 && obj_field_int32_get(obj, field) == tag) return obj;
    } while (obj_inst_next(&obj, &iterator));
    return OBJ_HANDLE_NULL;
}
static bool setup(void)
{
    int64_t pc = coop_campaign_object(0), origin = obj_field_int64_get(pc, OBJ_F_LOCATION);
    int64_t chest = 0, locked = 0, body = 0;
    int64_t prototype = sub_4685A0(BP_WOOD_CHEST_1);
    if (!prototype || !object_create(prototype, location_make(location_get_x(origin) + 3, location_get_y(origin) + 3), &chest)
        || !object_create(prototype, location_make(location_get_x(origin) - 5, location_get_y(origin) - 1), &locked)) return false;
    obj_field_int32_set(chest, OBJ_F_CONTAINER_PAD_I_1, CHEST);
    obj_field_int32_set(locked, OBJ_F_CONTAINER_PAD_I_1, LOCKED);
    obj_field_int32_set(chest, OBJ_F_CONTAINER_INVENTORY_SOURCE, 0); obj_field_int32_set(locked, OBJ_F_CONTAINER_INVENTORY_SOURCE, 0);
    obj_field_int32_set(chest, OBJ_F_CONTAINER_FLAGS, 0);
    obj_field_int32_set(locked, OBJ_F_CONTAINER_FLAGS, OCOF_LOCKED | OCOF_JAMMED);
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && critter_is_dead(obj)) { body = obj; break; }
    } while (obj_inst_next(&obj, &iterator));
    if (!body) return false;
    obj_field_int32_set(body, OBJ_F_NPC_PAD_I_1, BODY);
    sub_43E770(body, location_make(location_get_x(origin) + 6, location_get_y(origin) + 2), 0, 0);
    int64_t weapon = item_wield_get(pc, ITEM_INV_LOC_WEAPON);
    if (!weapon) return false;
    prototype = obj_field_handle_get(weapon, OBJ_F_PROTOTYPE_HANDLE);
    for (unsigned i = 0; i < 19; i++) {
        int64_t item;
        if (!object_create(prototype, origin, &item)) return false;
        obj_field_int32_set(item, OBJ_F_ITEM_WORTH, i < 16 ? MARKER + (int)i : i == 16 ? HEAVY : i == 17 ? CONTESTED : CORPSE_ITEM);
        if (i == 16) obj_field_int32_set(item, OBJ_F_ITEM_WEIGHT, 1000000);
        if (!item_transfer(item, i == 18 ? body : chest)) return false;
    }
    int64_t gold = item_gold_create(43, origin);
    return gold && item_transfer(gold, chest);
}
static bool inventory_click(int64_t item, bool target, TigMessageMouseEvent event)
{
    int location = item_inventory_location_get(item);
    if (location < 0 || location >= (target ? 80 : 120)) return false;
    click((target ? 8 : 368) + (location % 10) * 32 + 16, (target ? 177 : 49) + (location / 10) * 32 + 16, event);
    return true;
}
static bool markers(bool with_corpse)
{
    unsigned counts[19] = { 0 }; int64_t current; int iterator;
    if (obj_inst_first(&current, &iterator)) do {
        if (!item_is_item(current)) continue;
        int worth = obj_field_int32_get(current, OBJ_F_ITEM_WORTH);
        int index = worth >= MARKER && worth < MARKER + 16 ? worth - MARKER : worth == HEAVY ? 16 : worth == CONTESTED ? 17 : worth == CORPSE_ITEM ? 18 : -1;
        if (index >= 0) counts[index]++;
    } while (obj_inst_next(&current, &iterator));
    for (unsigned i = 0; i < 19; i++) if (counts[i] != 1) return false;
    for (int tag = MARKER; tag < MARKER + 16; tag++) {
        int64_t obj = tagged(tag), parent = 0;
        if (!obj || !item_parent(obj, &parent) || coop_campaign_slot_for_object(parent) < 0) return false;
    }
    int64_t heavy = tagged(HEAVY), parent = 0;
    if (!heavy || !item_parent(heavy, &parent) || parent != tagged(CHEST)) return false;
    int64_t contested = tagged(CONTESTED), corpse = tagged(CORPSE_ITEM);
    if (!contested || !corpse || !item_parent(contested, &parent) || coop_campaign_slot_for_object(parent) < 0) return false;
    if (with_corpse && (!item_parent(corpse, &parent) || coop_campaign_slot_for_object(parent) < 0)) return false;
    int gold = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) gold += item_gold_get(coop_campaign_object(i));
    return gold == 385 * 4 + 43;
}
bool coop_loot_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false, loss = false;
    unsigned port = 35742, player = COOP_MAX_PLAYERS;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-loot-loss")) loss = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= COOP_MAX_PLAYERS || host != (player == 0)) return false;
    CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Loot player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    CoopSession* session = coop_session_create(&config); if (!session) return false;
    uint64_t start = SDL_GetTicks(), playing_at = 0, sent_at = 0, finished_at = 0;
    bool good = (host ? coop_session_host(session, (uint16_t)port, start) : coop_session_join(session, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(session, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    bool selected = false, initialized = false, sent = false, done = false, finishing = false, moved = false, shared_close = false;
    unsigned step = 0, ack[COOP_MAX_PLAYERS] = { 0 }, completed = 0, race_wins = 0, stale_denials = 0;
    uint64_t chats = 0;
    uint32_t expected = 0, old_generation = 0;
    uint64_t step_at = 0;
    ObjectID race_id = { 0 }, corpse_id = { 0 };
    int local = -1, baseline_decay = item_decay_process_is_enabled();
    while (good && SDL_GetTicks() - start < 120000) {
        uint64_t now = SDL_GetTicks(); tig_ping(); coop_session_poll(session, now); coop_runtime_tick(now);
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) {
            if (loss && completed == 1) break;
            fprintf(stderr, "ARCMP loot runtime failed: %s\n", coop_runtime_status()); good = false; break;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name, sizeof(name), "Loot hero %u", player); selected = coop_session_character(session, 16067 + player, name); }
            else if (coop_session_players(session)[coop_session_local_slot(session)].prototype && !coop_session_players(session)[coop_session_local_slot(session)].ready) coop_session_ready(session, true);
            unsigned count = 0; for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) count += coop_session_players(session)[i].connected;
            if (host && count == 4 && coop_session_all_ready(session)) good = coop_runtime_start();
        }
        CoopGameplay* game = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing_at) { playing_at = now; local = coop_campaign_slot_for_owner(config.identity); }
            CoopItems* items = coop_gameplay_items(game);
            const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (view->active && view->chooser == coop_session_local_slot(session)) key(SDL_SCANCODE_ESCAPE);
            if (host && !initialized && now - playing_at > 1500 && !view->active) {
                initialized = setup(); good = good && initialized;
                if (good) good = coop_session_chat(session, "loot-step:1");
            }
            for (; chats < coop_session_chat_count(session); chats++) {
                const CoopChat* chat = coop_session_chat_at(session, chats); if (!chat) continue;
                unsigned value;
                if (!chat->slot && sscanf(chat->text, "loot-step:%u", &value) == 1) {
                    step = value; sent = false; done = false;
                    step_at = now;
                    unsigned status; expected = coop_items_last_result(items, &status) + 1;
                    fprintf(stderr, "ARCMP loot step=%u member=%d generation=%u\n", step, local, coop_items_loot_generation(items));
                }
                if (sscanf(chat->text, "loot-done:%u", &value) == 1 && chat->slot < COOP_MAX_PLAYERS) ack[chat->slot] = value;
            }
            int64_t chest = tagged(CHEST), locked = tagged(LOCKED), body = tagged(BODY), target = coop_items_loot_target(items);
            if (step && chest && locked && body && race_id.type == OID_TYPE_NULL) {
                int64_t race = tagged(CONTESTED), corpse = tagged(CORPSE_ITEM);
                if (race && corpse) { race_id = obj_get_id(race); corpse_id = obj_get_id(corpse); }
            }
            if (host && step == 7 && done && now - step_at < 1200 && !coop_items_loot_target(items))
                shared_close = tig_art_id_frame_get(obj_field_int32_get(chest, OBJ_F_CURRENT_AID)) == 1 && item_decay_process_is_enabled() == baseline_decay - 1;
            if (step && !done && !sent && race_id.type != OID_TYPE_NULL && !coop_items_busy(items)
                && !(step == 7 && !host && now - step_at < 1500)) {
                bool submitted = true;
                if (step == 1) submitted = world_click(chest);
                else if (step == 13 || step == 15 || step == 17) submitted = coop_runtime_item(COOP_LOOT_OPEN, chest, 0, -1);
                else if (step == 2 || step == 11) submitted = coop_runtime_item(COOP_LOOT_TAKE, obj_pool_perm_lookup(step == 2 ? race_id : corpse_id), 0, -1);
                else if (step == 3) submitted = inventory_click(tagged(MARKER + local), true, TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP);
                else if (step == 4) submitted = inventory_click(tagged(MARKER + local), false, TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP);
                else if (step == 5 || step == 8 || step == 14) {
                    CoopObjectId id; coop_object_id_from_engine(race_id, &id);
                    submitted = coop_items_submit_access(items, map_current_map(), coop_campaign_member((unsigned)local)->object_id.d.g.data,
                        COOP_LOOT_TAKE, id, 0, -1, step == 5 ? coop_items_loot_generation(items) - 1 : old_generation);
                } else if (step == 6) {
                    click(310, 100, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN); click(310, 100, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
                } else if (step == 7 || step == 12 || step == 19) { old_generation = coop_items_loot_generation(items); key(SDL_SCANCODE_ESCAPE); }
                else if (step == 9) submitted = coop_runtime_item(COOP_LOOT_OPEN, locked, 0, -1);
                else if (step == 10) submitted = coop_runtime_item(COOP_LOOT_OPEN, body, 0, -1);
                else if (step == 16) {
                    int64_t pc = player_get_local_pc_obj(), origin = obj_field_int64_get(pc, OBJ_F_LOCATION); int64_t loc;
                    submitted = false;
                    for (int d = 0; d < 8 && !submitted; d++) if (location_in_range(origin, d, 8, &loc)
                        && location_dist(loc, obj_field_int64_get(chest, OBJ_F_LOCATION)) > 6) {
                        int8_t rotations[64]; PathCreateInfo path = { .obj = pc, .from = origin, .to = loc, .rotations = rotations, .max_rotations = 64 };
                        if (path_make(&path) > 0) submitted = coop_runtime_move(loc, false);
                    }
                    old_generation = coop_items_loot_generation(items); moved = submitted;
                } else if (step == 18) {
                    CoopObjectId id = { 0 };
                    submitted = coop_items_submit_access(items, map_current_map(), coop_campaign_member((unsigned)local)->object_id.d.g.data,
                        COOP_LOOT_CLOSE, id, 0, -1, old_generation);
                }
                good = good && submitted; sent = true; sent_at = now;
            }
            unsigned status = 0; uint32_t result = coop_items_last_result(items, &status);
            bool receipt = sent && result == expected && !coop_items_busy(items);
            if (sent && !done && ((step == 16 && !target && now - sent_at > 500) || receipt)) {
                bool correct = true;
                if (step == 2 || step == 11) { correct = status == 0 || status == COOP_ITEM_OWNERSHIP; if (!status) race_wins++; }
                else if (step == 5 || step == 8 || step == 14) { correct = status == COOP_LOOT_DENIED; stale_denials++; }
                else if (step == 6) correct = status == COOP_LOOT_PARTIAL;
                else if (step == 9) correct = status == COOP_LOOT_JAMMED && !target;
                else if (step != 16) correct = status == 0;
                if (step == 1 || step == 13 || step == 15 || step == 17 || step == 18) correct = correct && target == chest;
                if (step == 10) correct = correct && target == body;
                if (step == 7 || step == 12 || step == 19) correct = correct && !target && !inven_ui_is_created();
                if (step == 1 && framebuffer) { gamelib_redraw(); iso_redraw(); tig_window_display(); correct = correct && tig_video_screenshot_save("arcmp-loot-open.bmp") == TIG_OK; }
                fprintf(stderr, "ARCMP loot receipt step=%u status=%u correct=%d generation=%u\n", step, status, correct, coop_items_loot_generation(items));
                good = good && correct;
                if (good) { char text[32]; snprintf(text, sizeof(text), "loot-done:%u", step); good = coop_session_chat(session, text); done = true; completed++; }
            }
            if (sent && !done && now - sent_at > 18000) { fprintf(stderr, "ARCMP loot stuck step=%u expected=%u result=%u status=%u target=%d\n", step, expected, result, status, target != 0); good = false; }
            if (host && step && done) {
                bool all = true; for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) all = all && ack[i] == step;
                if (all) {
                    if (loss && step == 1) { coop_session_leave(session); continue; }
                    if (step == 6) good = good && markers(false);
                    if (step == 13) obj_field_int32_set(chest, OBJ_F_CONTAINER_FLAGS, OCOF_JAMMED);
                    if (step == 14) obj_field_int32_set(chest, OBJ_F_CONTAINER_FLAGS, 0);
                    if (step < 19) { char text[32]; snprintf(text, sizeof(text), "loot-step:%u", step + 1); good = coop_session_chat(session, text); done = false; }
                    else if (!finishing && coop_items_settled(items)) finishing = coop_gameplay_finish(game);
                }
            }
            if (coop_gameplay_finished(game)) {
                if (!finished_at) finished_at = now;
                if (now - finished_at > 600) break;
            }
        }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage message; while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT) good = false;
            else if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&message);
        }
        SDL_Delay(2);
    }
    CoopGameplay* game = coop_runtime_gameplay();
    bool frozen = false;
    if (loss) {
        DateTime before = sub_45A7C0(); SDL_Delay(20);
        tig_timestamp_t timestamp; tig_timer_now(&timestamp); timeevent_ping(timestamp); gamelib_ping();
        DateTime after = sub_45A7C0();
        frozen = coop_runtime_phase() == COOP_RUNTIME_FAILED && datetime_compare(&before, &after) == 0;
        good = good && frozen && completed == 1 && game && !coop_gameplay_finishing(game);
    } else good = good && game && coop_gameplay_finished(game) && completed == 19 && stale_denials == 3 && moved && markers(true) && (!host || shared_close);
    coop_runtime_detach();
    good = good && item_decay_process_is_enabled() == baseline_decay;
    FILE* file = fopen("arcmp-loot-result.json", "wb");
    if (file) {
        fprintf(file, "{\"verified\":%s,\"steps\":%u,\"local_member\":%d,\"race_wins\":%u,\"stale_denials\":%u,\"moved_and_revoked\":%s,\"decay_balanced\":%s,\"one_viewer_close_keeps_shared_container_open\":%s,\"loss_with_open_loot\":%s,\"disconnect_freeze\":%s}\n",
            good ? "true" : "false", completed, local, race_wins, stale_denials, moved ? "true" : "false", item_decay_process_is_enabled() == baseline_decay ? "true" : "false", shared_close ? "true" : "false", loss ? "true" : "false", frozen ? "true" : "false");
        if (fclose(file)) good = false;
    } else good = false;
    coop_session_destroy(session);
    fprintf(stderr, "ARCMP runtime loot probe %s\n", good ? "PASS" : "FAIL");
    return good;
}
