#include "coop/runtime_probe.h"
#include "coop/engine_probe.h"
#include "coop/lobby_ui.h"
#include "coop/starting_shop.h"
#include "coop/engine_character.h"
#include "coop/runtime.h"
#include "coop/campaign.h"
#include "coop/engine_story.h"
#include "coop/engine_object.h"
#include "coop/bootstrap.h"
#include "game/description.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/script.h"
#include "game/target.h"
#include "game/timeevent.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/iso.h"
#include "ui/mainmenu_ui.h"
#include "tig/file.h"
#include "tig/video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static bool recovery_crc(uint32_t* crc)
{
    FILE* file = fopen("arcmp-recovery.acsave", "rb");
    if (!file) return false;
    bool good = !fseek(file, 0, SEEK_END);
    long size = good ? ftell(file) : -1;
    good = good && size >= 196 && size <= COOP_SNAPSHOT_LIMIT + 196 && !fseek(file, 0, SEEK_SET);
    uint8_t* bytes = good ? malloc((size_t)size) : NULL;
    good = good && bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (good) *crc = coop_snapshot_crc(bytes, (size_t)size);
    free(bytes); fclose(file);
    return good;
}
static bool fail_cached_map(void)
{
    char* save; char path[TIG_MAX_PATH];
    map_paths(NULL, &save);
    snprintf(path, sizeof(path), "%s\\Anim.dat", save);
    TigFile* file = tig_file_fopen(path, "wb");
    if (!file) return false;
    int invalid_count = -1;
    bool written = tig_file_fwrite(&invalid_count, sizeof(invalid_count), 1, file) == 1;
    if (tig_file_fclose(file)) written = false;
    return written && !map_open_in_game(map_current_map(), false, true)
        && coop_runtime_phase() == COOP_RUNTIME_FAILED;
}
static void key(SDL_Scancode code)
{
    TigMessage message = { .type = TIG_MESSAGE_KEYBOARD };
    message.data.keyboard.scancode = code;
    message.data.keyboard.pressed = true;
    tig_message_enqueue(&message);
}
static void click(int x, int y, TigMessageMouseEvent event)
{
    TigMessage message = { .type = TIG_MESSAGE_MOUSE };
    message.data.mouse.x = x;
    message.data.mouse.y = y;
    message.data.mouse.event = event;
    tig_message_enqueue(&message);
}
static bool move_click(int64_t pc, int64_t* target)
{
    int64_t origin = obj_field_int64_get(pc, OBJ_F_LOCATION);
    location_origin_set(origin);
    for (int distance = 4; distance <= 7; distance++) for (int direction = 0; direction < 8; direction++) {
        int64_t location, x, y;
        if (!location_in_range(origin, direction, distance, &location)) continue;
        int8_t rotations[64];
        PathCreateInfo path = { .obj = pc, .from = origin, .to = location,
            .max_rotations = SDL_arraysize(rotations), .rotations = rotations };
        if (path_make(&path) <= 0) continue;
        location_xy(location, &x, &y);
        x += 40; y += 20;
        TargetDescriptor picked;
        if (x < 0 || x >= 800 || y < 80 || y >= 430
            || !target_pick_at_screen_xy((int)x, (int)y, &picked, false) || !picked.is_loc || picked.loc != location) continue;
        *target = location;
        click((int)x, (int)y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN);
        click((int)x, (int)y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
        return true;
    }
    return false;
}
static bool npc_click(void)
{
    int64_t object;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
        const char* name = description_get(obj_field_int32_get(object, OBJ_F_DESCRIPTION));
        if (!name || strcmp(name, "Virgil")) continue;
        location_origin_set(obj_field_int64_get(object, OBJ_F_LOCATION));
        TigRect rect;
        object_get_rect(object, 0, &rect);
        for (int y = rect.y; y < rect.y + rect.height; y += 2) for (int x = rect.x; x < rect.x + rect.width; x += 2) {
            TargetDescriptor picked;
            if (x < 0 || x >= 800 || y < 80 || y >= 430 || !target_pick_at_screen_xy(x, y, &picked, false)
                || picked.is_loc || picked.obj != object) continue;
            click(x, y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN);
            click(x, y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
            return true;
        }
    } while (obj_inst_next(&object, &iterator));
    return false;
}
static bool travel_probe(int argc, char** argv);
static bool items_probe(int argc, char** argv);
typedef struct IntroDrop {
    CoopSession* session;
    unsigned player;
    bool dropped;
} IntroDrop;
static void intro_drop(void* context)
{
    IntroDrop* probe = context;
    const CoopPresentView* view = coop_present_current(coop_runtime_presentation());
    if (probe->player == 2 && !probe->dropped && view && view->sequence == 1
        && view->kind == COOP_PRESENT_MOVIE && view->phase == COOP_PRESENT_PREPARING) {
        probe->dropped = coop_session_drop_transport(probe->session);
        fprintf(stderr, "ARCMP original intro transport drop=%d\n", probe->dropped);
    }
}
bool coop_runtime_probe(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) if (strstr(argv[i], "--coop-engine-probe=runtime-loot") == argv[i]) return coop_loot_probe(argc, argv);
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--coop-engine-probe=runtime-items")) return items_probe(argc, argv);
    for (int i = 1; i < argc; i++) if (strstr(argv[i], "--coop-engine-probe=runtime-travel") == argv[i]) return travel_probe(argc, argv);
    bool host = false;
    bool loss = false;
    bool custom = false, framebuffer = false;
    const char* native_role = getenv("ARCMP_PROBE_NATIVE_FAILURE");
    bool native_failure = native_role && (!strcmp(native_role, "host") || !strcmp(native_role, "client"));
    unsigned port = 35742, player = COOP_MAX_PLAYERS;
    const char* address = "127.0.0.1";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
        if (!strncmp(argv[i], "--coop-probe-address=", 21)) address = argv[i] + 21;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-loss")) loss = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-characters") || !strcmp(argv[i], "--coop-engine-probe=runtime-preparation")) custom = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
    }
    if (!port || port > 65535 || player >= COOP_MAX_PLAYERS || host != (player == 0)) return false;
    CoopCharacter draft;
    bool preparation = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-preparation")) preparation = true;
    bool resume_preparation = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--coop-preparation-resume")) resume_preparation = true;
    if (preparation && resume_preparation) {
        FILE* input = fopen("arcmp-prepared-character.bin", "rb");
        uint8_t bytes[COOP_CHARACTER_BYTES];
        bool loaded = input && fread(bytes, 1, sizeof(bytes), input) == sizeof(bytes) && fgetc(input) == EOF;
        if (input && fclose(input)) loaded = false;
        if (!loaded || !coop_character_decode(&draft, bytes, sizeof(bytes)) || !coop_engine_character_validate(&draft)) return false;
    } else if (custom && !(preparation ? coop_lobby_preparation_probe(player, framebuffer, &draft) : coop_character_edit_probe(player, framebuffer, &draft)))
        return false;
    uint32_t starting_hash = preparation ? coop_starting_preview(&draft) : 0;
    if (preparation && !starting_hash) return false;
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Runtime player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    CoopSession* session = coop_session_create(&config);
    if (!session) return false;
    uint64_t start = SDL_GetTicks(), running_at = 0, finished_at = 0, view_at = 0;
    bool good = host ? coop_session_host(session, (uint16_t)port, start) : coop_session_join(session, address, (uint16_t)port, start);
    good = good && coop_runtime_attach(session, config.identity);
    coop_runtime_probe_enable();
    IntroDrop intro = { .session = session, .player = player };
    bool check_intro = getenv("ARCMP_PROBE_INTRO_DROP") != NULL;
    if (check_intro) coop_runtime_present_observer(intro_drop, &intro);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port);
    fflush(stderr);
    bool selected = false, assembled = false, moved = false, halted = false, talked = false, finishing = false;
    bool invalid_draft_sent = false, invalid_start = false, repaired_draft = false;
    bool rejected_targets = false;
    bool view_handled = false, observer_test = false;
    bool fault_injected = false, displaced = false, native_reported = false;
    unsigned fault_ready = 0;
    uint64_t fault_chats = 0;
    uint32_t recovery_before = 0;
    uint32_t revision = 0;
    unsigned choices = 0, cancelled = 0;
    int local = -1;
    int64_t initial = 0, target = 0;
    FILE* transcript = fopen("arcmp-runtime-transcript.bin", "wb");
    if (!transcript) good = false;
    while (good && SDL_GetTicks() - start < 45000) {
        tig_ping();
        coop_session_poll(session, SDL_GetTicks());
        coop_runtime_tick(SDL_GetTicks());
        if (native_failure) for (; fault_chats < coop_session_chat_count(session); fault_chats++) {
            const CoopChat* chat = coop_session_chat_at(session, fault_chats);
            if (chat->slot < 4 && !strcmp(chat->text, "native-fault-ready")) fault_ready |= 1u << chat->slot;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) {
            if (native_failure && coop_session_abort_pending(session)) { SDL_Delay(2); continue; }
            if (loss && revision == 6) break;
            fprintf(stderr, "ARCMP runtime probe failed: %s\n", coop_runtime_status());
            good = false; break;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (!selected) {
                char name[COOP_NAME_SIZE];
                snprintf(name, sizeof(name), "Runtime hero %u", player);
                selected = custom ? coop_session_custom_character(session, &draft) : coop_session_character(session, 16067 + player, name);
            } else if (coop_session_players(session)[coop_session_local_slot(session)].prototype
                && !coop_session_players(session)[coop_session_local_slot(session)].ready) coop_session_ready(session, true);
            unsigned count = 0;
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) count += coop_session_players(session)[i].connected;
            if (host && !assembled && count == 4 && coop_session_all_ready(session)) {
                if (custom && !invalid_draft_sent) {
                    CoopCharacter forged = draft; forged.unspent++;
                    good = coop_session_custom_character(session, &forged);
                    invalid_draft_sent = good;
                } else if (custom && !invalid_start) {
                    invalid_start = coop_runtime_start();
                } else if (custom && !repaired_draft) {
                    good = !coop_campaign_active() && strstr(coop_runtime_status(), "invalid creation choices")
                        && coop_session_custom_character(session, &draft);
                    repaired_draft = good;
                    fprintf(stderr, "ARCMP invalid draft launch refused without disconnect; repaired=%d\n", repaired_draft);
                } else assembled = coop_runtime_start();
            }
        }
        CoopGameplay* gameplay = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!running_at) {
                running_at = SDL_GetTicks();
                local = coop_campaign_slot_for_object(player_get_local_pc_obj());
                initial = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                if (native_failure) good &= recovery_crc(&recovery_before);
                if (custom) {
                    CoopCharacter applied;
                    uint8_t expected[COOP_CHARACTER_BYTES], actual[COOP_CHARACTER_BYTES];
                    good = coop_engine_character_capture(player_get_local_pc_obj(), &applied);
                    applied.trades = draft.trades;
                    memcpy(applied.purchases, draft.purchases, sizeof(applied.purchases));
                    if (preparation) {
                        good &= coop_starting_inventory(player_get_local_pc_obj()) == starting_hash;
                        fprintf(stderr, "ARCMP starting inventory authoritative %s hash=%u\n", good ? "PASS" : "FAIL", starting_hash);
                    }
                    good = good && coop_character_encode(&draft, expected) && coop_character_encode(&applied, actual)
                        && !memcmp(expected, actual, sizeof(actual));
                    FILE* file = good ? fopen("arcmp-character-applied.bin", "wb") : NULL;
                    if (!file) good = false;
                    if (file) { good = fwrite(actual, 1, sizeof(actual), file) == sizeof(actual) && good; if (fclose(file)) good = false; }
                    if (!good) { fprintf(stderr, "ARCMP created character did not survive authoritative launch\n"); break; }
                }
            }
            CoopDialogue* dialogue = coop_gameplay_dialogue(gameplay);
            const CoopDialogueView* view = coop_dialogue_current(dialogue);
            if (view->revision != revision) {
                size_t size = 0;
                uint8_t* bytes = coop_dialogue_view_encode(view, &size);
                if (bytes) {
                    uint8_t length[4]; coop_object_write32(length, (uint32_t)size);
                    good = fwrite(length, 1, 4, transcript) == 4 && fwrite(bytes, 1, size, transcript) == size;
                    free(bytes);
                } else good = false;
                revision = view->revision;
                view_handled = false;
                view_at = SDL_GetTicks();
                fprintf(stderr, "ARCMP runtime dialogue conversation=%u revision=%u active=%d chooser=%u options=%u\n",
                    view->conversation, revision, view->active, view->chooser, view->option_count);
            }
            if (view->active && !view_handled && (view->chooser != coop_session_local_slot(session) || SDL_GetTicks() - view_at > 700)) {
                if (view->chooser == coop_session_local_slot(session)) {
                    if ((view->conversation == 1 && revision == 1) || (view->conversation == 2 && revision == 4)) {
                        key(SDL_SCANCODE_1); choices++;
                    } else { key(SDL_SCANCODE_ESCAPE); cancelled++; }
                } else {
                    key(SDL_SCANCODE_1);
                    observer_test = true;
                }
                view_handled = true;
            }
            if (view->revision >= 3 && !view->active && !moved) {
                moved = move_click(player_get_local_pc_obj(), &target);
                if (!moved) { fprintf(stderr, "ARCMP runtime probe could not select a walkable UI tile\n"); good = false; }
            }
            if (moved && !halted && SDL_GetTicks() - running_at > 6500) {
                int64_t x, y;
                location_xy(target, &x, &y);
                click((int)x + 40, (int)y + 20, TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP);
                halted = true;
            }
            if (local == 1 && halted && !talked && SDL_GetTicks() - running_at > 8500) {
                CoopObjectId target_id;
                good = coop_object_id_from_engine(coop_campaign_member(0)->object_id, &target_id)
                    && coop_gameplay_talk_actor(gameplay, coop_campaign_member(0)->object_id.d.g.data, target_id);
                target_id = (CoopObjectId) { .bytes = { 3 } };
                coop_object_write32(target_id.bytes + 1, UINT32_MAX);
                coop_object_write32(target_id.bytes + 13, (uint32_t)map_current_map());
                good = good && coop_gameplay_talk_actor(gameplay, coop_campaign_member(1)->object_id.d.g.data, target_id);
                rejected_targets = good;
                talked = npc_click();
                if (!talked) { fprintf(stderr, "ARCMP runtime probe could not hit retail NPC\n"); good = false; }
            }
            displaced |= obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION) != initial;
            if (native_failure && !native_reported && view->revision == 6 && !view->active && coop_dialogue_settled(dialogue)) {
                native_reported = coop_engine_probe_report("runtime-loss");
                good &= native_reported && coop_session_chat(session, "native-fault-ready");
            }
            if (!finishing && view->revision == 6 && !view->active && SDL_GetTicks() - running_at > 12000
                && coop_dialogue_settled(dialogue)) {
                if (loss) {
                    if (native_failure) {
                        bool inject = !strcmp(native_role, "host") ? host : player == 3;
                        if (inject && fault_ready == 15 && !fault_injected) { fault_injected = true; good &= fail_cached_map(); }
                    } else if (host) { coop_session_leave(session); continue; }
                } else if (host) finishing = coop_gameplay_finish(gameplay);
            }
            if (coop_gameplay_finished(gameplay)) {
                if (!finished_at) finished_at = SDL_GetTicks();
                if (SDL_GetTicks() - finished_at > 700) break;
            }
        }
        gamelib_ping();
        iso_redraw();
        tig_window_display();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT) good = false;
            else if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw();
            else intgame_process_event(&message);
        }
        SDL_Delay(2);
    }
    CoopGameplay* gameplay = coop_runtime_gameplay();
    good = good && gameplay && (loss ? coop_runtime_phase() == COOP_RUNTIME_FAILED && !coop_gameplay_finishing(gameplay)
                                  : coop_gameplay_finished(gameplay)) && moved && halted && observer_test
        && revision == 6 && (local != 1 || (talked && rejected_targets)) && choices == (local < 2 ? 1u : 0u) && cancelled == choices
        && (native_failure ? displaced : obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION) != initial);
    uint32_t recovery_after = 0;
    bool native_propagated = !native_failure || (strstr(coop_runtime_status(), "Cached campaign actions could not be restored")
        && !coop_session_can_retry(session));
    bool checkpoint_preserved = !native_failure || (recovery_crc(&recovery_after) && recovery_after == recovery_before);
    good &= native_propagated && checkpoint_preserved && (!native_failure || native_reported);
    /* Stop the actual borrowed connection after the agreed final state, then
     * verify native timer/game ticks cannot continue an isolated campaign. */
    DateTime before_loss = sub_45A7C0();
    coop_session_leave(session);
    coop_runtime_tick(SDL_GetTicks());
    SDL_Delay(20);
    tig_timestamp_t timestamp;
    tig_timer_now(&timestamp);
    timeevent_ping(timestamp);
    gamelib_ping();
    DateTime after_loss = sub_45A7C0();
    /* An agreed final drain deliberately retains its terminal runtime state.
     * An unexpected loss during play must still enter FAILED. Both freeze time. */
    bool terminal = coop_runtime_phase() == COOP_RUNTIME_FAILED || (!loss && gameplay && coop_gameplay_finished(gameplay));
    bool frozen = terminal && datetime_compare(&before_loss, &after_loss) == 0;
    good = good && frozen && (!custom || !host || repaired_draft);
    if (check_intro) {
        good &= coop_runtime_epoch() == 2 && (player != 2 || intro.dropped);
        coop_runtime_present_observer(NULL, NULL);
    }
    unsigned accepted = gameplay ? coop_gameplay_accepted(gameplay) : 0, rejected = gameplay ? coop_gameplay_rejected(gameplay) : 0;
    bool failure_exit = false;
    if (native_failure && good) {
        coop_lobby_open(); coop_lobby_tick(); gamelib_redraw(); tig_window_display();
        if (framebuffer) good &= tig_video_screenshot_save("arcmp-native-failure.bmp") == TIG_OK
            && tig_video_screenshot_save("arcmp-probe.bmp") == TIG_OK;
        click(116, 154, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) intgame_process_event(&message);
        good &= coop_runtime_phase() == COOP_RUNTIME_FAILED;
        click(438, 154, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
        while (tig_message_dequeue(&message) == TIG_OK) intgame_process_event(&message);
        failure_exit = coop_runtime_phase() == COOP_RUNTIME_OFF;
        good &= failure_exit;
    }
    if (transcript && fclose(transcript) != 0) good = false;
    FILE* result = fopen("arcmp-runtime-result.json", "wb");
    if (!result) good = false;
    if (result) {
        fprintf(result, "{\"verified\":%s,\"local_member\":%d,\"moved\":%s,\"halted\":%s,\"talked\":%s,\"observer_input\":%s,\"disconnect_freeze\":%s,\"loss_during_play\":%s,\"choices\":%u,\"cancellations\":%u,\"revision\":%u,\"accepted\":%u,\"rejected\":%u,\"invalid_draft_launch_refused_and_repaired\":%s,\"native_failure\":%s,\"native_fault_injected\":%s,\"native_failure_propagated\":%s,\"recovery_checkpoint_preserved\":%s,\"failed_session_exit\":%s}\n",
            good ? "true" : "false", local, moved ? "true" : "false", halted ? "true" : "false", talked ? "true" : "false",
            observer_test ? "true" : "false", frozen ? "true" : "false", loss ? "true" : "false", choices, cancelled, revision,
            accepted, rejected, repaired_draft ? "true" : "false", native_failure ? "true" : "false", fault_injected ? "true" : "false",
            native_propagated ? "true" : "false", checkpoint_preserved ? "true" : "false", failure_exit ? "true" : "false");
        if (fclose(result) != 0) good = false;
    }
    coop_runtime_detach();
    coop_session_destroy(session);
    fprintf(stderr, "ARCMP runtime controls probe %s\n", good ? "PASS" : "FAIL");
    return good;
}

static bool travel_markers(bool host, bool initialize, int original_map)
{
    if (initialize && host) {
        int64_t pc = coop_campaign_object(0);
        int64_t source = item_wield_get(pc, ITEM_INV_LOC_WEAPON), item = 0, ground = 0;
        if (!source) return false;
        int64_t prototype = obj_field_handle_get(source, OBJ_F_PROTOTYPE_HANDLE);
        int64_t location = obj_field_int64_get(pc, OBJ_F_LOCATION);
        if (!object_create(prototype, location, &item) || !item_transfer(item, coop_campaign_object(2))
            || !object_create(prototype, location, &ground)) return false;
        obj_field_int32_set(item, OBJ_F_ITEM_WORTH, 7654321);
        obj_field_int32_set(ground, OBJ_F_ITEM_WORTH, 7654322);
        script_global_var_set(1999, -64203);
    }
    unsigned carried = 0, ground = 0;
    int64_t object;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        int type = obj_field_int32_get(object, OBJ_F_TYPE);
        if (type < OBJ_TYPE_WEAPON || type > OBJ_TYPE_GENERIC) continue;
        int worth = obj_field_int32_get(object, OBJ_F_ITEM_WORTH);
        if (worth == 7654321) {
            if (obj_field_handle_get(object, OBJ_F_ITEM_PARENT) != coop_campaign_object(2)) return false;
            carried++;
        }
        if (worth == 7654322) {
            if (obj_field_handle_get(object, OBJ_F_ITEM_PARENT)) return false;
            ground++;
        }
    } while (obj_inst_next(&object, &iterator));
    return carried == 1 && ground == (map_current_map() == original_map ? 1u : 0u) && script_global_var_get(1999) == -64203;
}

static bool travel_probe(int argc, char** argv)
{
    bool host = false;
    bool framebuffer = false;
    bool loss = false, stale = false, fault_sent = false;
    unsigned port = 35742, player = COOP_MAX_PLAYERS;
    const char* address = "127.0.0.1";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-travel-loss")) loss = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-travel-stale")) stale = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
        if (!strncmp(argv[i], "--coop-probe-address=", 21)) address = argv[i] + 21;
    }
    if (!port || port > 65535 || player >= COOP_MAX_PLAYERS || host != (player == 0)) return false;
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Travel player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    CoopSession* session = coop_session_create(&config);
    if (!session) return false;
    uint64_t start = SDL_GetTicks(), playing_at = 0, finished_at = 0, delayed_until = 0, requested_at = 0;
    bool good = host ? coop_session_host(session, (uint16_t)port, start) : coop_session_join(session, address, (uint16_t)port, start);
    good = good && coop_runtime_attach(session, config.identity);
    coop_runtime_probe_enable();
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port);
    fflush(stderr);
    bool selected = false, assembled = false, initialized = false, moved = false, finishing = false, delayed = false;
    unsigned visited = 0, last_epoch = 0, cost_epoch = 0, costs_checked = 0, resume_checks = 0;
    uint64_t last_native_tick = 0;
    int original_map = map_by_type(MAP_TYPE_START_MAP);
    CoopCampaignMember members[COOP_MAX_PLAYERS];
    DateTime paused_time = { 0 };
    DateTime trip_time = { 0 };
    int64_t initial = 0, target = 0;
    while (good && SDL_GetTicks() - start < 45000) {
        uint64_t now = SDL_GetTicks();
        tig_ping();
        coop_session_poll(session, now);
        if (player == 3 && !delayed && coop_runtime_epoch() == 2 && coop_runtime_phase() == COOP_RUNTIME_LOADING) {
            delayed = true;
            delayed_until = now + 1200;
            paused_time = sub_45A7C0();
        }
        if (now >= delayed_until) coop_runtime_tick(now);
        else {
            DateTime current = sub_45A7C0();
            if (datetime_compare(&paused_time, &current)) { good = false; break; }
        }
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) {
            if ((loss || stale) && coop_runtime_epoch() == 2) break;
            fprintf(stderr, "ARCMP travel failed: %s\n", coop_runtime_status()); good = false; break;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (!selected) {
                char name[COOP_NAME_SIZE];
                snprintf(name, sizeof(name), "Travel hero %u", player);
                selected = coop_session_character(session, 16067 + player, name);
            } else if (coop_session_players(session)[coop_session_local_slot(session)].prototype
                && !coop_session_players(session)[coop_session_local_slot(session)].ready) coop_session_ready(session, true);
            unsigned count = 0;
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) count += coop_session_players(session)[i].connected;
            if (host && !assembled && count == 4 && coop_session_all_ready(session)) assembled = coop_runtime_start();
        }
        CoopGameplay* gameplay = coop_runtime_gameplay();
        unsigned epoch = coop_runtime_epoch();
        if (host && cost_epoch && epoch > cost_epoch) {
            DateTime current = sub_45A7C0();
            datetime_add_milliseconds(&trip_time, 17000);
            good = good && datetime_compare(&trip_time, &current) == 0;
            costs_checked++;
            cost_epoch = 0;
        }
        if (host && loss && epoch == 2 && coop_runtime_phase() == COOP_RUNTIME_LOADING && !fault_sent) {
            coop_session_leave(session);
            fault_sent = true;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (last_epoch != epoch) {
                if (host && epoch > 1) {
                    DateTime before = sub_45A7C0();
                    gamelib_ping();
                    DateTime after = sub_45A7C0();
                    uint64_t before_ms = (uint64_t)before.days * 86400000 + before.milliseconds;
                    uint64_t after_ms = (uint64_t)after.days * 86400000 + after.milliseconds;
                    good = good && after_ms >= before_ms && after_ms - before_ms <= 8 * (SDL_GetTicks() - last_native_tick + 5);
                    resume_checks++;
                }
                last_epoch = epoch;
                playing_at = now;
                visited++;
                fprintf(stderr, "ARCMP travel entered epoch=%u map=%d elapsed=%" PRIu64 "\n", epoch, map_current_map(), now - start);
                if (epoch == 1) {
                    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) members[i] = *coop_campaign_member(i);
                } else {
                    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                        const CoopCampaignMember* member = coop_campaign_member(i);
                        good = good && objid_is_equal(members[i].object_id, member->object_id)
                            && !memcmp(members[i].owner, member->owner, COOP_IDENTITY_SIZE);
                    }
                    good = good && travel_markers(false, false, original_map)
                        && map_current_map() == (epoch == 2 ? map_by_type(MAP_TYPE_SHOPPING_MAP) : original_map);
                    if (host && epoch == 2) good = good && now - requested_at >= 1200;
                }
                if (epoch == 3) initial = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                if (framebuffer) {
                    char path[64];
                    snprintf(path, sizeof(path), "arcmp-travel-epoch-%u.bmp", epoch);
                    iso_interface_refresh();
                    gamelib_redraw();
                    iso_redraw();
                    tig_window_display();
                    good = good && tig_video_screenshot_save(path) == TIG_OK;
                }
            }
            CoopDialogue* dialogue = coop_gameplay_dialogue(gameplay);
            const CoopDialogueView* view = coop_dialogue_current(dialogue);
            if (stale && player == 1 && epoch == 2 && now - playing_at > 500 && !fault_sent) {
                uint8_t command[41] = { 0 };
                coop_object_write32(command, 1); /* An otherwise valid command from the retired epoch. */
                coop_object_write32(command + 4, 1);
                coop_object_write32(command + 8, (uint32_t)map_current_map());
                memcpy(command + 12, coop_campaign_member((unsigned)coop_campaign_slot_for_owner(config.identity))->object_id.d.g.data, 16);
                int64_t location = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                coop_object_write32(command + 28, (uint32_t)location_get_x(location));
                coop_object_write32(command + 32, (uint32_t)location_get_y(location));
                coop_object_write32(command + 37, combat_turn_generation_get());
                fault_sent = coop_session_send(session, 0, 23, command, sizeof(command));
                good = good && fault_sent;
            }
            if (view->active && view->chooser == coop_session_local_slot(session)) key(SDL_SCANCODE_ESCAPE);
            if (host && epoch == 1 && !initialized && now - playing_at > 1000 && !view->active) {
                initialized = travel_markers(true, true, original_map);
                good = good && initialized;
            }
            if (host && epoch < 3 && now - playing_at > 3000 && !view->active && coop_dialogue_settled(dialogue)) {
                int destination = epoch == 1 ? map_by_type(MAP_TYPE_SHOPPING_MAP) : original_map;
                int64_t x, y;
                good = good && map_get_starting_location(destination, &x, &y);
                TeleportData trip = { .obj = coop_campaign_object(epoch == 1 ? 1 : 0), .map = destination,
                    .loc = location_make(x, y), .flags = TELEPORT_TIME, .time = 17 };
                DateTime before = sub_45A7C0();
                trip_time = before;
                cost_epoch = epoch;
                good = good && teleport_do(&trip) && map_current_map() != destination
                    && coop_runtime_phase() == COOP_RUNTIME_DRAINING;
                DateTime after = sub_45A7C0();
                good = good && !datetime_compare(&before, &after);
                requested_at = now;
            }
            if (epoch == 3 && !moved && now - playing_at > 500 && !view->active) {
                moved = move_click(player_get_local_pc_obj(), &target);
                good = good && moved;
            }
            if (epoch == 3 && host && !finishing && now - playing_at > 3500 && !view->active && coop_dialogue_settled(dialogue))
                finishing = coop_gameplay_finish(gameplay);
            if (epoch == 3 && coop_gameplay_finished(gameplay)) {
                if (!finished_at) finished_at = now;
                if (now - finished_at > 500) break;
            }
        }
        gamelib_ping();
        last_native_tick = SDL_GetTicks();
        iso_redraw();
        tig_window_display();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT) good = false;
            else if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw();
            else intgame_process_event(&message);
        }
        SDL_Delay(2);
    }
    CoopGameplay* gameplay = coop_runtime_gameplay();
    bool frozen = false;
    if (loss || stale) {
        DateTime before = sub_45A7C0();
        SDL_Delay(20);
        tig_timestamp_t timestamp;
        tig_timer_now(&timestamp);
        timeevent_ping(timestamp);
        gamelib_ping();
        DateTime after = sub_45A7C0();
        frozen = coop_runtime_phase() == COOP_RUNTIME_FAILED && datetime_compare(&before, &after) == 0;
        good = good && frozen && coop_runtime_epoch() == 2 && visited == (loss ? 1u : 2u)
            && (!host || (costs_checked == 1 && resume_checks == (stale ? 1u : 0u)))
            && ((loss && !host) || (stale && player != 1) || fault_sent);
    } else {
        good = good && gameplay && coop_gameplay_finished(gameplay) && visited == 3 && moved
            && obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION) != initial
            && (player != 3 || delayed) && (!host || (costs_checked == 2 && resume_checks == 2));
    }
    good = good && travel_markers(false, false, original_map);
    FILE* result = fopen("arcmp-travel-result.json", "wb");
    if (!result) good = false;
    if (result) {
        fprintf(result, "{\"verified\":%s,\"epochs\":%u,\"final_epoch\":%u,\"map\":%d,\"travel_time_costs_checked\":%u,\"resume_clock_checks\":%u,\"delayed_loading\":%s,\"moved_after_return\":%s,\"disconnect_freeze\":%s,\"loss_during_loading\":%s,\"stale_epoch_fault\":%s,\"accepted\":%u,\"rejected\":%u}\n",
            good ? "true" : "false", visited, coop_runtime_epoch(), map_current_map(), costs_checked, resume_checks, delayed ? "true" : "false", moved ? "true" : "false",
            frozen ? "true" : "false", loss ? "true" : "false", stale ? "true" : "false",
            gameplay ? coop_gameplay_accepted(gameplay) : 0, gameplay ? coop_gameplay_rejected(gameplay) : 0);
        if (fclose(result) != 0) good = false;
    }
    coop_runtime_detach();
    coop_session_destroy(session);
    fprintf(stderr, "ARCMP runtime travel probe %s\n", good ? "PASS" : "FAIL");
    return good;
}

static bool pickup_click(int64_t item)
{
    location_origin_set(obj_field_int64_get(item, OBJ_F_LOCATION));
    TigRect rect;
    object_get_rect(item, 0, &rect);
    for (int y = rect.y; y < rect.y + rect.height; y++) for (int x = rect.x; x < rect.x + rect.width; x++) {
        TargetDescriptor target;
        if (x < 0 || x >= 800 || y < 80 || y >= 430 || !target_pick_at_screen_xy(x, y, &target, false)
            || target.is_loc || target.obj != item) continue;
        click(x, y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN);
        click(x, y, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
        return true;
    }
    return false;
}
static bool items_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false;
    unsigned port = 35742, player = COOP_MAX_PLAYERS;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= COOP_MAX_PLAYERS || host != (player == 0)) return false;
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Item player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    CoopSession* session = coop_session_create(&config);
    if (!session) return false;
    uint64_t start = SDL_GetTicks(), playing_at = 0, sent_at = 0, finished_at = 0;
    bool good = host ? coop_session_host(session, (uint16_t)port, start) : coop_session_join(session, "127.0.0.1", (uint16_t)port, start);
    good = good && coop_runtime_attach(session, config.identity);
    coop_runtime_probe_enable();
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    bool selected = false, assembled = false, sent = false, finishing = false, replayed = false, staged_unchanged = false;
    unsigned stage = 0;
    int local = -1;
    ObjectID weapons[COOP_MAX_PLAYERS];
    while (good && SDL_GetTicks() - start < 45000) {
        uint64_t now = SDL_GetTicks();
        tig_ping(); coop_session_poll(session, now); coop_runtime_tick(now);
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) { good = false; break; }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (!selected) {
                char name[COOP_NAME_SIZE]; snprintf(name, sizeof(name), "Item hero %u", player);
                selected = coop_session_character(session, 16067 + player, name);
            } else if (coop_session_players(session)[coop_session_local_slot(session)].prototype
                && !coop_session_players(session)[coop_session_local_slot(session)].ready) coop_session_ready(session, true);
            unsigned count = 0;
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) count += coop_session_players(session)[i].connected;
            if (host && !assembled && count == 4 && coop_session_all_ready(session)) assembled = coop_runtime_start();
        }
        CoopGameplay* gameplay = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing_at) playing_at = now;
            CoopItems* items = coop_gameplay_items(gameplay);
            const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(gameplay));
            if (view->active && view->chooser == coop_session_local_slot(session)) key(SDL_SCANCODE_ESCAPE);
            if (!stage && !view->active && now - playing_at > 1200) {
                local = coop_campaign_slot_for_owner(config.identity);
                for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                    int64_t weapon = item_wield_get(coop_campaign_object(i), ITEM_INV_LOC_WEAPON);
                    if (!weapon) { good = false; break; }
                    weapons[i] = obj_get_id(weapon);
                }
                good = good && inven_ui_open(player_get_local_pc_obj(), 0, INVEN_UI_MODE_INVENTORY);
                stage = 1;
            }
            int64_t incoming = stage ? obj_pool_perm_lookup(weapons[(local + 3) % 4]) : 0, incoming_parent = 0;
            if (incoming) item_parent(incoming, &incoming_parent);
            if (stage == 4 && !sent && inven_ui_is_created()) {
                click(132, 20, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN);
                click(132, 20, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
            }
            if (stage && stage <= 10 && !sent && !(stage == 4 && inven_ui_is_created())
                && (stage < 8 || incoming_parent == player_get_local_pc_obj())) {
                int64_t weapon = obj_pool_perm_lookup(weapons[stage >= 8 ? (local + 3) % 4 : local]);
                good = good && weapon;
                if (stage == 1) click(70, 260, TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP);
                else if (stage == 2) {
                    int loc = item_inventory_location_get(weapon);
                    click(368 + 32 * (loc % 10) + 16, 49 + 32 * (loc / 10) + 16, TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP);
                } else if (stage == 3) {
                    click(70, 260, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN);
                    click(720, 405, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
                } else if (stage == 4) {
                    good = good && pickup_click(weapon);
                } else if (stage == 5) good = good && coop_gameplay_item(gameplay, COOP_ITEM_GIVE, weapon, (uint8_t)((local + 1) % 4), -1);
                else if (stage <= 7) {
                    CoopObjectId id;
                    good = good && coop_object_id_from_engine(weapons[local], &id);
                    const CoopCampaignMember* actor = coop_campaign_member((unsigned)(stage == 6 ? (local + 1) % 4 : local));
                    good = good && coop_gameplay_item_actor(gameplay, actor->object_id.d.g.data, COOP_ITEM_MOVE, id, 0, -1);
                } else if (stage == 8) {
                    good = good && inven_ui_open(player_get_local_pc_obj(), 0, INVEN_UI_MODE_INVENTORY);
                    click(775, 246, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN);
                    click(775, 246, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
                } else if (stage == 9) {
                    int loc = item_inventory_location_get(weapon);
                    click(368 + 32 * (loc % 10) + 16, 49 + 32 * (loc / 10) + 16, TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN);
                    click(384, 321, TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
                } else {
                    CoopObjectId id = { .bytes = { 3 } };
                    coop_object_write32(id.bytes + 1, UINT32_MAX);
                    coop_object_write32(id.bytes + 13, (uint32_t)map_current_map());
                    good = good && coop_gameplay_item_actor(gameplay, coop_campaign_member((unsigned)local)->object_id.d.g.data, COOP_ITEM_PICKUP, id, 0, -1);
                }
                sent = true; sent_at = now;
                fprintf(stderr, "ARCMP items probe submitted stage=%u member=%d\n", stage, local);
            }
            unsigned status = 0;
            uint32_t result = coop_items_last_result(items, &status);
            if (stage && stage <= 10 && sent && result == stage) {
                int64_t weapon = obj_pool_perm_lookup(weapons[local]), parent = 0;
                if (weapon) item_parent(weapon, &parent);
                good = good && status == (stage <= 5 || stage == 8 || stage == 9 ? 0u : stage == 6 ? COOP_ITEM_UNAVAILABLE : stage == 7 ? COOP_ITEM_OWNERSHIP : COOP_ITEM_TARGET);
                if (stage == 1) good = good && item_wield_get(player_get_local_pc_obj(), ITEM_INV_LOC_WEAPON) == 0;
                if (stage == 2) good = good && item_wield_get(player_get_local_pc_obj(), ITEM_INV_LOC_WEAPON) == weapon;
                if (stage == 3) good = good && !parent;
                if (stage == 4) good = good && parent == player_get_local_pc_obj();
                if (stage >= 5) good = good && parent == coop_campaign_object((unsigned)((local + 1) % 4));
                if (stage == 9) good = good && item_inventory_location_get(incoming) == 80;
                fprintf(stderr, "ARCMP items probe receipt stage=%u status=%u good=%d\n", stage, status, good);
                stage++; sent = false;
                if (stage == 11 && player == 1) {
                    uint8_t p[59] = { 0 }; CoopObjectId id = { .bytes = { 3 } };
                    coop_object_write32(p + 51, coop_items_loot_generation(items));
                    coop_object_write32(p + 55, combat_turn_generation_get());
                    coop_object_write32(id.bytes + 1, UINT32_MAX); coop_object_write32(id.bytes + 13, (uint32_t)map_current_map());
                    coop_object_write32(p, coop_runtime_epoch()); coop_object_write32(p + 4, 10); coop_object_write32(p + 8, (uint32_t)map_current_map());
                    memcpy(p + 12, coop_campaign_member((unsigned)local)->object_id.d.g.data, 16); p[28] = COOP_ITEM_PICKUP;
                    memcpy(p + 29, id.bytes, sizeof(id.bytes)); coop_object_write32(p + 47, UINT32_MAX);
                    replayed = coop_session_send(session, 0, COOP_ITEM_REQUEST, p, sizeof(p));
                    good = good && replayed;
                }
            } else if (sent && now - sent_at > 20000) {
                fprintf(stderr, "ARCMP items probe stuck stage=%u result=%u busy=%d\n", stage, result, coop_items_busy(items));
                gamelib_redraw(); iso_redraw(); tig_window_display(); tig_video_screenshot_save("arcmp-items-failure.bmp");
                good = false;
            }
            if (host && stage == 11 && !finishing && coop_items_executed(items) == 28 && coop_items_rejected(items) == 12 && coop_items_settled(items))
                finishing = coop_gameplay_finish(gameplay);
            if (coop_gameplay_finished(gameplay) && stage == 11) {
                if (!finished_at) finished_at = now;
                if (now - finished_at > 700) break;
            }
        }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT) good = false;
            else if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw();
            else intgame_process_event(&message);
        }
        if (stage == 1 && sent && !staged_unchanged && coop_items_busy(coop_gameplay_items(coop_runtime_gameplay()))) {
            staged_unchanged = item_wield_get(player_get_local_pc_obj(), ITEM_INV_LOC_WEAPON) == obj_pool_perm_lookup(weapons[local]);
            good = good && staged_unchanged;
            good = good && !coop_gameplay_item(coop_runtime_gameplay(), COOP_ITEM_ARRANGE, 0, 0, 0);
        }
        SDL_Delay(2);
    }
    CoopGameplay* gameplay = coop_runtime_gameplay();
    good = good && gameplay && coop_gameplay_finished(gameplay) && stage == 11 && staged_unchanged && (player != 1 || replayed);
    if (good) {
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            int64_t weapon = obj_pool_perm_lookup(weapons[i]), parent = 0;
            good = good && weapon && item_parent(weapon, &parent) && parent == coop_campaign_object((i + 1) % 4);
        }
        if (!inven_ui_is_created()) good = good && inven_ui_open(player_get_local_pc_obj(), 0, INVEN_UI_MODE_INVENTORY);
        gamelib_redraw(); iso_redraw(); tig_window_display();
        if (framebuffer) good = good && tig_video_screenshot_save("arcmp-items-final.bmp") == TIG_OK;
    }
    FILE* result = fopen("arcmp-items-result.json", "wb");
    if (!result) good = false;
    if (result) {
        CoopItems* items = gameplay ? coop_gameplay_items(gameplay) : NULL;
        fprintf(result, "{\"verified\":%s,\"stages\":%u,\"local_member\":%d,\"staged_without_mutation\":%s,\"replayed\":%s,\"executed\":%u,\"rejected\":%u}\n",
            good ? "true" : "false", stage ? stage - 1 : 0, local, staged_unchanged ? "true" : "false", replayed ? "true" : "false",
            items ? coop_items_executed(items) : 0, items ? coop_items_rejected(items) : 0);
        if (fclose(result) != 0) good = false;
    }
    coop_runtime_detach(); coop_session_destroy(session);
    fprintf(stderr, "ARCMP runtime items probe %s\n", good ? "PASS" : "FAIL");
    return good;
}
