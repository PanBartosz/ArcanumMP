#include "coop/runtime.h"
#include "coop/localization.h"
#include "coop/authority.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/checkpoint.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_object.h"
#include "coop/engine_rest.h"
#include "coop/engine_snapshot.h"
#include "coop/engine_transition.h"
#include "coop/engine_teleport.h"
#include "coop/engine_options.h"
#include "coop/saves_ui.h"
#include "coop/engine_use.h"
#include "coop/reconnect.h"
#include "coop/lobby_ui.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/gamelib.h"
#include "game/fate.h"
#include "game/gmovie.h"
#include "game/location.h"
#include "game/map.h"
#include "game/magictech.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/scroll.h"
#include "game/sector.h"
#include "game/stat.h"
#include "game/teleport.h"
#include "game/tile.h"
#include "game/tile_script.h"
#include "game/townmap.h"
#include "game/timeevent.h"
#include "tig/font.h"
#include "ui/gameuilib.h"
#include "ui/intgame.h"
#include "ui/hotkey_ui.h"
#include "ui/fate_ui.h"
#include "ui/mainmenu_ui.h"
#include "ui/sleep_ui.h"
#include "ui/slide_ui.h"
#include "ui/wmap_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct CoopRuntime {
    CoopSession* session;
    CoopBootstrap* bootstrap;
    CoopGameplay* gameplay;
    CoopControls* controls;
    CoopPresentation* presentation;
    CoopReconnect* reconnect;
    CoopSaveCatalog* saves;
    uint64_t autosave_tick, autosave_elapsed;
    uint32_t seen_roster, reconnect_prepared;
    uint64_t waiting_since, retry_at;
    uint8_t saved_pauses;
    CoopControlView interrupted_control;
    bool operation_interrupted;
    uint32_t prepared_present, played_present;
    bool presenting, present_interrupted, present_recovering;
    uint64_t ending_at;
    uint64_t defeated_since;
    DateTime journey_started;
    bool journey_done;
    ObjectID journey_local;
    uint8_t journey_hidden;
    uint8_t content[32];
    uint8_t* load_snapshot;
    size_t load_size;
    CoopCheckpointInfo load_info;
    CoopRest rest;
    CoopControlRequest operation;
    bool operation_pending, resume_checkpoint, rest_done;
    unsigned operation_status;
    ObjectID local_bed;
    uint64_t bed_tick, bed_wait;
    CoopRuntimePhase phase;
    uint8_t owner[COOP_IDENTITY_SIZE];
    uint8_t* snapshot;
    bool campaign;
    bool preparing_native;
    bool restoring;
    bool executing_travel;
    bool travel_pending;
    bool travel_wait_dialogue;
    bool dialogue_journey_pending;
    int64_t dialogue_journey[COOP_JOURNEY_WAYPOINTS];
    unsigned dialogue_journey_count;
    uint32_t dialogue_journey_epoch, dialogue_journey_map;
    TeleportData travel;
    ObjectID traveler;
    ObjectID entry_owner;
    uint32_t epoch;
    uint64_t loading_at;
    uint64_t reported_at;
    uint32_t reported_view;
    char error[160];
} CoopRuntime;
static CoopRuntime* runtime;
static const char checkpoint_path[] = "arcmp-campaign.acsave";
static const char recovery_path[] = "arcmp-recovery.acsave";
static unsigned control_validate(void* context, uint8_t sender, const CoopControlRequest* request);
static unsigned control_begin(void* context, uint8_t sender, const CoopControlRequest* request);
static void phase(CoopRuntimePhase value);
static void fail(const char* message);
static bool present_run(CoopRuntime* r);
static bool reconnect_prepare(CoopRuntime* r, uint32_t epoch);
static bool read_content(uint8_t content[32])
{
    FILE* file = fopen("arcmp-content.id", "rb"); char hex[66] = { 0 };
    if (!file) return false;
    bool good = fread(hex, 1, 65, file) >= 64; fclose(file);
    for (unsigned i = 0; good && i < 32; i++) {
        char pair[] = { hex[2*i], hex[2*i+1], 0 }, *end;
        content[i] = (uint8_t)strtoul(pair, &end, 16); good = !*end;
    }
    return good;
}
static bool owners_match(CoopRuntime* r, const CoopCheckpointInfo* info)
{
    unsigned count = 0, expected = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (info->members & (1u << i)) expected++;
        const CoopPlayer* p = &coop_session_players(r->session)[i];
        if (!p->connected) continue;
        count++; bool found = false;
        for (unsigned j = 0; j < COOP_MAX_PLAYERS; j++) if ((info->members & (1u << j)) && !memcmp(p->identity, info->owners[j], COOP_IDENTITY_SIZE)) found = true;
        if (!found) return false;
    }
    return count == expected;
}
static bool checkpoint_campaign_matches(const CoopCheckpointInfo* info)
{
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if (((info->members & (1u << i)) != 0) != (member != NULL)) return false;
        if (member && memcmp(member->owner, info->owners[i], COOP_IDENTITY_SIZE)) return false;
    }
    return true;
}
bool coop_runtime_save_set(CoopSaveSet* party)
{
    CoopRuntime* r = runtime;
    if (!r || !party) return false;
    *party = (CoopSaveSet) { 0 };
    memcpy(party->content, r->content, 32);
    if (r->campaign && coop_campaign_active()) {
        for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
            const CoopCampaignMember* member = coop_campaign_member(i);
            if (member && member->occupied) { party->members |= (uint8_t)(1u << i); memcpy(party->owners[i], member->owner, 16); }
        }
    } else {
        const CoopPlayer* players = coop_session_players(r->session);
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (players[i].connected) {
            party->members |= (uint8_t)(1u << i); memcpy(party->owners[i], players[i].identity, 16);
        }
    }
    return party->members != 0;
}
static void read_save(void* context, unsigned slot, CoopSaveEntry* entry)
{
    (void)context;
    CoopSaveSet party;
    if (coop_runtime_save_set(&party)) coop_save_inspect(&party, slot, entry);
    else *entry = (CoopSaveEntry) { .slot = slot, .status = COOP_CHECKPOINT_MISSING };
}
CoopSaveCatalog* coop_runtime_save_catalog(void) { return runtime ? runtime->saves : NULL; }
bool coop_runtime_save_message(uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    return runtime && coop_save_catalog_message(runtime->saves, sender, type, bytes, size);
}
static unsigned load_checkpoint_version(CoopRuntime* r, const char* path, bool checked, uint32_t fingerprint, uint8_t** bytes, size_t* size, CoopCheckpointInfo* info)
{
    CoopCheckpointStatus status = checked ? coop_checkpoint_load_checked(path, r->content, r->owner, fingerprint, info, bytes, size)
        : coop_checkpoint_load(path, r->content, r->owner, info, bytes, size);
    if (status == COOP_CHECKPOINT_MISSING) return COOP_CONTROL_NO_CHECKPOINT;
    if (status == COOP_CHECKPOINT_CONTENT) return COOP_CONTROL_CONTENT;
    if (status == COOP_CHECKPOINT_OWNER) return COOP_CONTROL_OWNERS;
    if (status != COOP_CHECKPOINT_OK) return COOP_CONTROL_IO;
    if (!owners_match(r, info)) { free(*bytes); *bytes = NULL; return COOP_CONTROL_OWNERS; }
    return COOP_CONTROL_OK;
}
static bool store_checkpoint(CoopRuntime* r, const char* path, const uint8_t* bytes, size_t size, const char* description)
{
    CoopCheckpointInfo info = { .epoch = r->epoch, .map = (uint32_t)map_current_map() };
    memcpy(info.content, r->content, 32); snprintf(info.description, sizeof(info.description), "%s", description);
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if (!member) return false;
        info.members |= (uint8_t)(1u << i); memcpy(info.owners[i], member->owner, COOP_IDENTITY_SIZE);
    }
    CoopCheckpointStatus status = coop_checkpoint_save(path, &info, bytes, size);
    fprintf(stderr, "ARCMP checkpoint file=%s epoch=%u map=%u status=%u bytes=%zu\n", path, r->epoch, info.map, status, size);
    if (status == COOP_CHECKPOINT_OK || status == COOP_CHECKPOINT_COMMITTED_UNSYNCED) r->autosave_elapsed = 0;
    return status == COOP_CHECKPOINT_OK || status == COOP_CHECKPOINT_COMMITTED_UNSYNCED;
}
static bool store_slot(CoopRuntime* r, unsigned slot, const uint8_t* bytes, size_t size, const char* description)
{
    CoopSaveSet party; CoopSaveEntry previous; char path[COOP_SAVE_PATH];
    if (slot >= COOP_SAVE_TOTAL || slot == COOP_SAVE_RECOVERY || !coop_runtime_save_set(&party) || !coop_save_path(&party, slot, path)) return false;
    if (slot) {
        coop_save_inspect(&party, slot, &previous);
        if (previous.status != COOP_CHECKPOINT_OK && previous.status != COOP_CHECKPOINT_MISSING) return false;
    }
    return store_checkpoint(r, path, bytes, size, description[0] ? description : "Party checkpoint");
}
static unsigned validate_checkpoint(CoopRuntime* r, const CoopControlRequest* request)
{
    CoopSaveSet party; CoopSaveEntry entry;
    if (!coop_runtime_save_set(&party)) return COOP_CONTROL_OWNERS;
    coop_save_inspect(&party, request->value, &entry);
    if (request->action == COOP_CONTROL_SAVE && !request->value && !request->description[0] && !request->checkpoint_present) return COOP_CONTROL_OK;
    if (entry.status == COOP_CHECKPOINT_MISSING)
        return request->action == COOP_CONTROL_SAVE && !request->checkpoint_present ? COOP_CONTROL_OK : COOP_CONTROL_NO_CHECKPOINT;
    if (entry.status == COOP_CHECKPOINT_CONTENT) return COOP_CONTROL_CONTENT;
    if (entry.status == COOP_CHECKPOINT_OWNER || (entry.status == COOP_CHECKPOINT_OK && !owners_match(r, &entry.info))) return COOP_CONTROL_OWNERS;
    if (entry.status != COOP_CHECKPOINT_OK) return COOP_CONTROL_IO;
    if ((request->action == COOP_CONTROL_SAVE && !request->checkpoint_present)
        || (request->checkpoint_present && request->checkpoint_fingerprint != entry.fingerprint)
        || (request->action == COOP_CONTROL_LOAD && request->description[0] && strcmp(request->description, entry.info.description))) return COOP_CONTROL_CHANGED;
    return COOP_CONTROL_OK;
}
static bool playing_allowed(void)
{
    if (!runtime || runtime->phase != COOP_RUNTIME_PLAYING || !runtime->gameplay || coop_gameplay_finishing(runtime->gameplay)) return false;
    if (runtime->presenting || runtime->present_recovering || coop_present_active(runtime->presentation) || runtime->ending_at || coop_runtime_entry_pending()) return false;
    if (coop_session_phase(runtime->session) != COOP_LOBBY || coop_session_revision(runtime->session) != runtime->seen_roster) return false;
    const CoopControlView* v = coop_controls_current(runtime->controls);
    return !v || (!v->paused && (v->phase == COOP_CONTROL_IDLE || v->phase == COOP_CONTROL_VOTING));
}
static unsigned control_validate(void* context, uint8_t sender, const CoopControlRequest* request)
{
    CoopRuntime* r = context;
    if (r->presenting || r->present_recovering || coop_present_active(r->presentation) || r->ending_at) return COOP_CONTROL_BUSY;
    if (!coop_session_is_host(r->session) || sender >= COOP_MAX_PLAYERS || !coop_session_players(r->session)[sender].connected) return COOP_CONTROL_OWNER;
    int member = coop_campaign_slot_for_owner(coop_session_players(r->session)[sender].identity);
    int64_t pc = member < 0 ? 0 : coop_campaign_object((unsigned)member);
    if (!pc || memcmp(obj_get_id(pc).d.g.data, request->actor, 16)) return COOP_CONTROL_OWNER;
    if (request->action == COOP_CONTROL_VOTE || request->action == COOP_CONTROL_CANCEL) return COOP_CONTROL_OK;
    if (r->phase != COOP_RUNTIME_PLAYING || !r->gameplay || coop_gameplay_finishing(r->gameplay) || request->map != (uint32_t)map_current_map()) return COOP_CONTROL_BUSY;
    if (request->action == COOP_CONTROL_PAUSE || request->action == COOP_CONTROL_RESUME_ALL) return COOP_CONTROL_OK;
    if (coop_runtime_entry_pending() || coop_engine_dialogue_pending() || coop_dialogue_current(coop_gameplay_dialogue(r->gameplay))->active) return COOP_CONTROL_BUSY;
    if ((request->action == COOP_CONTROL_REST || request->action == COOP_CONTROL_TRAVEL) && coop_engine_teleport_any()) return COOP_CONTROL_BUSY;
    if (request->action == COOP_CONTROL_TRAVEL) {
        int64_t max_x, max_y; location_limits_get(&max_x, &max_y);
        if (map_current_map() != map_by_type(MAP_TYPE_START_MAP)
            || sector_is_blocked(sector_id_from_loc(obj_field_int64_get(pc, OBJ_F_LOCATION)))) return COOP_CONTROL_LOCATION;
        for (unsigned i = 0; i < (request->route_count ? request->route_count : 1); i++) {
            uint32_t packed = request->route_count ? request->route[i] : request->value;
            int64_t destination = location_make((int64_t)(packed >> 16) * 64 + 32, (int64_t)(packed & 65535) * 64 + 32);
            if (location_get_x(destination) >= max_x || location_get_y(destination) >= max_y
                || sector_is_blocked(sector_id_from_loc(destination))) return COOP_CONTROL_LOCATION;
        }
        /* Native walking travel starts outside local-map regions and encounters.
         * Refuse before collecting votes or draining the current world. */
        if (townmap_get(sector_id_from_loc(obj_field_int64_get(pc, OBJ_F_LOCATION))) != TOWNMAP_NONE
            || wmap_ui_is_encounter()) return COOP_CONTROL_LOCATION;
        if (!critter_is_active(pc)) return COOP_CONTROL_INCAPACITATED;
        for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
            int64_t member_pc = coop_campaign_object(i);
            if (!member_pc || object_dist(pc, member_pc) > 8) return COOP_CONTROL_DISTANCE;
            if (!critter_is_dead(member_pc) && !combat_can_exit_combat_mode(member_pc)) return COOP_CONTROL_ENEMIES;
        }
        return COOP_CONTROL_OK;
    }
    if (request->action == COOP_CONTROL_REST) {
        int64_t bed = request->bed.bytes[0] ? obj_pool_perm_lookup(coop_object_id_to_engine(request->bed)) : 0;
        if (request->bed.bytes[0] && !bed) return COOP_CONTROL_LOCATION;
        return coop_engine_rest_validate(pc, bed, request->value);
    }
    if (request->action == COOP_CONTROL_SAVE && request->value >= COOP_SAVE_AUTOSAVE_FIRST && sender != 0) return COOP_CONTROL_OWNER;
    if (request->action == COOP_CONTROL_LOAD || request->action == COOP_CONTROL_SAVE) return validate_checkpoint(r, request);
    return COOP_CONTROL_OK;
}
static unsigned control_begin(void* context, uint8_t sender, const CoopControlRequest* request)
{
    CoopRuntime* r = context;
    unsigned status = control_validate(context, sender, request);
    if (status) return status;
    if (r->operation_pending || r->travel_pending || r->epoch == UINT32_MAX) return COOP_CONTROL_BUSY;
    if (request->action == COOP_CONTROL_LOAD) {
        CoopSaveSet party; char path[COOP_SAVE_PATH];
        if (!coop_runtime_save_set(&party) || !coop_save_path(&party, request->value, path)) return COOP_CONTROL_NO_CHECKPOINT;
        status = load_checkpoint_version(r, path, request->checkpoint_present != 0, request->checkpoint_fingerprint, &r->load_snapshot, &r->load_size, &r->load_info);
        if (status) return status;
    }
    if (!coop_gameplay_transition(r->gameplay, r->epoch + 1)) { free(r->load_snapshot); r->load_snapshot = NULL; return COOP_CONTROL_BUSY; }
    r->operation = *request; r->operation_pending = true; r->operation_status = COOP_CONTROL_OK;
    phase(COOP_RUNTIME_DRAINING);
    return COOP_CONTROL_OK;
}
CoopControls* coop_runtime_controls(void) { return runtime ? runtime->controls : NULL; }
CoopSession* coop_runtime_session(void) { return runtime ? runtime->session : NULL; }
bool coop_runtime_control_message(uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    return runtime && runtime->controls && coop_controls_message(runtime->controls, sender, type, bytes, size);
}
bool coop_runtime_paused(void)
{
    const CoopControlView* view = coop_runtime_controls() ? coop_controls_current(runtime->controls) : NULL;
    return view && view->paused;
}
static bool control_route(CoopControlAction action, unsigned value, uint32_t proposal, int64_t bed, const uint32_t* route, unsigned count)
{
    bool coordination = action == COOP_CONTROL_VOTE || action == COOP_CONTROL_CANCEL;
    if (!runtime || !runtime->controls || (runtime->phase != COOP_RUNTIME_PLAYING && runtime->phase != COOP_RUNTIME_RESTING && runtime->phase != COOP_RUNTIME_LOADING && runtime->phase != COOP_RUNTIME_JOURNEY
        && !(coordination && runtime->phase == COOP_RUNTIME_DRAINING))) return false;
    if (!coordination && runtime->gameplay && coop_gameplay_finished(runtime->gameplay)) return false;
    const CoopControlView* view = coop_controls_current(runtime->controls);
    /* The action controller finishes before native rest/journey work starts.
     * Its next world epoch is already reserved, while the active coordination
     * proposal still belongs to the preceding epoch until capture completes.
     * Keep its vote/Stop path usable through that interval. */
    bool active_previous = coordination && view->proposal && view->epoch < runtime->epoch
        && runtime->epoch - view->epoch == 1;
    if ((!active_previous && view->epoch != runtime->epoch) || view->map != (uint32_t)map_current_map()) return false;
    int64_t pc = player_get_local_pc_obj();
    if (!pc) return false;
    CoopControlRequest request = { .map = (uint32_t)map_current_map(), .action = action, .value = value, .proposal = proposal };
    if (action == COOP_CONTROL_LOAD) {
        CoopSaveSet party; CoopSaveEntry entry;
        if (!coop_runtime_save_set(&party)) return false;
        coop_save_inspect(&party, value, &entry);
        if (entry.status != COOP_CHECKPOINT_OK) return false;
        request.checkpoint_present = 1;
        request.checkpoint_fingerprint = entry.fingerprint;
        memcpy(request.description, entry.info.description, sizeof(request.description));
    }
    request.route_count = (uint8_t)count;
    if (count) memcpy(request.route, route, count * sizeof(*route));
    memcpy(request.actor, obj_get_id(pc).d.g.data, 16);
    if (bed && !coop_object_id_from_engine(obj_get_id(bed), &request.bed)) return false;
    return coop_controls_submit(runtime->controls, &request);
}
bool coop_runtime_control(CoopControlAction action, unsigned value, uint32_t proposal, int64_t bed)
{
    return control_route(action, value, proposal, bed, NULL, 0);
}
bool coop_runtime_pause_toggle(void)
{
    if (!runtime || !runtime->controls) return false;
    const CoopControlView* view = coop_controls_current(runtime->controls);
    uint8_t slot = coop_session_local_slot(runtime->session);
    return slot < COOP_MAX_PLAYERS && coop_runtime_control(COOP_CONTROL_PAUSE, !(view->paused & (1u << slot)), 0, 0);
}
bool coop_runtime_modal_tick(TigWindowModalDialogChoice* choice)
{
    coop_lobby_tick();
    gamelib_ping();
    if (!runtime) return false;
    const CoopControlView* control = coop_controls_current(runtime->controls);
    if ((runtime->phase != COOP_RUNTIME_LOBBY && runtime->phase != COOP_RUNTIME_PLAYING)
        || (control && control->phase == COOP_CONTROL_VOTING)
        || coop_present_active(runtime->presentation)) {
        *choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL;
        return true;
    }
    return false;
}
bool coop_runtime_fate(int64_t actor, unsigned fate, bool activate)
{
    if (!playing_allowed() || !actor || actor != player_get_local_pc_obj() || fate >= FATE_COUNT || critter_is_dead(actor)
        || (!activate && !fate)) return false;
    int points = stat_base_get(actor, STAT_FATE_POINTS);
    if (points < 0) return false;
    CoopProgressionRequest request = { .operation = COOP_PROGRESS_FATE, .map = (uint32_t)map_current_map(),
        .index = (uint8_t)fate, .direction = activate ? 1 : 2,
        .expected = (uint32_t)obj_field_int32_get(actor, OBJ_F_PC_FLAGS_FATE), .points = (uint32_t)points };
    memcpy(request.actor, obj_get_id(actor).d.g.data, 16);
    return coop_progression_submit(coop_gameplay_progression(runtime->gameplay), &request);
}
bool coop_runtime_magic_choice(CoopMagicRequest* request)
{
    if (!request || request->operation != COOP_MAGIC_DESTINATION || !playing_allowed()) return false;
    int64_t actor = player_get_local_pc_obj();
    if (!actor) return false;
    memcpy(request->actor, obj_get_id(actor).d.g.data, 16);
    request->map = (uint32_t)map_current_map(); request->generation = combat_turn_generation_get();
    return coop_gameplay_magic_request(runtime->gameplay, request);
}
bool coop_runtime_difficulty(unsigned value)
{
    if (!runtime || !coop_session_is_host(runtime->session) || !playing_allowed()
        || coop_gameplay_finishing(runtime->gameplay) || runtime->operation_pending || runtime->travel_pending
        || coop_controls_current(runtime->controls)->phase != COOP_CONTROL_IDLE
        || !coop_engine_options_difficulty_set(value)) return false;
    static const char* names[] = { "Easy", "Moderate", "Hard" };
    char line[96]; snprintf(line, sizeof(line), "The host set campaign difficulty to %s.", names[value]);
    coop_session_chat(runtime->session, line);
    return true;
}
bool coop_runtime_player_options(unsigned mask)
{
    if (mask > 15 || !playing_allowed() || coop_gameplay_finishing(runtime->gameplay)) return false;
    int64_t actor = player_get_local_pc_obj();
    if (!actor) return false;
    CoopProgressionRequest request = { .operation = COOP_PROGRESS_OPTIONS, .map = (uint32_t)map_current_map(),
        .direction = (uint8_t)mask, .expected = coop_engine_options_record(actor) };
    memcpy(request.actor, obj_get_id(actor).d.g.data, 16);
    return coop_progression_submit(coop_gameplay_progression(runtime->gameplay), &request);
}
bool coop_runtime_player_option(unsigned option, bool value)
{
    if (option >= COOP_OPTION_COUNT || !playing_allowed() || coop_gameplay_finishing(runtime->gameplay)) return false;
    return coop_progression_option(coop_gameplay_progression(runtime->gameplay), option, value);
}
bool coop_runtime_note(const CoopNoteRequest* request)
{
    if (!request || !playing_allowed() || coop_gameplay_finishing(runtime->gameplay)) return false;
    int64_t actor = player_get_local_pc_obj();
    if (!actor) return false;
    CoopNoteRequest r = *request; r.map = (uint32_t)map_current_map();
    memcpy(r.actor, obj_get_id(actor).d.g.data, 16);
    return coop_notes_submit(coop_gameplay_notes(runtime->gameplay), &r);
}
bool coop_runtime_commit_spell_destination(int64_t actor, int64_t location, unsigned spell, bool charge)
{
    CoopRuntime* r = runtime;
    if (!r || !coop_session_is_host(r->session) || !playing_allowed() || r->operation_pending || r->travel_pending
        || spell >= MT_SPELL_COUNT || coop_campaign_slot_for_object(actor) < 0) return false;
    if (charge && !magictech_can_charge_spell_fatigue(actor, (int)spell)) return false;
    TeleportData travel = { .obj = actor, .loc = location, .map = map_by_type(MAP_TYPE_START_MAP), .flags = TELEPORT_RENDER_LOCK | TELEPORT_COOP_PARTY };
    if (!teleport_do(&travel)) return false;
    if (charge && !magictech_charge_spell_fatigue(actor, (int)spell)) {
        fail("The committed spell cost could not be applied. Use Recover party."); return false;
    }
    return true;
}
bool coop_runtime_checkpoint_request(CoopControlAction action, const CoopSaveEntry* entry, const char* description)
{
    CoopRuntime* r = runtime;
    if (!r || !entry || r->phase != COOP_RUNTIME_PLAYING || !r->gameplay || coop_gameplay_finishing(r->gameplay)
        || (action != COOP_CONTROL_SAVE && action != COOP_CONTROL_LOAD) || entry->slot >= COOP_SAVE_TOTAL
        || (action == COOP_CONTROL_SAVE && entry->slot >= COOP_SAVE_SLOTS)
        || (entry->status != COOP_CHECKPOINT_OK && !(action == COOP_CONTROL_SAVE && entry->status == COOP_CHECKPOINT_MISSING))) return false;
    int64_t pc = player_get_local_pc_obj();
    if (!pc) return false;
    const CoopControlView* view = coop_controls_current(r->controls);
    if (view->epoch != r->epoch || view->map != (uint32_t)map_current_map() || view->phase != COOP_CONTROL_IDLE) return false;
    CoopControlRequest request = { .map = (uint32_t)map_current_map(), .action = action, .value = entry->slot,
        .checkpoint_present = entry->status == COOP_CHECKPOINT_OK, .checkpoint_fingerprint = entry->fingerprint };
    const char* label = action == COOP_CONTROL_LOAD ? entry->info.description : description;
    if (!label || strlen(label) >= sizeof(request.description)) return false;
    snprintf(request.description, sizeof(request.description), "%s", label);
    memcpy(request.actor, obj_get_id(pc).d.g.data, 16);
    return coop_controls_submit(r->controls, &request);
}
static bool exists(const char* path) { FILE* file = fopen(path, "rb"); if (!file) return false; fclose(file); return true; }
bool coop_runtime_checkpoint_exists(void) { return exists(checkpoint_path); }
bool coop_runtime_recovery_exists(void) { return exists(recovery_path); }
bool coop_runtime_native_save_allowed(void) { return !runtime || ((!runtime->gameplay || coop_gameplay_finished(runtime->gameplay)) && runtime->phase != COOP_RUNTIME_FAILED && coop_session_is_host(runtime->session)); }
bool coop_runtime_native_load_allowed(void) { return !runtime || runtime->restoring || !runtime->campaign; }
static bool probe;
static uint32_t probe_retry_delay = 1000;
void coop_runtime_probe_retry_delay(uint32_t milliseconds) { if (probe && milliseconds <= 30000) probe_retry_delay = milliseconds; }
void coop_runtime_probe_enable(void) { probe = true; }
static void report(uint64_t now)
{
    CoopRuntime* r = runtime;
    if (!probe || !r->gameplay || !coop_campaign_validate() || now - r->reported_at < 200) return;
    r->reported_at = now;
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(r->gameplay));
    if (view->revision && view->revision != r->reported_view) {
        size_t size = 0;
        uint8_t* bytes = coop_dialogue_view_encode(view, &size);
        FILE* file = bytes ? fopen("arcmp-live-dialogue.bin", "wb") : NULL;
        if (file) {
            if (fwrite(bytes, 1, size, file) == size) r->reported_view = view->revision;
            fclose(file);
        }
        free(bytes);
    }
    FILE* file = fopen("arcmp-live-state.json", "wb");
    if (!file) return;
    DateTime time = sub_45A7C0();
    fprintf(file, "{\"phase\":%u,\"epoch\":%u,\"map\":%d,\"authority\":%u,\"local_slot\":%u,\"local_member\":%d,\"owner\":\"",
        r->phase, r->epoch, map_current_map(), coop_authority_get(), coop_session_local_slot(r->session), coop_campaign_slot_for_owner(r->owner));
    for (unsigned i = 0; i < COOP_IDENTITY_SIZE; i++) fprintf(file, "%02x", r->owner[i]);
    fprintf(file, "\",\"world_revision\":%u,\"accepted\":%u,\"rejected\":%u,\"days\":%u,\"milliseconds\":%u,\"dialogue_revision\":%u,\"dialogue_active\":%s,\"members\":[",
        coop_gameplay_world_revision(r->gameplay), coop_gameplay_accepted(r->gameplay), coop_gameplay_rejected(r->gameplay),
        time.days, time.milliseconds, view->revision, view->active ? "true" : "false");
    bool first = true;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        if (!pc) continue;
        char id[OID_STR_LEN];
        objid_id_to_str(id, obj_get_id(pc));
        int64_t loc = obj_field_int64_get(pc, OBJ_F_LOCATION);
        fprintf(file, "%s{\"slot\":%u,\"id\":\"%s\",\"x\":%" PRId64 ",\"y\":%" PRId64 ",\"offset_x\":%d,\"offset_y\":%d,\"aid\":%u,\"strength\":%d,\"hp_damage\":%d,\"fatigue_damage\":%d}",
            first ? "" : ",", i, id, location_get_x(loc), location_get_y(loc), obj_field_int32_get(pc, OBJ_F_OFFSET_X), obj_field_int32_get(pc, OBJ_F_OFFSET_Y),
            (unsigned)obj_field_int32_get(pc, OBJ_F_CURRENT_AID), stat_base_get(pc, STAT_STRENGTH), obj_field_int32_get(pc, OBJ_F_HP_DAMAGE), obj_field_int32_get(pc, OBJ_F_CRITTER_FATIGUE_DAMAGE));
        first = false;
    }
    fprintf(file, "]}\n");
    fclose(file);
}
static bool plan_party(int64_t anchor,int64_t positions[COOP_MAX_PLAYERS],int64_t max_x,int64_t max_y)
{
    if (tile_is_blocking(anchor,false)) return false;
    positions[0]=anchor;
    for (unsigned i = coop_campaign_next_slot(1); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc=coop_campaign_object(i); if (!pc) return false;
        bool placed=false;
        for (int radius=2;!placed && radius<=8;radius++)
            for (int dy=-radius;!placed && dy<=radius;dy++) for (int dx=-radius;!placed && dx<=radius;dx++) {
                if (abs(dx)!=radius && abs(dy)!=radius) continue;
                int64_t x=location_get_x(anchor)+dx,y=location_get_y(anchor)+dy;
                if (x<0 || y<0 || x>=max_x || y>=max_y) continue;
                int64_t candidate=location_make(x,y);
                if (tile_is_blocking(candidate,false)) continue;
                bool separated=true;
                for (unsigned j = coop_campaign_next_slot(0); j < i; j = coop_campaign_next_slot(j + 1))
                    if (location_dist(candidate, positions[j]) < 2) separated = false;
                if (!separated) continue;
                int8_t rotations[64];
                PathCreateInfo path={.obj=pc,.from=anchor,.to=candidate,.max_rotations=SDL_arraysize(rotations),
                    .rotations=rotations,.flags=PATH_FLAG_0x0010};
                if (path_make(&path)<=0) continue;
                positions[i]=candidate; placed=true;
            }
        if (!placed) return false;
    }
    return true;
}
static bool place_party(void)
{
    if (!coop_campaign_count() || !coop_campaign_object(0)) return false;
    int64_t original=obj_field_int64_get(coop_campaign_object(0),OBJ_F_LOCATION),max_x,max_y,positions[COOP_MAX_PLAYERS];
    map_precache_sectors(original); location_limits_get(&max_x,&max_y);
    /* A clear sector-centre tile can be isolated by cliffs. Plan the entire
     * party first, searching nearby anchors when any member cannot fit. Do
     * not partially relocate characters while testing candidate landings. */
    for (int radius=0;radius<=32;radius++)
        for (int dy=-radius;dy<=radius;dy++) for (int dx=-radius;dx<=radius;dx++) {
            if (radius && abs(dx)!=radius && abs(dy)!=radius) continue;
            int64_t x=location_get_x(original)+dx,y=location_get_y(original)+dy;
            if (x<0 || y<0 || x>=max_x || y>=max_y || !plan_party(location_make(x,y),positions,max_x,max_y)) continue;
            for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) sub_43E770(coop_campaign_object(i),positions[i],0,0);
            if (radius) fprintf(stderr,"ARCMP party landing shifted radius=%d members=%u\n",radius,coop_campaign_count());
            return true;
        }
    fprintf(stderr,"ARCMP party has no complete landing near %lld,%lld\n",(long long)location_get_x(original),(long long)location_get_y(original));
    return false;
}
static void phase(CoopRuntimePhase value)
{
    if (runtime->phase == COOP_RUNTIME_FAILED) return;
    if (runtime->phase == value) return;
    if (value != COOP_RUNTIME_LOBBY && value != COOP_RUNTIME_PLAYING) tig_window_modal_dialog_cancel();
    if (value != COOP_RUNTIME_PLAYING) fate_ui_close();
    if (runtime->phase == COOP_RUNTIME_PLAYING) hotkey_ui_coop_store();
    if (value != COOP_RUNTIME_LOBBY && value != COOP_RUNTIME_PLAYING) coop_saves_ui_close();
    runtime->phase = value;
    if (value != COOP_RUNTIME_PLAYING) wmap_ui_coop_dismiss_spell();
    fprintf(stderr, "ARCMP runtime phase=%u\n", value);
    if (value == COOP_RUNTIME_PLAYING) {
        hotkey_ui_coop_bind(runtime->owner);
        runtime->entry_owner = (ObjectID) { 0 };
        char id[OID_STR_LEN];
        objid_id_to_str(id, obj_get_id(player_get_local_pc_obj()));
        fprintf(stderr, "ARCMP campaign connected host=%d slot=%u member=%d pc=%s map-ready=1\n",
            coop_session_is_host(runtime->session), coop_session_local_slot(runtime->session),
            coop_campaign_slot_for_owner(runtime->owner), id);
    }
    fflush(stderr);
}
static void fail(const char* message)
{
    if (!runtime || runtime->phase == COOP_RUNTIME_FAILED) return;
    char reason[sizeof(runtime->error)] = { 0 };
    snprintf(reason, sizeof(reason), "%s", message ? message : "The campaign could not continue. Use Recover party.");
    memcpy(runtime->error, reason, sizeof(reason));
    phase(COOP_RUNTIME_FAILED);
    fprintf(stderr, "ARCMP runtime failed: %s\n", runtime->error);
    coop_session_abort(runtime->session, runtime->error);
}
void coop_runtime_native_failure(const char* message)
{
    if (runtime && runtime->phase != COOP_RUNTIME_OFF && runtime->phase != COOP_RUNTIME_LOBBY
        && runtime->phase != COOP_RUNTIME_FAILED) fail(message);
}
static TimeEvent arriving_tile;
static unsigned arrival_count;
static int arrival_sequence;
static TimeEvent dispatching_arrival;
static bool entry_any(TimeEvent* event)
{
    (void)event;
    return true;
}
static bool entry_count(TimeEvent* event)
{
    (void)event;
    arrival_count++;
    if (event->type == TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED && event->params[3].integer_value > arrival_sequence)
        arrival_sequence = event->params[3].integer_value;
    return false;
}
static bool entry_matches(TimeEvent* event)
{
    return event->params[0].object_value == arriving_tile.params[0].object_value
        && event->params[1].location_value == arriving_tile.params[1].location_value
        && event->params[2].integer_value == arriving_tile.params[2].integer_value;
}
bool coop_runtime_entry_pending(void)
{
    return runtime && coop_session_is_host(runtime->session)
        && (timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL, entry_any) || timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED, entry_any));
}
static bool entry_prior(TimeEvent* other)
{
    /* Legacy checkpoints retain their existing deadline order and precede
     * newly queued entries. New entries carry an explicit saved sequence:
     * retry timestamps alone cannot keep FIFO order across partial pings. */
    if (other->type == TIMEEVENT_TYPE_COOP_ARRIVAL)
        return dispatching_arrival.type == TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED
            || datetime_compare(&other->datetime, &dispatching_arrival.datetime) < 0;
    return dispatching_arrival.type == TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED
        && other->params[3].integer_value < dispatching_arrival.params[3].integer_value;
}
bool coop_runtime_defer_tile(int64_t location, int64_t triggerer)
{
    CoopRuntime* r = runtime;
    if (!r || r->phase == COOP_RUNTIME_OFF || r->phase == COOP_RUNTIME_LOBBY || !triggerer
        || obj_field_int32_get(triggerer, OBJ_F_TYPE) != OBJ_TYPE_PC) return false;
    if (r->phase == COOP_RUNTIME_PLAYING && r->gameplay && coop_gameplay_running(r->gameplay)) return false;
    if (!coop_session_is_host(r->session)) return true;
    if (r->entry_owner.type) {
        int64_t initiating = obj_pool_perm_lookup(r->entry_owner);
        if (initiating && coop_campaign_slot_for_object(initiating) >= 0) triggerer = initiating;
    }
    arriving_tile = (TimeEvent) { .type = TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED };
    arriving_tile.params[0].object_value = triggerer;
    arriving_tile.params[1].location_value = location;
    arriving_tile.params[2].integer_value = map_current_map();
    if (timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL, entry_matches) || timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED, entry_matches)) return true;
    arrival_count = 0;
    arrival_sequence = 0;
    timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL, entry_count);
    timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED, entry_count);
    arriving_tile.params[3].integer_value = arrival_sequence < INT32_MAX ? arrival_sequence + 1 : 0;
    DateTime delay = { .milliseconds = arrival_count + 1 };
    if (arrival_count >= 64 || !arriving_tile.params[3].integer_value || !timeevent_add_delay(&arriving_tile, &delay)) {
        fail("Could not retain the native arrival event. The campaign requires recovery.");
        return true;
    }
    fprintf(stderr, "ARCMP deferred arrival tile map=%d location=%lld,%lld phase=%u pending=%u member=%d\n", map_current_map(),
        (long long)location_get_x(location), (long long)location_get_y(location), r->phase, arrival_count + 1, coop_campaign_slot_for_object(triggerer));
    return true;
}
bool coop_runtime_arrival_event(TimeEvent* event)
{
    CoopRuntime* r = runtime;
    if (!r || !coop_session_is_host(r->session)) return true;
    if (event->type == TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED && event->params[3].integer_value <= 0) {
        fail("The saved campaign arrival order is invalid. Use Recover party.");
        return true;
    }
    dispatching_arrival = *event;
    bool older = timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL, entry_prior) || timeevent_any(TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED, entry_prior);
    const CoopControlView* controls = coop_controls_current(r->controls);
    if (r->phase != COOP_RUNTIME_PLAYING || !r->gameplay || !coop_gameplay_running(r->gameplay)
        || older || controls->paused || controls->phase != COOP_CONTROL_IDLE || coop_engine_dialogue_pending()
        || coop_dialogue_current(coop_gameplay_dialogue(r->gameplay))->active
        || !coop_dialogue_settled(coop_gameplay_dialogue(r->gameplay))) {
        /* Advance each saved deadline equally. Replacing them with now+100
         * gives simultaneous retries equal timestamps and the native LIFO
         * insertion reverses their original order. */
        DateTime next = event->datetime;
        datetime_add_milliseconds(&next, 100);
        if (!timeevent_add_base(event, &next)) fail("Could not defer the native arrival event.");
        return true;
    }
    int64_t triggerer = event->params[0].object_value, location = event->params[1].location_value;
    if (event->params[2].integer_value == map_current_map() && triggerer && coop_campaign_slot_for_object(triggerer) >= 0) {
        fprintf(stderr, "ARCMP dispatch arrival tile map=%d location=%lld,%lld\n", map_current_map(),
            (long long)location_get_x(location), (long long)location_get_y(location));
        tile_script_exec(location, triggerer);
    }
    return true;
}
static void (*present_observer)(void*);
static void* present_observer_context;
void coop_runtime_present_observer(void (*observer)(void*), void* context) { present_observer = observer; present_observer_context = context; }
CoopPresentation* coop_runtime_presentation(void) { return runtime ? runtime->presentation : NULL; }
bool coop_runtime_present_active(void) { return runtime && runtime->presenting; }
bool coop_runtime_present_message(uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    return runtime && coop_present_message(runtime->presentation, sender, type, bytes, size);
}
static unsigned bits(unsigned mask) { unsigned count = 0; while (mask) { count += mask & 1; mask >>= 1; } return count; }
const char* coop_runtime_present_status(void)
{
    static char status[160];
    if (!runtime) return "";
    const CoopPresentView* v = coop_present_current(runtime->presentation);
    if (!v || !v->sequence) return "";
    if (v->phase == COOP_PRESENT_PREPARING) snprintf(status, sizeof(status), "Preparing the shared story: %u/%u travellers ready.", bits(v->ready), bits(v->participants));
    else if (v->phase == COOP_PRESENT_FAILED) snprintf(status, sizeof(status), "A traveller could not play the shared story. The campaign is paused.");
    else if (v->kind == COOP_PRESENT_FADE)
        snprintf(status, sizeof(status), "The shared scene is changing. Waiting for the party...");
    else if (v->kind == COOP_PRESENT_MOVIE && (v->flags & GAME_MOVIE_IGNORE_KEYBOARD)) snprintf(status, sizeof(status), "Watching together. Waiting for every traveller before continuing.");
    else snprintf(status, sizeof(status), "Escape/click to request a skip: %u/%u agreed. %u finished watching.", bits(v->skip | v->done), bits(v->participants), bits(v->done));
    return status;
}
bool coop_runtime_present_input(void)
{
    if (!runtime || !runtime->presenting) return false;
    const CoopPresentView* v = coop_present_current(runtime->presentation);
    if (v->kind != COOP_PRESENT_FADE && (v->kind != COOP_PRESENT_MOVIE || !(v->flags & GAME_MOVIE_IGNORE_KEYBOARD))) coop_present_skip(runtime->presentation);
    return true;
}
int coop_runtime_present_pump(void)
{
    CoopRuntime* r = runtime;
    if (!r || !r->presenting || r->phase == COOP_RUNTIME_FAILED) return -1;
    uint64_t now = SDL_GetTicks();
    coop_session_poll(r->session, now);
    if (coop_session_failed(r->session)) { fail(coop_session_status(r->session)); return -1; }
    uint8_t online = 0, reserved = coop_session_reserved(r->session);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (coop_session_players(r->session)[i].connected) online |= (uint8_t)(1u << i);
    if (coop_session_phase(r->session) != COOP_LOBBY || (reserved && online != reserved)
        || coop_session_revision(r->session) != r->seen_roster || coop_reconnect_active(r->reconnect)) {
        /* Stop local playback, retain the native caller on its stack and
         * rejoin through the ordinary reserved-owner barrier before replay. */
        r->present_interrupted = true;
        return 1;
    }
    if (!coop_present_poll(r->presentation, now)) { fail("The shared story acknowledgment failed."); return -1; }
    if (!coop_controls_poll(r->controls, now, false)) { fail("Party controls failed during the shared story sequence."); return -1; }
    /* This early guarded timeevent ping updates its real-time anchor. It
     * cannot execute scheduled callbacks while the presentation owns pause. */
    timeevent_ping((tig_timestamp_t)now);
    if (r->gameplay && !coop_gameplay_finished(r->gameplay)) {
        coop_gameplay_set_paused(r->gameplay, true); coop_gameplay_poll(r->gameplay, now);
        if (coop_gameplay_failed(r->gameplay)) { fail(coop_gameplay_error(r->gameplay)); return -1; }
    }
    if (present_observer) present_observer(present_observer_context);
    const CoopPresentView* v = coop_present_current(r->presentation);
    if (v->phase == COOP_PRESENT_FAILED) return -1;
    return v->phase != COOP_PRESENT_PREPARING && (uint8_t)(v->skip | v->done) == v->participants ? 1 : 0;
}
static int movie_pump(void* context) { (void)context; return coop_runtime_present_pump(); }
static void movie_skip(void* context) { (void)context; coop_runtime_present_input(); }
static const char* movie_status(void* context) { (void)context; return coop_tr_rendered(coop_runtime_present_status()); }
static bool present_wait_input(TigMessage* message)
{
    if (message->type == TIG_MESSAGE_KEYBOARD) { if (message->data.keyboard.pressed) coop_runtime_present_input(); return true; }
    if (message->type == TIG_MESSAGE_MOUSE) { if (message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) coop_runtime_present_input(); return true; }
    return message->type == TIG_MESSAGE_TEXT_INPUT;
}
static bool present_launch_rejoin(CoopRuntime* r, const CoopPresentView* original)
{
    /* Before the first snapshot, replicas still have their menu placeholder.
     * Keep the host's native intro caller suspended; synchronize the reserved
     * roster/receivers without trying to save or restore that placeholder. */
    bool host = coop_session_is_host(r->session);
    uint32_t retired = r->epoch;
    uint64_t began = SDL_GetTicks(), retry = began;
    r->present_recovering = true;
    phase(COOP_RUNTIME_RECONNECTING);
    bool good = !host || (retired != UINT32_MAX
        && coop_reconnect_begin(r->reconnect, retired + 1, coop_session_reserved(r->session), began)
        && reconnect_prepare(r, retired + 1));
    tig_window_handle_t waiting = TIG_WINDOW_HANDLE_INVALID;
    TigWindowData data = { .flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER,
        .rect = { 0, 0, 800, 600 }, .message_filter = present_wait_input };
    good = good && tig_window_create(&data, &waiting) == TIG_OK;
    while (good && runtime == r && r->phase != COOP_RUNTIME_FAILED) {
        uint64_t now = SDL_GetTicks();
        if (now - began >= COOP_RECONNECT_GRACE) { good = false; break; }
        coop_session_poll(r->session, now);
        if (coop_session_failed(r->session)) { fail(coop_session_status(r->session)); good = false; break; }
        if (!host && coop_session_phase(r->session) == COOP_OFFLINE) {
            if (!coop_session_can_retry(r->session)) { good = false; break; }
            if (now >= retry) { coop_session_retry(r->session, now); retry = now + 1000; }
        }
        const CoopReconnectView* view = coop_reconnect_current(r->reconnect);
        if (!host && coop_reconnect_active(r->reconnect) && view->epoch > retired) {
            good = reconnect_prepare(r, view->epoch);
            if (!good) break;
        }
        bool prepared = r->reconnect_prepared > retired;
        good = coop_reconnect_poll(r->reconnect, now, prepared);
        if (!good) break;
        if (host && coop_reconnect_ready(r->reconnect)) {
            coop_reconnect_end(r->reconnect);
            r->seen_roster = coop_session_revision(r->session);
            phase(COOP_RUNTIME_PREPARING);
            good = coop_present_begin(r->presentation, original->kind, original->id, original->path,
                original->flags, original->track, now);
            break;
        }
        if (!host && prepared) {
            if (view->phase == COOP_RECONNECT_SYNC) {
                /* The SYNC acknowledgment follows every discarded old frame.
                 * Accept the host's restarted media next, before its snapshot. */
                r->seen_roster = coop_session_revision(r->session);
                phase(COOP_RUNTIME_LOBBY);
                if (coop_present_active(r->presentation)) {
                    coop_reconnect_end(r->reconnect);
                    break;
                }
            } else phase(COOP_RUNTIME_RECONNECTING);
        }
        timeevent_ping((tig_timestamp_t)now);
        tig_window_fill(waiting, NULL, 0);
        TigRect label = { 30, 248, 740, 100 };
        tig_window_text_write(waiting, coop_tr("Waiting for the original party. The opening story will resume when everyone returns."), &label);
        tig_window_display(); tig_ping();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK)
            if (message.type == TIG_MESSAGE_QUIT) good = false;
        SDL_Delay(2);
    }
    if (waiting != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(waiting);
    if (runtime != r) return false;
    r->present_recovering = false;
    return good && r->phase != COOP_RUNTIME_FAILED && coop_present_active(r->presentation);
}
static bool present_rejoin(CoopRuntime* r, const CoopPresentView* original)
{
    if (r->preparing_native || (!r->campaign && r->phase == COOP_RUNTIME_LOBBY))
        return present_launch_rejoin(r, original);
    uint32_t retired = r->epoch;
    r->present_recovering = true;
    tig_window_handle_t waiting = TIG_WINDOW_HANDLE_INVALID;
    TigWindowData data = { .flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER,
        .rect = { 0, 0, 800, 600 }, .message_filter = present_wait_input };
    bool good = tig_window_create(&data, &waiting) == TIG_OK;
    while (good && runtime == r && r->phase != COOP_RUNTIME_FAILED) {
        uint64_t now = SDL_GetTicks();
        coop_session_poll(r->session, now);
        coop_runtime_tick(now);
        if (runtime != r) break;
        /* Resetting the anchor does not run native callbacks while this
         * interrupted script and its presentation still own the pause. */
        timeevent_ping((tig_timestamp_t)now);
        if (r->epoch > retired && r->phase == COOP_RUNTIME_PLAYING && r->gameplay && coop_gameplay_running(r->gameplay)) {
            if (coop_session_is_host(r->session) && !coop_present_current(r->presentation)->sequence)
                good = coop_present_begin(r->presentation, original->kind, original->id, original->path,
                    original->flags, original->track, now);
            if (good && coop_present_active(r->presentation)) break;
        }
        tig_window_fill(waiting, NULL, 0);
        TigRect label = { 30, 248, 740, 100 };
        tig_window_text_write(waiting, coop_tr("Rejoining the party. The shared story will restart when everyone returns."), &label);
        tig_window_display();
        tig_ping();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT) { good = false; break; }
            if (message.type == TIG_MESSAGE_KEYBOARD && message.data.keyboard.pressed && message.data.keyboard.key == SDL_SCANCODE_R)
                coop_runtime_retry_now();
        }
        SDL_Delay(2);
    }
    if (waiting != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(waiting);
    if (runtime != r) return false;
    r->present_recovering = false;
    return good && r->phase != COOP_RUNTIME_FAILED && coop_present_active(r->presentation);
}
static bool present_run(CoopRuntime* r)
{
    if (r->presenting) return false;
    coop_saves_ui_close();
    CoopPresentView initial = *coop_present_current(r->presentation);
restart:;
    uint32_t sequence = initial.sequence;
    tig_window_handle_t waiting = TIG_WINDOW_HANDLE_INVALID;
    r->presenting = true;
    r->present_interrupted = false;
    tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, MM_COLOR_WHITE));
    TigMovieCallbacks callbacks = { .pump = movie_pump, .skip = movie_skip, .status = movie_status };
    tig_movie_callbacks(&callbacks);
    bool success = true;
    while (runtime == r && r->phase != COOP_RUNTIME_FAILED) {
        const CoopPresentView* v = coop_present_current(r->presentation);
        if (v->sequence != sequence || coop_present_settled(r->presentation)) break;
        int state = coop_runtime_present_pump();
        if (r->present_interrupted) break;
        v = coop_present_current(r->presentation);
        if (state < 0) { success = false; break; }
        if (r->prepared_present != sequence && v->phase == COOP_PRESENT_PREPARING) {
            bool ready = true;
            if (v->kind == COOP_PRESENT_MOVIE) { char extracted[TIG_MAX_PATH]; ready = tig_file_extract(v->path, extracted) && tig_movie_probe(extracted, v->track); }
            else if (v->kind == COOP_PRESENT_SLIDE) ready = slide_ui_coop_probe((int)v->id);
            coop_present_ready(r->presentation, ready); r->prepared_present = sequence;
            fprintf(stderr, "ARCMP presentation ready sequence=%u kind=%u success=%d\n", sequence, v->kind, ready);
        }
        if (r->played_present != sequence && v->phase == COOP_PRESENT_PLAYING) {
            if (waiting != TIG_WINDOW_HANDLE_INVALID) { tig_window_destroy(waiting); waiting = TIG_WINDOW_HANDLE_INVALID; }
            r->played_present = sequence;
            bool played = v->kind == COOP_PRESENT_MOVIE ? gmovie_play_local(v->path, v->flags, (int)v->track)
                : v->kind == COOP_PRESENT_SLIDE         ? slide_ui_coop_present((int)v->id)
                : v->kind == COOP_PRESENT_FADE          ? tig_video_fade(tig_color_make((v->id >> 16) & 255, (v->id >> 8) & 255, v->id & 255), (int)(v->flags >> 8), v->track / 1000.0f, v->flags & 1u) == TIG_OK
                                                        : true;
            if (r->present_interrupted) break;
            coop_present_done(r->presentation, played);
            fprintf(stderr, "ARCMP presentation played sequence=%u kind=%u success=%d\n", sequence, v->kind, played);
        }
        if (waiting == TIG_WINDOW_HANDLE_INVALID) {
            TigWindowData data = { .flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER,
                .rect = { 0, 0, 800, 600 }, .message_filter = present_wait_input };
            if (tig_window_create(&data, &waiting) != TIG_OK) { success = false; break; }
        }
        tig_window_fill(waiting, NULL, 0); TigRect label = { 30, 276, 740, 48 };
        tig_window_text_write(waiting, coop_tr_rendered(coop_runtime_present_status()), &label); tig_window_display();
        tig_ping(); TigMessage message; while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT) { success = false; break; }
            present_wait_input(&message);
        }
        if (!success) break;
        SDL_Delay(2);
    }
    if (waiting != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(waiting);
    tig_movie_callbacks(NULL);
    tig_font_pop();
    if (runtime != r) return false;
    if (r->present_interrupted) {
        r->presenting = false;
        if (!present_rejoin(r, &initial)) {
            if (runtime == r && r->phase != COOP_RUNTIME_FAILED) fail("Could not resume the interrupted shared story. Use Recover party.");
            return false;
        }
        initial = *coop_present_current(r->presentation);
        goto restart;
    }
    /* Bring action deadlines to the final paused tick before returning to the
     * caller, which may immediately continue an authoritative native script. */
    if (r->gameplay && !coop_gameplay_failed(r->gameplay)) { coop_gameplay_set_paused(r->gameplay, true); coop_gameplay_poll(r->gameplay, SDL_GetTicks()); }
    timeevent_ping(SDL_GetTicks()); r->presenting = false;
    if (!success || coop_present_current(r->presentation)->phase == COOP_PRESENT_FAILED) {
        if (r->phase != COOP_RUNTIME_FAILED) fail("A traveller could not play the shared story sequence. Check its movie and slide assets.");
        return false;
    }
    if (initial.kind == COOP_PRESENT_END) r->ending_at = SDL_GetTicks() + 500;
    return true;
}
bool coop_runtime_present(CoopPresentKind kind, unsigned id, const char* path, unsigned flags, unsigned track)
{
    CoopRuntime* r = runtime;
    if (!r) return false;
    if (!coop_session_is_host(r->session)) return true;
    tig_window_modal_dialog_cancel();
    if (r->presenting || r->phase == COOP_RUNTIME_FAILED || !coop_present_begin(r->presentation, kind, id, path, flags, track, SDL_GetTicks())) { fail("Could not start the shared story sequence."); return true; }
    present_run(r); return true;
}
bool coop_runtime_fade(const FadeData* fade)
{
    CoopRuntime* r = runtime;
    if (!r || !r->campaign || r->presenting) return false;
    if (!coop_session_is_host(r->session)) return true;
    if (!fade || fade->steps < 0 || fade->steps > 1024 || !isfinite(fade->duration) || fade->duration < 0 || fade->duration > 60 || (fade->flags & ~1u)) {
        fail("The shared scene has invalid fade parameters.");
        return true;
    }
    unsigned rgb = (unsigned)tig_color_get_red(fade->color) << 16 | (unsigned)tig_color_get_green(fade->color) << 8 | tig_color_get_blue(fade->color);
    return coop_runtime_present(COOP_PRESENT_FADE, rgb, NULL, ((unsigned)fade->steps << 8) | fade->flags, (unsigned)(fade->duration * 1000.0f));
}
void coop_runtime_end_campaign(bool defeated)
{
    if (!runtime || !coop_session_is_host(runtime->session)) return;
    slide_ui_start(defeated ? SLIDE_UI_TYPE_DEATH : SLIDE_UI_TYPE_END_GAME);
    if (runtime && runtime->phase != COOP_RUNTIME_FAILED) coop_runtime_present(COOP_PRESENT_END, defeated ? 1 : 0, NULL, 0, 0);
}
static bool bootstrap_message(void* context, uint8_t sender, uint8_t type, const uint8_t* data, size_t size)
{
    CoopRuntime* r = context;
    if (type == COOP_SAVE_QUERY || type == COOP_SAVE_PAGE) return coop_runtime_save_message(sender, type, data, size);
    if (type == COOP_RECONNECT_VIEW || type == COOP_RECONNECT_ACK) return coop_runtime_reconnect_message(sender,type,data,size);
    if (r->phase == COOP_RUNTIME_RECONNECTING) {
        if (size < 4) return false;
        uint32_t epoch = size >= 4 ? coop_object_read32(data) : 0;
        const CoopReconnectView* v = coop_reconnect_current(r->reconnect);
        if (type >= COOP_SNAPSHOT_OFFER && type <= COOP_SNAPSHOT_CANCEL && r->bootstrap && epoch == r->epoch
            && r->reconnect_prepared == r->epoch && v->phase == COOP_RECONNECT_SYNC)
            return coop_bootstrap_message(r->bootstrap,sender,type,data,size);
        /* The two acknowledged recovery views drain old TCP traffic before
         * any fresh bootstrap. This includes epoch-less presentation ACKs. */
        return type >= 16 && type <= COOP_GAME_QUIESCED && (type >= COOP_PRESENT_VIEW || (epoch && epoch <= r->epoch));
    }
    if (type == COOP_PRESENT_VIEW || type == COOP_PRESENT_ACK) return coop_present_message(r->presentation, sender, type, data, size);
    if (type >= COOP_CONTROL_REQUEST && type <= COOP_CONTROL_ACK) return coop_controls_message(r->controls, sender, type, data, size);
    uint32_t epoch = size >= 4 ? ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3] : 0;
    return (epoch && epoch < r->epoch) || (epoch == r->epoch && coop_bootstrap_message(r->bootstrap, sender, type, data, size));
}
static bool create_bootstrap(CoopRuntime* r)
{
    r->bootstrap = coop_bootstrap_create(r->session);
    if (!r->bootstrap) return false;
    coop_session_set_handler(r->session, bootstrap_message, r);
    return true;
}
static bool capture(CoopRuntime* r, uint64_t now)
{
    if (runtime != r || r->phase == COOP_RUNTIME_FAILED) return false;
    size_t size = 0;
    uint8_t* snapshot = coop_engine_snapshot_capture(&size);
    if (!snapshot) return false;
    if (runtime != r || r->phase == COOP_RUNTIME_FAILED) { free(snapshot); return false; }
    r->snapshot = snapshot;
    if (r->operation_pending && r->operation.action == COOP_CONTROL_SAVE
        && !store_slot(r, r->operation.value, r->snapshot, size, r->operation.description)) r->operation_status = COOP_CONTROL_IO;
    /* A synchronous native script remains suspended during media rejoin.
     * Keep the last completed durable checkpoint until that script returns. */
    if (!r->present_recovering) store_checkpoint(r, recovery_path, r->snapshot, size, "Party recovery");
    r->seen_roster = coop_session_revision(r->session);
    /* Native movies, map loading and saving may have yielded for seconds.
     * Transfer deadlines begin after capture, not before that native work. */
    uint64_t captured_at = SDL_GetTicks();
    if (captured_at > now) now = captured_at;
    return coop_bootstrap_begin(r->bootstrap, r->epoch, r->snapshot, size, now);
}

bool coop_runtime_reconnect_message(uint8_t sender,uint8_t type,const uint8_t* bytes,size_t size)
{
    return runtime && coop_reconnect_message(runtime->reconnect,sender,type,bytes,size);
}
bool coop_runtime_retry_now(void)
{
    if (!runtime || runtime->phase != COOP_RUNTIME_RECONNECTING || coop_session_is_host(runtime->session)) return false;
    runtime->retry_at = 0; return true;
}
static bool finish_journey(CoopRuntime* r)
{
    teleport_ping(0);
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) if (r->journey_hidden & (1u<<i))
        object_flags_unset(coop_campaign_object(i),OF_OFF);
    int64_t local=obj_pool_perm_lookup(r->journey_local);
    if (!local || !sub_40DAF0(local)) return false;
    r->journey_hidden=0;
    if (!coop_campaign_validate() || !place_party()) return false;
    int64_t location=obj_field_int64_get(local,OBJ_F_LOCATION);
    scroll_set_center(location); location_origin_set(location);
    return true;
}
static bool reconnect_wait(CoopRuntime* r,uint64_t now)
{
    if (r->phase == COOP_RUNTIME_RECONNECTING) return true;
    const CoopControlView* control = coop_controls_current(r->controls);
    r->saved_pauses = control ? control->paused : 0;
    r->operation_interrupted=control && control->phase!=COOP_CONTROL_IDLE
        && (control->action==COOP_CONTROL_REST || control->action==COOP_CONTROL_SAVE
            || control->action==COOP_CONTROL_LOAD || control->action==COOP_CONTROL_TRAVEL);
    if (r->operation_interrupted) r->interrupted_control=*control;
    bool journey=r->phase==COOP_RUNTIME_JOURNEY;
    phase(COOP_RUNTIME_RECONNECTING);
    /* An interrupted operation keeps its native work so far. Release bed and
     * world-map references before capturing, without replaying elapsed hours. */
    if (coop_session_is_host(r->session)) {
        coop_engine_rest_end(&r->rest); r->rest_done=false;
        if (journey) { wmap_ui_close(); if (!finish_journey(r)) return false; r->journey_done=false; }
    }
    if (!coop_engine_dialogue_preserve()) return false;
    if (coop_session_is_host(r->session)) for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc=coop_campaign_object(i); if (pc) sub_424070(pc,PRIORITY_3,false,true);
    }
    coop_gameplay_destroy(r->gameplay); r->gameplay=NULL;
    coop_bootstrap_destroy(r->bootstrap); r->bootstrap=NULL;
    free(r->snapshot); r->snapshot=NULL;
    free(r->load_snapshot); r->load_snapshot=NULL; r->load_size=0;
    r->operation_pending=false; r->local_bed=(ObjectID){0};
    r->travel_pending=false;
    r->travel_wait_dialogue = false;
    r->dialogue_journey_pending = false;
    r->waiting_since=now; r->retry_at=now+(probe ? probe_retry_delay : 1000); r->reconnect_prepared=0; r->reported_view=0;
    coop_authority_set(coop_session_is_host(r->session) ? COOP_AUTHORITY_HOST : COOP_AUTHORITY_REPLICA);
    coop_session_set_handler(r->session,bootstrap_message,r);
    fprintf(stderr,"ARCMP reconnect waiting epoch=%u host=%d\n",r->epoch,coop_session_is_host(r->session));
    return true;
}
static bool reconnect_prepare(CoopRuntime* r,uint32_t epoch)
{
    if (!epoch) return false;
    if (r->reconnect_prepared == epoch) return true;
    coop_bootstrap_destroy(r->bootstrap); r->bootstrap=NULL;
    coop_controls_destroy(r->controls);
    coop_present_destroy(r->presentation);
    r->epoch=epoch; r->prepared_present=r->played_present=0;
    r->controls=coop_controls_create(r->session,epoch,(uint32_t)map_current_map(),SDL_GetTicks(),control_validate,control_begin,r);
    r->presentation=coop_present_create(r->session);
    if (!r->controls || !r->presentation || !create_bootstrap(r)) return false;
    if (coop_session_is_host(r->session) && !coop_controls_restore_pauses(r->controls,r->saved_pauses)) return false;
    if (coop_session_is_host(r->session) && r->operation_interrupted
        && !coop_controls_restore_interrupted(r->controls,&r->interrupted_control)) return false;
    r->reconnect_prepared=epoch;
    return !coop_session_is_host(r->session) || coop_engine_use_settled();
}
static bool reconnect_tick(CoopRuntime* r,uint64_t now)
{
    const CoopReconnectView* view=coop_reconnect_current(r->reconnect);
    bool host=coop_session_is_host(r->session);
    if (coop_reconnect_active(r->reconnect) && !host && view->epoch >= r->epoch) {
        if (r->phase != COOP_RUNTIME_RECONNECTING && !reconnect_wait(r,now)) { fail("Could not suspend the party's native operation."); return true; }
        if (!reconnect_prepare(r,view->epoch)) { fail("Could not prepare the returning traveller's campaign."); return true; }
    }
    bool eligible=r->campaign && (r->phase==COOP_RUNTIME_PLAYING || r->phase==COOP_RUNTIME_STARTING || r->phase==COOP_RUNTIME_LOADING
        || r->phase==COOP_RUNTIME_DRAINING || r->phase==COOP_RUNTIME_RESTING || r->phase==COOP_RUNTIME_JOURNEY);
    uint8_t reserved=coop_session_reserved(r->session),online=0;
    for (unsigned i=0;i<COOP_MAX_PLAYERS;i++) if (coop_session_players(r->session)[i].connected) online |= (uint8_t)(1u<<i);
    bool changed=coop_session_phase(r->session)!=COOP_LOBBY || (reserved && online!=reserved)
        || (r->seen_roster && coop_session_revision(r->session)!=r->seen_roster);
    if (eligible && changed && !r->presenting && !r->ending_at) {
        if (!host && !coop_session_can_retry(r->session) && coop_session_phase(r->session)==COOP_OFFLINE) return false;
        /* A replica can finish draining and enter the announced destination
         * before the host does. Retire that epoch as well as the live one. */
        uint32_t retiring=r->epoch;
        uint32_t destination=r->gameplay ? coop_gameplay_transition_epoch(r->gameplay) : 0;
        if (destination>retiring) retiring=destination;
        if (!reconnect_wait(r,now)) { fail("Could not release the interrupted native operation. Use Recover party."); return true; }
        if (host) {
            if (retiring==UINT32_MAX || !coop_reconnect_begin(r->reconnect,retiring+1,reserved,now)
                || !reconnect_prepare(r,retiring+1)) { fail("Could not suspend the party safely. Use Recover party."); return true; }
        }
    }
    if (r->phase != COOP_RUNTIME_RECONNECTING) return false;
    if (now-r->waiting_since >= COOP_RECONNECT_GRACE) { fail("A traveller did not return in time. Use Recover party with the original profiles."); return true; }
    if (!host && coop_session_phase(r->session)==COOP_OFFLINE) {
        if (!coop_session_can_retry(r->session)) { fail(coop_session_status(r->session)); return true; }
        if (!r->retry_at || now>=r->retry_at) { coop_session_retry(r->session,now); r->retry_at=now+2000; }
    }
    if (!host && coop_session_phase(r->session)==COOP_LOBBY && !coop_session_locked(r->session)) {
        fail("The host restarted its lobby. Leave and use Recover party with the saved owners."); return true;
    }
    bool prepared=r->reconnect_prepared && r->reconnect_prepared==view->epoch;
    if (!coop_reconnect_poll(r->reconnect,now,prepared)) { fail("Party reconnection timed out. Use Recover party to continue."); return true; }
    unsigned seconds=(unsigned)((COOP_RECONNECT_GRACE-(now-r->waiting_since)+999)/1000);
    unsigned missing=bits(reserved & ~online);
    if (!host && coop_session_phase(r->session)!=COOP_LOBBY)
        snprintf(r->error,sizeof(r->error),"Connection lost. Rejoining your party automatically (%us).",seconds);
    else snprintf(r->error,sizeof(r->error),host ? "Waiting for %u traveller(s). The party is paused (%us). Original profiles can rejoin."
        : "Connected. Waiting for the party to synchronize (%u traveller(s), %us).",missing,seconds);
    if (host && coop_reconnect_ready(r->reconnect)) {
        coop_reconnect_end(r->reconnect);
        if (!capture(r,now)) { fail("Could not capture the running party for reconnection. Use Recover party."); return true; }
        r->loading_at=now; phase(COOP_RUNTIME_LOADING); return false;
    }
    if (!host && r->bootstrap && coop_bootstrap_phase(r->bootstrap)!=COOP_BOOT_IDLE) {
        coop_reconnect_end(r->reconnect); r->seen_roster=coop_session_revision(r->session);
        r->loading_at=now; phase(COOP_RUNTIME_LOADING); return false;
    }
    return true;
}
static const char* begin_travel(CoopRuntime* r, uint64_t now)
{
    r->local_bed = (ObjectID) { 0 };
    r->epoch = coop_gameplay_transition_epoch(r->gameplay);
    /* Release native dialogue references before deleting the old map objects. */
    coop_gameplay_destroy(r->gameplay);
    r->gameplay = NULL;
    r->reported_view = 0;
    r->loading_at = now;
    phase(COOP_RUNTIME_LOADING);
    coop_authority_set(coop_session_is_host(r->session) ? COOP_AUTHORITY_HOST : COOP_AUTHORITY_REPLICA);
    if (!create_bootstrap(r)) return "Could not create the destination transfer.";
    if (!coop_session_is_host(r->session)) return NULL;
    if (r->operation_pending) {
        if (r->operation.action == COOP_CONTROL_TRAVEL) {
            r->journey_started = sub_45A7C0(); r->journey_done = false;
            r->journey_local = obj_get_id(player_get_local_pc_obj()); r->journey_hidden = 0;
            int64_t actor = 0;
            for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
                int64_t pc = coop_campaign_object(i);
                if (!memcmp(obj_get_id(pc).d.g.data, r->operation.actor, 16)) actor = pc;
                if (combat_critter_is_combat_mode_active(pc)) combat_critter_deactivate_combat_mode(pc);
            }
            int64_t destinations[COOP_JOURNEY_WAYPOINTS];
            unsigned count = r->operation.route_count ? r->operation.route_count : 1;
            for (unsigned i = 0; i < count; i++) {
                uint32_t packed = r->operation.route_count ? r->operation.route[i] : r->operation.value;
                destinations[i] = location_make((int64_t)(packed >> 16) * 64 + 32, (int64_t)(packed & 65535) * 64 + 32);
            }
            if (!coop_controls_progress(r->controls, COOP_CONTROL_JOURNEY, 0)) return "Could not announce the party's world journey.";
            if (!actor || !sub_40DAF0(actor) || !wmap_ui_coop_begin_route(destinations, count)) { r->operation_status = COOP_CONTROL_LOCATION; r->journey_done = true; }
            else for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
                int64_t pc = coop_campaign_object(i);
                if (!(obj_field_int32_get(pc, OBJ_F_FLAGS) & OF_OFF)) { object_flags_set(pc, OF_OFF); r->journey_hidden |= (uint8_t)(1u << i); }
            }
            phase(COOP_RUNTIME_JOURNEY); return NULL;
        }
        if (r->operation.action == COOP_CONTROL_REST) {
            int64_t actor = OBJ_HANDLE_NULL;
            for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
                int64_t pc = coop_campaign_object(i);
                if (pc && !memcmp(obj_get_id(pc).d.g.data, r->operation.actor, 16)) actor = pc;
            }
            int64_t bed = r->operation.bed.bytes[0] ? obj_pool_perm_lookup(coop_object_id_to_engine(r->operation.bed)) : 0;
            r->operation_status = coop_engine_rest_begin(&r->rest, actor, bed, r->operation.value, now);
            r->rest_done = r->operation_status != COOP_CONTROL_OK;
            if (!coop_controls_progress(r->controls, COOP_CONTROL_RESTING, 0)) return "Could not announce party resting.";
            phase(COOP_RUNTIME_RESTING);
            return NULL;
        }
        if (r->operation.action == COOP_CONTROL_LOAD) {
            /* Keep the pre-load campaign recoverable even if native loading
             * fails after replacing a map. The durable bundle is independent
             * of the reserved native bootstrap staging files. */
            size_t backup_size = 0; uint8_t* backup = coop_engine_snapshot_capture(&backup_size);
            bool protected = backup && store_checkpoint(r, recovery_path, backup, backup_size, "Before party load"); free(backup);
            if (!protected) r->operation_status = COOP_CONTROL_IO;
            else {
                r->restoring = true;
                bool loaded = coop_engine_snapshot_restore_host(r->load_snapshot, r->load_size, r->owner);
                r->restoring = false;
                if (!loaded) return "Could not load the party checkpoint. The previous campaign remains in Recover party.";
                if (!checkpoint_campaign_matches(&r->load_info)) return "The loaded checkpoint's ownership does not match its header.";
                mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU); sub_5412D0();
            }
            free(r->load_snapshot); r->load_snapshot = NULL; r->load_size = 0;
            if (!coop_controls_progress(r->controls, COOP_CONTROL_LOADING, 0)) return "Could not announce party loading.";
        } else if (!coop_controls_progress(r->controls, COOP_CONTROL_SAVING, 0)) return "Could not announce party saving.";
        return capture(r, now) ? NULL : "Could not prepare the party's shared checkpoint snapshot.";
    }
    if (!r->travel_pending) return "The host has no pending destination.";
    r->travel.obj = obj_pool_perm_lookup(r->traveler);
    if (!r->travel.obj) return "The travelling character is no longer available.";
    r->entry_owner = r->traveler;
    r->executing_travel = true;
    bool moved = teleport_do(&r->travel) && teleport_flush();
    r->executing_travel = false;
    r->travel_pending = false;
    if (!moved || map_current_map() != r->travel.map) return "The host could not load the destination map.";
    if (!coop_campaign_validate()) return "A party character is missing from the destination.";
    if (!place_party()) return "Could not find room for the party at the destination entrance.";
    coop_engine_transition_reset();
    coop_engine_teleport_reset();
    fprintf(stderr, "ARCMP campaign travel epoch=%u map=%d\n", r->epoch, map_current_map());
    return capture(r, now) ? NULL : "Could not prepare the destination's shared save snapshot.";
}
bool coop_runtime_attach(CoopSession* session, const uint8_t owner[COOP_IDENTITY_SIZE])
{
    if (runtime || !session || !coop_identity_valid(owner)) return false;
    CoopRuntime* r = calloc(1, sizeof(*r));
    if (!r) return false;
    r->session = session;
    r->epoch = 1;
    if (!read_content(r->content)) { free(r); return false; }
    r->controls = coop_controls_create(session, r->epoch, (uint32_t)map_current_map(), SDL_GetTicks(), control_validate, control_begin, r);
    if (!r->controls) { free(r); return false; }
    r->presentation = coop_present_create(session);
    r->reconnect = coop_reconnect_create(session);
    r->saves = coop_save_catalog_create(session, read_save, r);
    if (!r->presentation || !r->reconnect || !r->saves || !create_bootstrap(r)) { coop_save_catalog_destroy(r->saves); coop_reconnect_destroy(r->reconnect); coop_present_destroy(r->presentation); coop_controls_destroy(r->controls); free(r); return false; }
    memcpy(r->owner, owner, sizeof(r->owner));
    runtime = r;
    phase(COOP_RUNTIME_LOBBY);
    return true;
}
bool coop_runtime_start(void)
{
    if (!runtime || runtime->phase != COOP_RUNTIME_LOBBY || !coop_session_is_host(runtime->session)
        || !coop_session_all_ready(runtime->session) || coop_campaign_active()) return false;
    runtime->error[0] = 0;
    phase(COOP_RUNTIME_PREPARING);
    coop_session_lock(runtime->session, true);
    runtime->seen_roster = coop_session_revision(runtime->session);
    return true;
}
static bool start_checkpoint(const char* path, const CoopSaveEntry* selected)
{
    CoopRuntime* r = runtime;
    if (!r || r->phase != COOP_RUNTIME_LOBBY || !coop_session_is_host(r->session) || !coop_session_all_ready(r->session) || coop_campaign_active()) return false;
    unsigned status = load_checkpoint_version(r, path, selected != NULL, selected ? selected->fingerprint : 0, &r->load_snapshot, &r->load_size, &r->load_info);
    if (status) {
        snprintf(r->error, sizeof(r->error), "%s", status == COOP_CONTROL_OWNERS ? "Every saved owner must join with their original player profile."
            : status == COOP_CONTROL_CONTENT ? "This checkpoint requires different game content." : status == COOP_CONTROL_NO_CHECKPOINT ? "No party checkpoint exists yet." : "The party checkpoint could not be read or validated.");
        return false;
    }
    r->resume_checkpoint = true; r->error[0] = 0;
    coop_session_lock(r->session, true);
    r->seen_roster = coop_session_revision(r->session);
    phase(COOP_RUNTIME_PREPARING); return true;
}
bool coop_runtime_start_checkpoint(void) { return start_checkpoint(checkpoint_path, NULL); }
bool coop_runtime_recover_checkpoint(void) { return start_checkpoint(recovery_path, NULL); }
static void autosave(CoopRuntime* r, uint64_t now)
{
    uint64_t elapsed = r->autosave_tick && now >= r->autosave_tick ? now - r->autosave_tick : 0;
    r->autosave_tick = now;
    if (probe || !coop_session_is_host(r->session) || !r->campaign || !playing_allowed() || coop_runtime_paused()
        || r->operation_pending || r->travel_pending || r->dialogue_journey_pending) return;
    /* Charge time only while ordinary connected gameplay is advancing. A
     * long snapshot, movie, pause or suspended OS process is not playtime. */
    r->autosave_elapsed += elapsed > 1000 ? 1000 : elapsed;
    if (r->autosave_elapsed < 600000 || coop_controls_pending(r->controls)) return;
    const CoopControlView* control = coop_controls_current(r->controls);
    CoopGameplay* game = r->gameplay;
    if (control->phase != COOP_CONTROL_IDLE || coop_saves_ui_active() || coop_engine_dialogue_pending() || coop_engine_teleport_any()
        || coop_dialogue_current(coop_gameplay_dialogue(game))->active || combat_turn_based_is_active()
        || !coop_dialogue_settled(coop_gameplay_dialogue(game)) || !coop_engine_use_settled()
        || !coop_items_settled(coop_gameplay_items(game)) || !coop_battle_settled(coop_gameplay_battle(game))
        || !coop_magic_settled(coop_gameplay_magic(game)) || !coop_skills_settled(coop_gameplay_skills(game))
        || !coop_services_settled(coop_gameplay_services(game)) || !coop_progression_settled(coop_gameplay_progression(game))
        || coop_progression_editing(coop_gameplay_progression(game))
        || !coop_followers_settled(coop_gameplay_followers(game)) || !coop_notes_settled(coop_gameplay_notes(game))) return;
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc = coop_campaign_object(i);
        if (!pc || (!critter_is_dead(pc) && (!critter_is_active(pc) || combat_critter_is_combat_mode_active(pc)
                || !combat_can_exit_combat_mode(pc)))) return;
    }
    CoopSaveSet party;
    if (!coop_runtime_save_set(&party)) return;
    CoopSaveEntry selected = { 0 }; bool found = false;
    for (unsigned i = 0; i < COOP_SAVE_AUTOSAVES; i++) {
        CoopSaveEntry entry;
        coop_save_inspect(&party, COOP_SAVE_AUTOSAVE_FIRST + i, &entry);
        if (entry.status != COOP_CHECKPOINT_OK && entry.status != COOP_CHECKPOINT_MISSING) continue;
        if (!found || (entry.status == COOP_CHECKPOINT_MISSING && selected.status != COOP_CHECKPOINT_MISSING)
            || (entry.status == selected.status && entry.modified < selected.modified)) { selected = entry; found = true; }
    }
    if (!found) { r->autosave_elapsed = 540000; return; }
    int64_t pc = player_get_local_pc_obj();
    if (!pc) return;
    CoopControlRequest request = { .map = (uint32_t)map_current_map(), .action = COOP_CONTROL_SAVE, .value = selected.slot,
        .checkpoint_present = selected.status == COOP_CHECKPOINT_OK, .checkpoint_fingerprint = selected.fingerprint };
    memcpy(request.actor, obj_get_id(pc).d.g.data, 16);
    snprintf(request.description, sizeof(request.description), "Autosave %u", selected.slot - COOP_SAVE_AUTOSAVE_FIRST + 1);
    if (coop_controls_submit(r->controls, &request)) {
        r->autosave_elapsed = 0;
        fprintf(stderr, "ARCMP party autosave requested slot=%u\n", selected.slot);
    }
}
bool coop_runtime_resume_slot(const CoopSaveEntry* entry)
{
    CoopSaveSet party; char path[COOP_SAVE_PATH];
    return entry && entry->status == COOP_CHECKPOINT_OK && coop_runtime_save_set(&party)
        && coop_save_path(&party, entry->slot, path) && start_checkpoint(path, entry);
}
CoopCheckpointStatus coop_runtime_remove_slot(const CoopSaveEntry* entry)
{
    CoopRuntime* r = runtime; CoopSaveSet party;
    if (!r || !entry || !coop_session_is_host(r->session) || (r->phase != COOP_RUNTIME_LOBBY && r->phase != COOP_RUNTIME_PLAYING)
        || !coop_runtime_save_set(&party)) return COOP_CHECKPOINT_OWNER;
    const CoopControlView* view = coop_controls_current(r->controls);
    if (view->phase != COOP_CONTROL_IDLE || coop_controls_pending(r->controls)) return COOP_CHECKPOINT_IO;
    return coop_save_remove(&party, entry->slot, entry->fingerprint);
}
void coop_runtime_tick(uint64_t now)
{
    CoopRuntime* r = runtime;
    if (!r || r->phase == COOP_RUNTIME_FAILED) return;
    if (coop_session_failed(r->session)) { fail(coop_session_status(r->session)); return; }
    /* Intro media precedes the first snapshot. Bind its locked launch roster
     * once; a later roster change must still interrupt active playback. */
    if (!r->campaign && r->phase == COOP_RUNTIME_LOBBY) {
        if (!coop_session_locked(r->session)) r->seen_roster = 0;
        else if (!r->seen_roster) r->seen_roster = coop_session_revision(r->session);
    }
    autosave(r, now);
    coop_save_catalog_poll(r->saves, now, (r->phase == COOP_RUNTIME_LOBBY || r->phase == COOP_RUNTIME_PLAYING) && !r->presenting && !r->present_recovering);
    if (r->ending_at) {
        if (now >= r->ending_at) { mainmenu_ui_reset(); mainmenu_ui_return_to_title(); gameuilib_wants_mainmenu_set(); }
        return;
    }
    /* An explicit final drain has sealed the campaign. Preserve that terminal
     * state until its owner detaches, even as drained peers close their sockets.
     * Epoch transitions still proceed through their normal bootstrap path. */
    if (r->gameplay && coop_gameplay_finished(r->gameplay) && !coop_gameplay_transition_epoch(r->gameplay)) return;
    if (reconnect_tick(r,now)) return;
    if (coop_session_phase(r->session) == COOP_OFFLINE) {
        fail(coop_session_status(r->session));
        return;
    }
    if (!coop_present_poll(r->presentation, now)) { fail("A traveller disconnected during the shared story sequence."); return; }
    if (coop_present_active(r->presentation) && !r->present_recovering) {
        tig_window_modal_dialog_cancel();
        present_run(r); return;
    }
    bool ready = (r->gameplay && coop_gameplay_running(r->gameplay) && r->phase == COOP_RUNTIME_PLAYING) || r->phase == COOP_RUNTIME_RESTING || r->phase == COOP_RUNTIME_JOURNEY;
    if (!coop_controls_poll(r->controls, now, ready)) { fail("Party control acknowledgment failed; the campaign is frozen."); return; }
    if (r->local_bed.type != OID_TYPE_NULL) {
        if (playing_allowed()) {
            if (now > r->bed_tick) r->bed_wait += now - r->bed_tick;
            int64_t bed = obj_pool_perm_lookup(r->local_bed), pc = player_get_local_pc_obj();
            if (!bed || !pc || !critter_is_active(pc) || r->bed_wait > 30000) r->local_bed = (ObjectID) { 0 };
            else if (object_dist(pc, bed) <= 3) { r->local_bed = (ObjectID) { 0 }; sleep_ui_toggle(bed); }
        }
        r->bed_tick = now;
    }
    if (r->phase == COOP_RUNTIME_PREPARING) {
        /* Native UI/media pumps can re-enter the runtime while this same
         * launch still owns character creation, loading or initial capture. */
        if (r->preparing_native) return;
        /* Native draft validation stays outside the input callback. A refused
         * build returns to the editable lobby before any campaign mutation. */
        if (!r->resume_checkpoint && !coop_campaign_selections_valid(coop_session_players(r->session))) {
            snprintf(r->error, sizeof(r->error), "A party character has invalid creation choices. Choose or create it again before starting.");
            coop_session_lock(r->session, false);
            phase(COOP_RUNTIME_LOBBY);
            return;
        }
        coop_authority_set(COOP_AUTHORITY_HOST);
        r->preparing_native = true;
        r->campaign = true;
        if (r->resume_checkpoint) {
            r->restoring = true;
            bool loaded = coop_engine_snapshot_restore_host(r->load_snapshot, r->load_size, r->owner);
            r->restoring = false; free(r->load_snapshot); r->load_snapshot = NULL; r->load_size = 0;
            if (!loaded) { fail("Could not resume the party checkpoint."); return; }
            if (!checkpoint_campaign_matches(&r->load_info)) { fail("Saved party ownership differs from its checkpoint header."); return; }
            mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU); sub_5412D0(); r->resume_checkpoint = false;
        } else {
            if (!coop_campaign_begin(coop_session_players(r->session))) { fail("Could not create the party's campaign characters."); return; }
            mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
            teleport_ping(0);
            if (!place_party()) { fail("Could not find room for the party at the campaign entrance."); return; }
        }
        if (!capture(r, now)) { fail("Could not prepare the shared campaign."); return; }
        r->preparing_native = false;
        phase(COOP_RUNTIME_LOADING);
    }
    if (r->phase == COOP_RUNTIME_RESTING) {
        if (r->rest_done) {
            if (!coop_campaign_validate()) { fail("A party character became unavailable during rest."); return; }
            r->rest_done = false;
            if (!capture(r, now)) { fail("Could not synchronize the rested party."); return; }
            phase(COOP_RUNTIME_LOADING);
        } else {
            bool complete = false;
            unsigned status = coop_engine_rest_step(&r->rest, now, coop_controls_cancelled(r->controls), &complete);
            if (!coop_controls_progress(r->controls, COOP_CONTROL_RESTING, r->rest.completed)) { fail("Could not publish resting progress."); return; }
            if (complete || status) { r->operation_status = status; coop_engine_rest_end(&r->rest); r->rest_done = true; }
        }
    }
    if (r->phase == COOP_RUNTIME_JOURNEY) {
        if (r->journey_done) {
            if (!finish_journey(r)) { fail("Could not restore the party after the world journey."); return; }
            if (!capture(r, now)) { fail("Could not save the shared world journey destination."); return; }
            phase(COOP_RUNTIME_LOADING);
        } else {
            if (coop_controls_cancelled(r->controls)) { wmap_ui_close(); r->operation_status = COOP_CONTROL_INTERRUPTED; }
            DateTime current = sub_45A7C0();
            uint64_t before = (uint64_t)r->journey_started.days * 86400000 + r->journey_started.milliseconds;
            uint64_t after = (uint64_t)current.days * 86400000 + current.milliseconds;
            unsigned hours = after > before ? (unsigned)((after - before) / 3600000) : 0;
            if (!coop_controls_progress(r->controls, COOP_CONTROL_JOURNEY, hours)) { fail("Could not update the party's world journey."); return; }
            r->journey_done = !wmap_ui_coop_moving();
        }
    }
    if (r->bootstrap) {
        CoopBootstrapPhase state = coop_bootstrap_phase(r->bootstrap);
        if (state == COOP_BOOT_IDLE && r->loading_at && now - r->loading_at >= 30000
            && coop_controls_current(r->controls)->phase != COOP_CONTROL_RESTING && coop_controls_current(r->controls)->phase != COOP_CONTROL_JOURNEY) {
            fail("The host did not begin loading the destination."); return;
        }
        if (state != COOP_BOOT_IDLE) {
            phase(COOP_RUNTIME_LOADING);
            coop_session_lock(r->session, true);
        }
        if (state == COOP_BOOT_RECEIVED) {
            size_t size;
            const uint8_t* data = coop_bootstrap_data(r->bootstrap, &size);
            r->restoring = true;
            r->campaign = true;
            bool restored = coop_engine_snapshot_restore(data, size, r->owner);
            r->restoring = false;
            if (!restored) { fail("Could not load the host's campaign."); return; }
            r->seen_roster=coop_session_revision(r->session);
            const CoopControlView* control = coop_controls_current(r->controls);
            if (control->phase != COOP_CONTROL_IDLE && control->action == COOP_CONTROL_SAVE
                && !store_slot(r, control->value, data, size, control->description)) {
                fail("Could not store this player's party checkpoint. The host retains its checkpoint."); return;
            }
            if (!r->present_recovering) store_checkpoint(r, recovery_path, data, size, "Party recovery");
            mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU);
            sub_5412D0();
            if (!coop_bootstrap_loaded(r->bootstrap)) { fail("Could not acknowledge campaign loading."); return; }
        }
        coop_bootstrap_poll(r->bootstrap, now);
        state = coop_bootstrap_phase(r->bootstrap);
        if (state == COOP_BOOT_FAILED) { fail(coop_bootstrap_error(r->bootstrap)); return; }
        if (state == COOP_BOOT_COMMITTED) {
            coop_bootstrap_destroy(r->bootstrap);
            r->bootstrap = NULL;
            free(r->snapshot);
            r->snapshot = NULL;
            r->gameplay = coop_gameplay_create(r->session, r->epoch);
            r->seen_roster=coop_session_revision(r->session);
            r->waiting_since=0; r->error[0]=0;
            if (!r->gameplay) { fail("Could not start the campaign connection."); return; }
            phase(COOP_RUNTIME_STARTING);
        }
    }
    if (r->gameplay) {
        const CoopControlView* view = coop_controls_current(r->controls);
        bool blocked = r->present_recovering || view->paused || (view->phase != COOP_CONTROL_IDLE && view->phase != COOP_CONTROL_VOTING);
        CoopGameplay* polled = r->gameplay;
        coop_gameplay_set_paused(polled, blocked);
        coop_gameplay_poll(polled, now);
        if (runtime != r || r->gameplay != polled) return;
        if (coop_gameplay_failed(r->gameplay)) { fail(coop_gameplay_error(r->gameplay)); return; }
        if (coop_gameplay_transition_epoch(r->gameplay)) {
            phase(COOP_RUNTIME_DRAINING);
            if (coop_gameplay_finished(r->gameplay)) {
                const char* error = begin_travel(r, now);
                if (error) fail(error);
                return;
            }
        } else if (coop_gameplay_running(r->gameplay)) {
            phase(COOP_RUNTIME_PLAYING);
            hotkey_ui_coop_tick();
            fate_ui_coop_tick();
            coop_engine_teleport_ui_tick();
            coop_engine_options_tick();
            if (coop_session_is_host(r->session) && !r->executing_travel && !r->present_recovering
                && !r->travel_pending && coop_engine_transition_pending()) {
                if (!coop_engine_transition_get(&r->travel, &r->traveler)) {
                    fail("The committed campaign destination could not be restored.");
                    return;
                }
                r->travel_pending = r->travel_wait_dialogue = true;
            }
            if (r->travel_wait_dialogue && !blocked && !coop_engine_dialogue_pending()
                && !coop_dialogue_current(coop_gameplay_dialogue(r->gameplay))->active
                && coop_dialogue_settled(coop_gameplay_dialogue(r->gameplay))) {
                if (!coop_gameplay_transition(r->gameplay, r->epoch + 1)) {
                    fail("Could not gather the party after queued conversations.");
                    return;
                }
                r->travel_wait_dialogue = false;
                phase(COOP_RUNTIME_DRAINING);
            }
            if (coop_session_is_host(r->session) && r->operation_pending) {
                if (!coop_controls_complete(r->controls, r->operation_status, r->epoch, (uint32_t)map_current_map())) { fail("Could not complete the shared party operation."); return; }
                r->operation_pending = false;
            } else if (!coop_controls_context(r->controls, r->epoch, (uint32_t)map_current_map())) { fail("Could not update party control context."); return; }
            if (r->dialogue_journey_pending) {
                if (r->dialogue_journey_epoch != r->epoch || r->dialogue_journey_map != (uint32_t)map_current_map())
                    r->dialogue_journey_pending = false;
                else if (!blocked && !r->travel_pending && !coop_gameplay_finishing(r->gameplay)) {
                    const CoopDialogueView* dialogue = coop_dialogue_current(coop_gameplay_dialogue(r->gameplay));
                    if (dialogue->active) {
                        if (dialogue->mode == 6 && dialogue->chooser == coop_session_local_slot(r->session)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                    } else if (!coop_engine_dialogue_pending() && coop_dialogue_settled(coop_gameplay_dialogue(r->gameplay))) {
                        if (coop_runtime_journey_route(r->dialogue_journey, r->dialogue_journey_count)) r->dialogue_journey_pending = false;
                    }
                }
            }
            if (coop_session_is_host(r->session) && !coop_gameplay_finishing(r->gameplay)) {
                bool defeated = coop_campaign_count() > 0;
                for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) defeated &= critter_is_dead(coop_campaign_object(i));
                if (!defeated) r->defeated_since = 0;
                else {
                    if (!r->defeated_since) r->defeated_since = now;
                    if (now - r->defeated_since >= 5000) { coop_runtime_end_campaign(true); return; }
                }
            }
        }
        report(now);
    }
}
bool coop_runtime_detach(void)
{
    CoopRuntime* r = runtime;
    if (!r) return false;
    coop_saves_ui_close();
    bool campaign = r->campaign;
    hotkey_ui_coop_unbind();
    coop_engine_rest_end(&r->rest);
    /* Native dialogue must release its object references before a map reset. */
    coop_gameplay_destroy(r->gameplay);
    coop_bootstrap_destroy(r->bootstrap);
    coop_controls_destroy(r->controls);
    coop_present_destroy(r->presentation);
    coop_reconnect_destroy(r->reconnect);
    coop_save_catalog_destroy(r->saves);
    present_observer = NULL; present_observer_context = NULL;
    coop_session_lock(r->session, false);
    free(r->snapshot);
    free(r->load_snapshot);
    coop_authority_set(COOP_AUTHORITY_LOCAL);
    runtime = NULL;
    free(r);
    return campaign;
}
void coop_runtime_before_reset(void)
{
    if (!runtime || runtime->restoring || !runtime->campaign) return;
    CoopSession* session = runtime->session;
    coop_runtime_detach();
    coop_session_leave(session);
}
CoopRuntimePhase coop_runtime_phase(void) { return runtime ? runtime->phase : COOP_RUNTIME_OFF; }
const char* coop_runtime_status(void)
{
    switch (coop_runtime_phase()) {
    case COOP_RUNTIME_LOBBY: return runtime->error;
    case COOP_RUNTIME_PREPARING: return "Preparing the party's campaign...";
    case COOP_RUNTIME_DRAINING: return "Gathering the party before travelling...";
    case COOP_RUNTIME_RESTING: return "The party is resting together. Anyone can stop resting.";
    case COOP_RUNTIME_JOURNEY: return "The party is travelling together. Anyone may stop the journey.";
    case COOP_RUNTIME_LOADING: return "Loading the shared campaign. Waiting for every player...";
    case COOP_RUNTIME_STARTING: return "Everyone has loaded. Connecting character controls...";
    case COOP_RUNTIME_PLAYING: return "Campaign connected.";
    case COOP_RUNTIME_FAILED: return runtime->error;
    case COOP_RUNTIME_RECONNECTING: return runtime->error;
    default: return "";
    }
}
bool coop_runtime_blocks_simulation(void)
{
    if (runtime && (runtime->presenting || runtime->present_recovering || runtime->ending_at || coop_present_active(runtime->presentation))) return true;
    CoopRuntimePhase state = coop_runtime_phase();
    if (state == COOP_RUNTIME_RESTING || state == COOP_RUNTIME_JOURNEY) return false;
    if (runtime && runtime->gameplay && coop_gameplay_finishing(runtime->gameplay))
        return (state != COOP_RUNTIME_PLAYING && state != COOP_RUNTIME_DRAINING) || !coop_gameplay_draining_native(runtime->gameplay);
    if (state == COOP_RUNTIME_PLAYING && coop_runtime_paused()) return true;
    return state != COOP_RUNTIME_OFF && state != COOP_RUNTIME_LOBBY && state != COOP_RUNTIME_PLAYING;
}
CoopGameplay* coop_runtime_gameplay(void) { return runtime ? runtime->gameplay : NULL; }
bool coop_runtime_move(int64_t location, bool run)
{
    return coop_runtime_move_route(&location, 1, run);
}
bool coop_runtime_move_route(const int64_t* locations, unsigned count, bool run)
{
    if (runtime) runtime->local_bed = (ObjectID) { 0 };
    return playing_allowed() && coop_gameplay_move_route(runtime->gameplay, locations, count, run);
}
bool coop_runtime_journey(int64_t destination)
{
    return coop_runtime_journey_route(&destination, 1);
}
bool coop_runtime_journey_route(const int64_t* destinations, unsigned count)
{
    if (!destinations || !count || count > COOP_JOURNEY_WAYPOINTS) return false;
    uint32_t route[COOP_JOURNEY_WAYPOINTS] = { 0 };
    for (unsigned i = 0; i < count; i++) {
        if (location_get_x(destinations[i]) < 0 || location_get_y(destinations[i]) < 0) return false;
        int64_t sector = sector_id_from_loc(destinations[i]), x = SECTOR_X(sector), y = SECTOR_Y(sector);
        if (x > 65535 || y > 65535) return false;
        route[i] = (uint32_t)((x << 16) | y);
        if (i && route[i] == route[i - 1]) return false;
    }
    if (runtime && runtime->gameplay && playing_allowed()) {
        const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(runtime->gameplay));
        if (view->active && view->mode == 6 && view->chooser == coop_session_local_slot(runtime->session)) {
            if (runtime->dialogue_journey_pending) return false;
            memcpy(runtime->dialogue_journey, destinations, count * sizeof(*destinations));
            runtime->dialogue_journey_count = count;
            runtime->dialogue_journey_pending = true;
            runtime->dialogue_journey_epoch = runtime->epoch;
            runtime->dialogue_journey_map = (uint32_t)map_current_map();
            coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            return true;
        }
    }
    return control_route(COOP_CONTROL_TRAVEL, route[count - 1], 0, 0, route, count);
}
bool coop_runtime_bed(int64_t bed)
{
    if (!playing_allowed() || !bed || !obj_handle_is_valid(bed) || obj_field_int32_get(bed, OBJ_F_TYPE) != OBJ_TYPE_SCENERY
        || tig_art_scenery_id_type_get(obj_field_int32_get(bed, OBJ_F_CURRENT_AID)) != TIG_ART_SCENERY_TYPE_BEDS) return false;
    int64_t pc = player_get_local_pc_obj();
    if (!pc || !critter_is_active(pc)) return false;
    if (object_dist(pc, bed) <= 3) { sleep_ui_toggle(bed); return true; }
    int64_t origin = obj_field_int64_get(pc, OBJ_F_LOCATION), target = obj_field_int64_get(bed, OBJ_F_LOCATION), max_x, max_y;
    location_limits_get(&max_x, &max_y);
    for (int radius = 1; radius <= 2; radius++) for (int dy = -radius; dy <= radius; dy++) for (int dx = -radius; dx <= radius; dx++) {
        if (abs(dx) != radius && abs(dy) != radius) continue;
        int64_t x = location_get_x(target) + dx, y = location_get_y(target) + dy;
        if (x < 0 || y < 0 || x >= max_x || y >= max_y) continue;
        int8_t rotations[256]; int64_t destination = location_make(x, y);
        PathCreateInfo path = { .obj = pc, .from = origin, .to = destination, .max_rotations = SDL_arraysize(rotations), .rotations = rotations };
        if (path_make(&path) > 0 && coop_gameplay_move(runtime->gameplay, destination, false)) {
            runtime->local_bed = obj_get_id(bed); runtime->bed_wait = 0; runtime->bed_tick = SDL_GetTicks(); return true;
        }
    }
    intgame_message_window_display_str(-1, "There is no clear path to that bed."); return false;
}
bool coop_runtime_talk(int64_t npc)
{
    if (runtime) runtime->local_bed = (ObjectID) { 0 };
    return playing_allowed() && coop_gameplay_talk(runtime->gameplay, npc);
}
bool coop_runtime_halt(int64_t towards)
{
    if (runtime) runtime->local_bed = (ObjectID) { 0 };
    return playing_allowed() && coop_gameplay_halt(runtime->gameplay, towards);
}
bool coop_runtime_combat(CoopCombatOperation operation, uint8_t value, int64_t target)
{
    return playing_allowed() && coop_gameplay_combat(runtime->gameplay, operation, value, target);
}
bool coop_runtime_cast(int64_t source, int spell, const TargetDescriptor* target, bool force)
{
    return playing_allowed() && coop_gameplay_cast(runtime->gameplay, source, spell, target, force);
}
bool coop_runtime_cancel_spell(uint64_t token)
{
    return playing_allowed() && coop_gameplay_cancel_spell(runtime->gameplay, token);
}
bool coop_runtime_use_item(int64_t item, const TargetDescriptor* target, bool force)
{
    return playing_allowed() && coop_gameplay_use_item(runtime->gameplay, item, target, force);
}
bool coop_runtime_written(int64_t reader, unsigned type, int number, bool vendigroth)
{
    if (!runtime || !runtime->gameplay) return false;
    coop_gameplay_written(runtime->gameplay, reader, type, number, vendigroth);
    return true;
}
bool coop_runtime_skill(CoopSkillAction action, uint8_t value, int64_t target)
{
    return playing_allowed() && coop_gameplay_skill(runtime->gameplay, action, value, target);
}
void coop_runtime_progression_editor(bool open)
{
    if (runtime && runtime->gameplay) coop_progression_editor(coop_gameplay_progression(runtime->gameplay), open);
}
bool coop_runtime_progression_change(int64_t actor, CoopProgressionKind kind, unsigned index, bool increase)
{
    if (!runtime || !runtime->gameplay) return false;
    if (actor != player_get_local_pc_obj() || !playing_allowed()
        || !coop_progression_change(coop_gameplay_progression(runtime->gameplay), kind, index, increase))
        intgame_message_window_display_str(-1, "Wait for your character sheet to finish updating.");
    return true;
}
bool coop_runtime_service_item(int64_t item, bool buy)
{
    return coop_runtime_service_quantity(item, 1, buy);
}
bool coop_runtime_service_quantity(int64_t item, unsigned quantity, bool buy)
{
    if (!runtime || !runtime->gameplay) return false;
    if (playing_allowed()) {
        CoopServices* s = coop_gameplay_services(runtime->gameplay);
        if (buy) coop_services_buy_quantity(s, item, quantity);
        else coop_services_hover_quantity(s, item, quantity);
    }
    return true;
}
bool coop_runtime_craft(unsigned recipe, int64_t crafter)
{
    if (!runtime || !runtime->gameplay) return false;
    if (!playing_allowed() || !coop_services_craft(coop_gameplay_services(runtime->gameplay), crafter, recipe))
        intgame_message_window_display_str(-1, "Wait for the current action to finish before combining items.");
    return true;
}
void coop_runtime_service_close(void)
{
    if (runtime && runtime->gameplay) coop_services_close(coop_gameplay_services(runtime->gameplay));
}
bool coop_runtime_skill_feedback(int64_t actor, unsigned message, bool red)
{
    if (actor && obj_field_int32_get(actor, OBJ_F_TYPE) == OBJ_TYPE_NPC) actor = critter_pc_leader_get(actor);
    if (!runtime || !runtime->gameplay || coop_campaign_slot_for_object(actor) < 0) return false;
    coop_gameplay_skill_feedback(runtime->gameplay, actor, message, red, 0);
    return true;
}
bool coop_runtime_trap_feedback(int64_t actor)
{
    if (!runtime || !runtime->gameplay || coop_campaign_slot_for_object(actor) < 0) return false;
    coop_gameplay_skill_feedback(runtime->gameplay, actor, 0, false, 1);
    return true;
}
bool coop_runtime_native_feedback(int64_t actor, unsigned catalogue, unsigned message)
{
    if (actor && obj_handle_is_valid(actor) && obj_field_int32_get(actor, OBJ_F_TYPE) == OBJ_TYPE_NPC) actor = critter_pc_leader_get(actor);
    if (!runtime || !runtime->gameplay || !coop_authority_can_simulate()
        || coop_campaign_slot_for_object(actor) < 0 || catalogue < 2 || catalogue > 6) return false;
    /* Queue refusal belongs to the campaign too: the existing feedback
     * controller freezes instead of showing another owner's result locally. */
    coop_gameplay_skill_feedback(runtime->gameplay, actor, message, catalogue != 4, catalogue);
    return true;
}
int coop_runtime_defer_teleport(const TeleportData* data)
{
    CoopRuntime* r = runtime;
    if (!r) return 0;
    if (r->phase == COOP_RUNTIME_FAILED) return -1;
    if (r->executing_travel) return 0;
    bool resting = r->operation_pending && r->operation.action == COOP_CONTROL_REST
        && (r->phase == COOP_RUNTIME_LOADING || r->phase == COOP_RUNTIME_RESTING);
    if (!r->gameplay && !resting) return 0;
    int destination_map = data->map == -1 ? map_current_map() : data->map;
    if (destination_map == map_current_map() && !(data->flags & TELEPORT_COOP_PARTY)) return 0;
    if (coop_campaign_slot_for_object(data->obj) < 0) return 0;
    char* name;
    if (!coop_session_is_host(r->session) || (r->phase != COOP_RUNTIME_PLAYING && !resting) || r->travel_pending || r->epoch == UINT32_MAX
        || !map_get_name(destination_map, &name)) return -1;
    TeleportData committed = *data;
    committed.map = destination_map;
    if (!coop_engine_transition_set(&committed, obj_get_id(data->obj))) return -1;
    /* Sleep scripts can commit a destination after the old action controller
     * has drained. Finish that rest interruption and publish its checkpoint
     * before a new controller carries out the saved party transition. */
    bool waiting = resting || coop_engine_dialogue_pending();
    if (!waiting && !coop_gameplay_transition(r->gameplay, r->epoch + 1)) { coop_engine_transition_reset(); return -1; }
    r->travel = *data;
    r->travel.map = destination_map;
    r->traveler = obj_get_id(data->obj);
    r->travel_pending = true;
    r->travel_wait_dialogue = waiting;
    if (!waiting) phase(COOP_RUNTIME_DRAINING);
    return 1;
}
uint32_t coop_runtime_epoch(void) { return runtime ? runtime->epoch : 0; }
bool coop_runtime_item(CoopItemOperation op, int64_t item, uint8_t recipient, int location)
{
    return playing_allowed() && coop_gameplay_item(runtime->gameplay, op, item, recipient, location);
}
bool coop_runtime_world_use(int64_t target)
{
    return coop_runtime_item(COOP_WORLD_USE, target, 0, -1);
}
bool coop_runtime_world_examine(int64_t target)
{
    /* Native hover/health refreshes can inspect the same target repeatedly.
     * Keep the normal description immediate while bounding script requests. */
    static ObjectID last;
    static uint64_t at;
    static uint32_t epoch;
    if (!runtime || !target) return false;
    ObjectID id = obj_get_id(target);
    uint64_t now = SDL_GetTicks();
    if (epoch == runtime->epoch && objid_is_equal(last, id) && now - at < 500) return false;
    if (!coop_runtime_item(COOP_WORLD_EXAMINE, target, 0, -1)) return false;
    last = id;
    at = now;
    epoch = runtime->epoch;
    return true;
}
bool coop_runtime_native_world_used(int64_t actor, int64_t target, const AnimID* goal)
{
    if (!coop_authority_can_simulate()) return false;
    return !runtime || !runtime->gameplay
        || coop_items_native_world_used(coop_gameplay_items(runtime->gameplay), actor, target, goal);
}
bool coop_runtime_native_world_allowed(int64_t actor, int64_t target, const AnimID* goal)
{
    if (!coop_authority_can_simulate()) return false;
    return !runtime || !runtime->gameplay
        || coop_items_native_world_allowed(coop_gameplay_items(runtime->gameplay), actor, target, goal);
}
bool coop_runtime_native_talk_allowed(int64_t actor, int64_t target, const AnimID* goal)
{
    if (!coop_authority_can_simulate()) return false;
    return !runtime || !runtime->gameplay || coop_gameplay_native_talk_allowed(runtime->gameplay, actor, target, goal);
}
bool coop_runtime_native_loot(int64_t pc, int64_t target)
{
    if (!runtime || !runtime->gameplay) return false;
    coop_items_native_loot(coop_gameplay_items(runtime->gameplay), pc, target);
    return true;
}

bool coop_runtime_follower_order(int64_t follower, CoopFollowerOrder order, int64_t target, int64_t location, bool force)
{
    if (!runtime) return false;
    if (!playing_allowed() || !coop_followers_order(coop_gameplay_followers(runtime->gameplay), follower, order, target, location, force))
        intgame_message_window_display_str(-1, "Companion orders are unavailable or a previous order is still pending.");
    return true;
}
bool coop_runtime_broadcast(int64_t actor, const char* command, int64_t target, int64_t location)
{
    if (!runtime) return false;
    if (actor != player_get_local_pc_obj() || !playing_allowed()
        || !coop_followers_broadcast(coop_gameplay_followers(runtime->gameplay), command, target, location))
        intgame_message_window_display_str(-1, "Wait for your current companion order to finish.");
    return true;
}
