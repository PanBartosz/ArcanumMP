#include "coop/modern.h"
#include "coop/engine_probe.h"
#include "coop/authority.h"
#include "coop/battle_probe.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/continuity_probe.h"
#include "coop/controls_probe.h"
#include "coop/documents.h"
#include "coop/encounter_probe.h"
#include "coop/engine_character.h"
#include "coop/engine_combat.h"
#include "coop/engine_dialogue.h"
#include "coop/language_engine.h"
#include "coop/engine_object.h"
#include "coop/engine_sector.h"
#include "coop/engine_rest.h"
#include "coop/engine_use.h"
#include "coop/social.h"
#include "coop/engine_snapshot.h"
#include "coop/engine_story.h"
#include "coop/environment_probe.h"
#include "coop/gameplay.h"
#include "coop/reconnect_probe.h"
#include "coop/services_probe.h"
#include "coop/starting_shop.h"
#include "coop/story_probe.h"
#include "coop/training_probe.h"
bool coop_sector_probe(int argc, char** argv);
bool coop_sustained_probe(int argc, char** argv);
bool coop_ordinary_probe(int argc, char** argv);
void coop_ordinary_probe_cleanup(void);
void coop_sector_probe_cleanup(void);
bool coop_campaign_progress_probe(int argc, char** argv);
void coop_campaign_progress_probe_cleanup(void);
#include "coop/lobby_ui.h"
#include "coop/runtime.h"
#include "coop/runtime_probe.h"
#include "coop/world.h"
#include "game/sector.h"
#include "game/anim.h"
#include "game/ai.h"
#include "game/area.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/description.h"
#include "game/descriptions.h"
#include "game/dialog.h"
#include "game/gamelib.h"
#include "game/gsound.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/magictech.h"
#include "game/monstergen.h"
#include "game/mt_obj_node.h"
#include "game/newspaper.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/obj_private.h"
#include "game/object.h"
#include "game/party.h"
#include "game/path.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/quest.h"
#include "game/rumor.h"
#include "game/reputation.h"
#include "game/script.h"
#include "game/spell.h"
#include "game/scroll.h"
#include "game/stat.h"
#include "game/skill.h"
#include "game/teleport.h"
#include "game/timeevent.h"
#include "game/tile.h"
#include "ui/gameuilib.h"
#include "ui/anim_ui.h"
#include "ui/dialog_ui.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/logbook_ui.h"
#include "ui/mainmenu_ui.h"
#include "tig/file_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CoopGameplay* held_gameplay;
static CoopSession* held_session;

static bool fail(const char* message)
{
    fprintf(stderr, "ARCMP engine probe FAILED: %s\n", message);
    return false;
}

static bool assemble_party_roster(bool fighters, unsigned mask)
{
    CoopSession* sessions[COOP_MAX_PLAYERS] = { 0 };
    bool good = true;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        CoopConfig config = { 0 };
        snprintf(config.name, sizeof(config.name), "Engine player %u", i + 1);
        sessions[i] = coop_session_create(&config);
        if (!sessions[i]) {
            good = false;
            break;
        }
    }
    uint64_t start = SDL_GetTicks();
    if (good) good = coop_session_host(sessions[0], 0, start);
    for (unsigned i = 1; good && i < COOP_MAX_PLAYERS; i++) {
        good = coop_session_join(sessions[i], "127.0.0.1", coop_session_port(sessions[0]), start);
    }
    bool selected[COOP_MAX_PLAYERS] = { 0 };
    while (good && !coop_session_all_ready(sessions[0]) && SDL_GetTicks() - start < 5000) {
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            coop_session_poll(sessions[i], SDL_GetTicks());
            if (coop_session_phase(sessions[i]) != COOP_LOBBY) continue;
            uint8_t slot = coop_session_local_slot(sessions[i]);
            if (!selected[i]) {
                if (fighters) {
                    CoopCharacter build;
                    selected[i] = coop_encounter_character(i + 1, &build)
                        && coop_session_custom_character(sessions[i], &build);
                } else {
                    char name[COOP_NAME_SIZE];
                    snprintf(name, sizeof(name), "Campaign hero %u", i + 1);
                    selected[i] = coop_session_character(sessions[i], 16067 + i, name);
                }
            } else if (coop_session_players(sessions[i])[slot].prototype) {
                coop_session_ready(sessions[i], true);
            }
        }
        SDL_Delay(1);
    }
    if (good && mask != 15) {
        /* Real lobby departures preserve the remaining connection slots. */
        for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++)
            if (!(mask & (1u << coop_session_local_slot(sessions[i])))) coop_session_leave(sessions[i]);
        uint64_t deadline = SDL_GetTicks() + 5000;
        bool ready = false;
        while (!ready && SDL_GetTicks() < deadline) {
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                coop_session_poll(sessions[i], SDL_GetTicks());
                if (coop_session_phase(sessions[i]) == COOP_LOBBY) coop_session_ready(sessions[i], true);
            }
            unsigned present = 0;
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
                if (coop_session_players(sessions[0])[i].connected) present |= 1u << i;
            ready = present == mask && coop_session_all_ready(sessions[0]);
            SDL_Delay(1);
        }
        good = ready;
    }
    if (good) good = coop_session_all_ready(sessions[0])
        && coop_campaign_begin(coop_session_players(sessions[0]));
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        coop_session_destroy(sessions[i]);
    return good || fail("live four-session lobby could not create the campaign party");
}

static bool assemble_party_setup(bool fighters)
{
    return assemble_party_roster(fighters, 15);
}

static bool assemble_party(void)
{
    return assemble_party_setup(false);
}

static bool verify_party(void)
{
    if (coop_campaign_count() != 4 || !coop_campaign_validate()) return fail("four valid permanent player objects required");
    int iter;
    unsigned count = 0;
    int64_t member = party_find_first(player_get_local_pc_obj(), &iter);
    while (member) {
        if (++count > COOP_MAX_PLAYERS) return fail("party traversal repeats a member");
        if (!critter_party_same(player_get_local_pc_obj(), member)) return fail("campaign PC is not an ally");
        member = party_find_next(&iter);
    }
    if (count != 4) return fail("party traversal omits an owned PC");
    ObjectID items[1024];
    unsigned item_count = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t obj = coop_campaign_object(i);
        if (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT)) return fail("owned PC was disabled or destroyed");
        if (stat_base_get(obj, STAT_STRENGTH) != 11 + (int)i) return fail("independent character stat did not persist");
        const CoopCampaignMember* entry = coop_campaign_member(i);
        if (coop_campaign_slot_for_owner(entry->owner) != (int)i) return fail("identity binding changed");
        int inventory = obj_field_int32_get(obj, OBJ_F_CRITTER_INVENTORY_NUM);
        if (inventory != obj_arrayfield_length_get(obj, OBJ_F_CRITTER_INVENTORY_LIST_IDX)) return fail("inventory count differs from array");
        for (int index = 0; index < inventory; index++) {
            int64_t item = obj_arrayfield_handle_get(obj, OBJ_F_CRITTER_INVENTORY_LIST_IDX, index);
            if (!item || obj_field_handle_get(item, OBJ_F_ITEM_PARENT) != obj) return fail("item has the wrong owner");
            ObjectID id = obj_get_id(item);
            for (unsigned j = 0; j < item_count; j++)
                if (objid_is_equal(items[j], id)) return fail("PCs share an item instance");
            if (item_count >= SDL_arraysize(items)) return fail("inventory audit capacity exceeded");
            items[item_count++] = id;
        }
    }
    return true;
}

static bool verify_object_fields(void)
{
    int64_t object;
    int iterator;
    unsigned count = 0;
    size_t total = 0;
    if (obj_inst_first(&object, &iterator)) do {
            size_t size = 0, second_size = 0;
            uint8_t* before = coop_engine_object_export(object, &size);
            if (!before) {
                char id[OID_STR_LEN];
                objid_id_to_str(id, obj_get_id(object));
                fprintf(stderr, "ARCMP object export failed: %s type=%d\n", id, obj_field_int32_get(object, OBJ_F_TYPE));
                return false;
            }
            CoopStagedObject* stage = coop_engine_object_stage(before, size);
            bool good = stage && coop_engine_object_matches(stage, object) && coop_engine_object_resolve(stage);
            if (good) coop_engine_object_install(stage, object);
            coop_engine_object_discard(stage);
            uint8_t* after = good ? coop_engine_object_export(object, &second_size) : NULL;
            good = good && after && size == second_size && !memcmp(before, after, size);
            if (coop_engine_object_stage(before, size - 1)) good = false;
            free(before);
            free(after);
            if (!good) {
                char id[OID_STR_LEN];
                objid_id_to_str(id, obj_get_id(object));
                fprintf(stderr, "ARCMP object roundtrip mismatch %s staged=%d before=%zu after=%zu\n", id, stage != NULL, size, second_size);
                return fail("portable object fields did not roundtrip exactly");
            }
            count++;
            total += size;
        } while (obj_inst_next(&object, &iterator));
    fprintf(stderr, "ARCMP portable object roundtrip PASS count=%u bytes=%zu\n", count, total);
    return count >= 21 && verify_party();
}

static bool verify_pool_restoration(void)
{
    TigGuid first_guid, second_guid;
    srand(24601);
    tig_guid_create(&first_guid);
    srand(24601);
    tig_guid_create(&second_guid);
    if (tig_guid_is_equal(&first_guid, &second_guid)) return fail("object identities depend on the gameplay random seed");
    int64_t source = item_wield_get(player_get_local_pc_obj(), ITEM_INV_LOC_WEAPON);
    if (!source) return fail("pool fixture requires an equipped weapon");
    int64_t prototype = obj_field_handle_get(source, OBJ_F_PROTOTYPE_HANDLE);
    int64_t location = obj_field_int64_get(source, OBJ_F_LOCATION);
    int64_t handles[3];
    for (unsigned i = 0; i < 3; i++)
        obj_create_inst(prototype, location, &handles[i]);
    ObjectID first_id = obj_get_id(handles[0]);
    obj_deallocate(handles[0]);
    obj_deallocate(handles[2]);
    bool good = obj_handle_request(handles[0]);
    int64_t restored = 0, next = 0;
    if (good) {
        obj_create_inst_with_oid(prototype, location, first_id, &restored);
        obj_field_int32_set(restored, OBJ_F_ITEM_WORTH, 765432);
        Script probe_script = { .num = 1 };
        obj_arrayfield_script_set(restored, OBJ_F_SCRIPTS_IDX, SAP_USE, &probe_script);
        script_local_flag_set(restored, SAP_USE, 31, true);
        script_local_counter_set(restored, SAP_USE, 3, 255);
        good = script_local_flag_get(restored, SAP_USE, 31) && script_local_counter_get(restored, SAP_USE, 3) == 255;
        script_local_flag_set(restored, SAP_USE, 31, false);
        script_local_counter_set(restored, SAP_USE, 3, 0);
        good = good && !script_local_flag_get(restored, SAP_USE, 31) && !script_local_counter_get(restored, SAP_USE, 3);
        obj_create_inst(prototype, location, &next);
        good = good && restored == handles[0] && next != restored && obj_handle_is_valid(restored)
            && objid_is_equal(obj_get_id(restored), first_id)
            && obj_pool_perm_lookup(first_id) == restored
            && obj_field_int32_get(restored, OBJ_F_ITEM_WORTH) == 765432
            && obj_handle_is_valid(handles[1]);
    }
    if (next) obj_deallocate(next);
    if (restored) obj_deallocate(restored);
    obj_deallocate(handles[1]);
    fprintf(stderr, "ARCMP out-of-order pool restoration %s\n", good ? "PASS" : "FAIL");
    return good || fail("restored handle was reused by a new allocation");
}

static bool verify_story_fields(void)
{
    CoopStoryState before, changed, restored;
    if (!coop_engine_story_capture(&before)) return fail("could not capture campaign subsystem state");
    changed = before;
    changed.variables[1999] = INT32_MIN;
    changed.flags[99] = UINT32_C(0x80000001);
    changed.story = 42;
    changed.quests[999] = QUEST_STATE_BOTCHED;
    changed.rumors[249] ^= 0x80;
    changed.generators[255] = 0x9f;
    changed.newspapers[24] = 1999;
    changed.newspaper_count = 25;
    if (changed.area_count) changed.areas[changed.area_count - 1] ^= 1;
    changed.game_time = (CoopStoryClock) { 13, 86399999 };
    changed.animation_time = (CoopStoryClock) { 14, 1 };
    CoopAuthority authority = coop_authority_get();
    coop_authority_set(COOP_AUTHORITY_REPLICA);
    bool discovery_denied = true;
    for (unsigned i = 0; i < before.area_count; i++) {
        if (!before.areas[i]) {
            discovery_denied = !area_set_known(player_get_local_pc_obj(), (int)i)
                && !area_is_known(player_get_local_pc_obj(), (int)i);
            break;
        }
    }
    bool good = coop_engine_story_install(&changed) && discovery_denied && coop_engine_story_capture(&restored);
    size_t expected_size = 0, actual_size = 0;
    uint8_t* expected = coop_story_state_encode(&changed, &expected_size);
    uint8_t* actual = good ? coop_story_state_encode(&restored, &actual_size) : NULL;
    good = good && expected && actual && expected_size == actual_size && !memcmp(expected, actual, actual_size);
    free(expected);
    free(actual);
    bool installed = coop_engine_story_install(&before);
    good = good && installed && coop_engine_story_capture(&restored);
    expected = coop_story_state_encode(&before, &expected_size);
    actual = good ? coop_story_state_encode(&restored, &actual_size) : NULL;
    good = good && expected && actual && expected_size == actual_size && !memcmp(expected, actual, actual_size);
    free(expected);
    free(actual);
    coop_authority_set(authority);
    fprintf(stderr, "ARCMP campaign subsystem restoration %s bytes=%zu areas=%u\n", good ? "PASS" : "FAIL", actual_size, before.area_count);
    return good || fail("campaign subsystem restoration failed");
}

static bool verify_carried_coordinates(void)
{
    int64_t item = item_wield_get(player_get_local_pc_obj(), ITEM_INV_LOC_WEAPON);
    if (!item) return false;
    unsigned flags = (unsigned)obj_field_int32_get(item, OBJ_F_FLAGS);
    int64_t original = obj_field_int64_get(item, OBJ_F_LOCATION);
    int ox = obj_field_int32_get(item, OBJ_F_OFFSET_X), oy = obj_field_int32_get(item, OBJ_F_OFFSET_Y);
    int64_t lx, ly;
    location_limits_get(&lx, &ly);
    int64_t stale = location_make(lx + 100, ly + 200);
    bool good = (flags & OF_INVENTORY) != 0;
    obj_field_int64_set(item, OBJ_F_LOCATION, stale);
    obj_field_int32_set(item, OBJ_F_OFFSET_X, 21);
    obj_field_int32_set(item, OBJ_F_OFFSET_Y, -7);
    for (unsigned carried = 0; carried < 2; carried++) {
        obj_field_int32_set(item, OBJ_F_FLAGS, (int)(carried ? flags : flags & ~OF_INVENTORY));
        size_t size = 0;
        uint8_t* bytes = coop_engine_object_export(item, &size);
        CoopStagedObject* state = bytes ? coop_engine_object_stage(bytes, size) : NULL;
        int x = 0, y = 0;
        int64_t loc = state ? coop_engine_object_location(state, &x, &y) : -1;
        good &= state && loc == (carried ? 0 : stale) && x == (carried ? 0 : 21) && y == (carried ? 0 : -7)
            && obj_field_int64_get(item, OBJ_F_LOCATION) == stale;
        coop_engine_object_discard(state);
        free(bytes);
    }
    obj_field_int32_set(item, OBJ_F_FLAGS, (int)flags);
    obj_field_int64_set(item, OBJ_F_LOCATION, original);
    obj_field_int32_set(item, OBJ_F_OFFSET_X, ox);
    obj_field_int32_set(item, OBJ_F_OFFSET_Y, oy);
    fprintf(stderr, "ARCMP carried coordinates %s cases=2 native_position_preserved=1\n", good ? "PASS" : "FAIL");
    return good;
}

static bool inspect_sector_ids(int argc, char** argv)
{
    unsigned x = 0, y = 0;
    bool selected = false;
    for (int i = 1; i < argc; i++)
        if (sscanf(argv[i], "--coop-probe-sector=%u,%u", &x, &y) == 2) selected = true;
    if (!selected || x >= 0x4000000 || y >= 0x4000000) return false;
    coop_authority_set(COOP_AUTHORITY_HOST);
    if (!gamelib_load("arcmp-bootstrap")) return false;
    Sector* sector;
    int64_t id = SECTOR_MAKE(x, y);
    if (!sector_lock(id, &sector)) return false;
    fprintf(stderr, "ARCMP sector identity inspection sector=%u,%u native_count=%d\n", x, y, sector->objects.next_temp_id);
    for (unsigned tile = 0; tile < 4096; tile++)
        for (ObjectNode* node = sector->objects.heads[tile]; node; node = node->next) {
            int64_t object = node->obj;
            if (!object_is_static(object)) continue;
            char name[OID_STR_LEN];
            objid_id_to_str(name, obj_get_id(object));
            fprintf(stderr, "ARCMP sector static temp=%d oid=%s flags=%x proto=%d tile=%u\n",
                obj_field_int32_get(object, OBJ_F_TEMP_ID), name,
                obj_field_int32_get(object, OBJ_F_FLAGS), obj_field_int32_get(object, OBJ_F_NAME), tile);
        }
    sector_unlock(id);
    return true;
}

static bool verify_hidden_static_ids(void)
{
    int64_t object;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            ObjectID id = obj_get_id(object);
            if (id.type != OID_TYPE_P || !object_is_static(object)) continue;
            unsigned original = (unsigned)obj_field_int32_get(object, OBJ_F_FLAGS);
            unsigned hidden[] = { OF_OFF, OF_DESTROYED, OF_OFF | OF_DESTROYED, OF_EXTINCT, OF_OFF | OF_EXTINCT, OF_DESTROYED | OF_EXTINCT };
            bool good = true;
            for (unsigned i = 0; i < SDL_arraysize(hidden); i++) {
                obj_field_int32_set(object, OBJ_F_FLAGS, (int)(original | hidden[i]));
                object_replica_detach(object);
                object_replica_attach(object);
                obj_pool_perm_oid_remove(id);
                good &= obj_pool_perm_lookup(id) == object;
            }
            object_replica_detach(object);
            obj_field_int32_set(object, OBJ_F_FLAGS, (int)original);
            object_replica_attach(object);
            fprintf(stderr, "ARCMP hidden positional identities %s cases=6\n", good ? "PASS" : "FAIL");
            return good;
        } while (obj_inst_next(&object, &iterator));
    return fail("no native static object for identity visibility proof");
}

static bool verify_world_fields(void)
{
    int64_t sx,sy;sector_limits_get(&sx,&sy);
    int64_t loc=obj_field_int64_get(player_get_local_pc_obj(),OBJ_F_LOCATION);
    fprintf(stderr,"ARCMP world sector baseline origin=%lld,%lld limits=%lld,%lld\n",(long long)SECTOR_X(sector_id_from_loc(loc)),(long long)SECTOR_Y(sector_id_from_loc(loc)),(long long)sx,(long long)sy);
    CoopWorldTracker* tracker = coop_world_tracker_create(false);
    uint8_t* baseline = NULL;
    size_t size = 0;
    bool good = tracker && coop_world_capture(tracker, &baseline, &size) && baseline;
    if (!good) return fail("world baseline capture failed");
    unsigned objects = coop_world_upserts(baseline, size);
    int64_t pc = player_get_local_pc_obj();
    stat_base_set(pc, STAT_STRENGTH, 3);
    obj_field_string_set(pc, OBJ_F_PC_PLAYER_NAME, "Damaged local name");
    int original_global = script_global_var_get(1999);
    script_global_var_set(1999, 8801);
    if (coop_world_apply(baseline, size - 1) || stat_base_get(pc, STAT_STRENGTH) != 3) good = false;
    uint8_t original_type = baseline[32 + 4 + 38];
    baseline[32 + 4 + 38] = 255;
    if (coop_world_apply(baseline, size) || script_global_var_get(1999) != 8801) good = false;
    baseline[32 + 4 + 38] = original_type;
    /* A structurally valid catalogue must still match installed text before
     * any object/story mutation from the same world batch is committed. */
    CoopDocuments unknown_document = { .count = 1, .records = { { .type = 0, .number = INT32_MAX - 20000u } } };
    size_t document_size;
    uint8_t* document = coop_documents_encode(&unknown_document, &document_size);
    size_t old_document_size = coop_object_read32(baseline + 28);
    uint8_t* invalid = document && old_document_size <= size - 32 ? malloc(size - old_document_size + document_size) : NULL;
    if (!invalid)
        good = false;
    else {
        memcpy(invalid, baseline, size - old_document_size);
        memcpy(invalid + size - old_document_size, document, document_size);
        coop_object_write32(invalid + 28, (uint32_t)document_size);
        if (coop_world_apply(invalid, size - old_document_size + document_size)
            || stat_base_get(pc, STAT_STRENGTH) != 3 || script_global_var_get(1999) != 8801) good = false;
    }
    free(invalid);
    free(document);
    coop_authority_set(COOP_AUTHORITY_REPLICA);
    if (good) good = coop_world_apply(baseline, size) && verify_party();
    if (script_global_var_get(1999) != original_global) good = false;
    uint8_t* unchanged = NULL;
    size_t unchanged_size = 0;
    if (good) good = coop_world_capture(tracker, &unchanged, &unchanged_size) && !unchanged;
    if (!good) fprintf(stderr, "ARCMP world initial apply/unchanged failed: initial=%u next=%u bytes=%zu\n", objects, coop_world_upserts(unchanged, unchanged_size), unchanged_size);
    free(unchanged);
    free(baseline);
    int64_t item = item_wield_get(pc, ITEM_INV_LOC_WEAPON), created = 0;
    if (good && item) {
        int64_t prototype = obj_field_handle_get(item, OBJ_F_PROTOTYPE_HANDLE);
        int64_t location = obj_field_int64_get(pc, OBJ_F_LOCATION);
        good = object_create(prototype, location, &created);
    } else
        good = false;
    ObjectID id = created ? obj_get_id(created) : (ObjectID) { 0 };
    uint8_t* addition = NULL;
    size_t addition_size = 0;
    if (good) good = coop_world_capture(tracker, &addition, &addition_size) && addition && coop_world_upserts(addition, addition_size) == 1;
    if (created) {
        object_replica_detach(created);
        object_delete(created);
    }
    if (good) good = !obj_pool_perm_lookup(id) && coop_world_apply(addition, addition_size);
    created = obj_pool_perm_lookup(id);
    if (good) good = created && obj_type_is_item(obj_field_int32_get(created, OBJ_F_TYPE));
    if (created) {
        object_replica_detach(created);
        object_delete(created);
    }
    uint8_t* deletion = NULL;
    size_t deletion_size = 0;
    if (good) good = coop_world_capture(tracker, &deletion, &deletion_size) && deletion && coop_world_deletions(deletion, deletion_size) == 1;
    if (good) good = coop_world_apply(addition, addition_size) && obj_pool_perm_lookup(id);
    if (good) {
        int64_t original_reference = obj_field_handle_get(pc, OBJ_F_CRITTER_FLEEING_FROM);
        obj_field_handle_set(pc, OBJ_F_CRITTER_FLEEING_FROM, obj_pool_perm_lookup(id));
        good = !coop_world_apply(deletion, deletion_size) && obj_pool_perm_lookup(id);
        obj_field_handle_set(pc, OBJ_F_CRITTER_FLEEING_FROM, original_reference);
    }
    if (good) good = coop_world_apply(deletion, deletion_size) && !obj_pool_perm_lookup(id);
    free(addition);
    free(deletion);
    if (good) {
        unsigned before=coop_world_tracked_objects(tracker);
        /* Force native eviction without deleting a mobile object. */
        sector_flush(0);
        uint8_t* evicted=NULL;size_t evicted_size=0;
        good=coop_world_capture(tracker,&evicted,&evicted_size);
        unsigned live=0;int64_t handle;int iter;
        if(obj_inst_first(&handle,&iter))do {live++;}while(obj_inst_next(&handle,&iter));
        good=good && coop_world_tracked_objects(tracker)==live && live<before;
        fprintf(stderr,"ARCMP world eviction cache %s before=%u tracked=%u live=%u\n",good?"PASS":"FAIL",before,coop_world_tracked_objects(tracker),live);
        free(evicted);
        map_precache_sectors(obj_field_int64_get(pc,OBJ_F_LOCATION));
        uint8_t* reloaded=NULL;size_t reload_size=0;
        good=good && coop_world_capture(tracker,&reloaded,&reload_size) && reloaded
            && coop_world_upserts(reloaded,reload_size)>0 && !coop_world_deletions(reloaded,reload_size);
        free(reloaded);
    }
    coop_world_tracker_destroy(tracker);
    coop_authority_set(COOP_AUTHORITY_LOCAL);
    fprintf(stderr, "ARCMP world object batch %s objects=%u baseline_bytes=%zu\n", good ? "PASS" : "FAIL", objects, size);
    return good || fail("world fields/creation/deletion roundtrip failed");
}

static bool verify_world_visuals(void)
{
    /* Normal overlay arrays already cross world batches. The renderer also needs their
     * derived visibility bits, including for PCs whose flags use the pose path.
     * Exercise each layer independently, highest native slots, frame values,
     * and removal with deliberately stale local visibility flags. */
    const unsigned mask = OF_HAS_OVERLAYS | OF_HAS_UNDERLAYS;
    const int fields[] = { OBJ_F_OVERLAY_FORE, OBJ_F_OVERLAY_BACK, OBJ_F_UNDERLAY };
    const int slots[] = { 6, 6, 3 };
    tig_art_id_t art[3];
    for (unsigned layer = 0; layer < 3; layer++) {
        art[layer] = TIG_ART_ID_INVALID;
        for (unsigned number = 0; number < 1024; number++) {
            tig_art_id_t candidate;
            if (tig_art_eye_candy_id_create(number, 0, 0, 0, layer, 0, 4, &candidate) == TIG_OK
                && tig_art_exists(candidate) == TIG_OK) {
                art[layer] = candidate;
                break;
            }
        }
        if (art[layer] == TIG_ART_ID_INVALID) return fail("overlay test could not find native artwork");
    }
    CoopWorldTracker* restore = coop_world_tracker_create(false);
    uint8_t* original = NULL;
    size_t original_size = 0;
    bool good = restore && coop_world_capture(restore, &original, &original_size) && original;
    CoopAuthority authority = coop_authority_get();
    coop_authority_set(COOP_AUTHORITY_REPLICA);
    unsigned cases = 0;
    for (unsigned separate = 0; good && separate < 2; separate++) {
        CoopWorldTracker* tracker = coop_world_tracker_create(separate != 0);
        for (unsigned phase = 0; good && phase < 6; phase++) {
            unsigned wanted = phase == 1 || phase == 2 ? OF_HAS_OVERLAYS
                : phase == 3 ? OF_HAS_UNDERLAYS : phase == 4 ? mask : 0;
            for (unsigned i = 0; i < 4; i++) {
                int64_t pc = coop_campaign_object(i);
                for (unsigned layer = 0; layer < 3; layer++) {
                    bool active = phase == layer + 1 || phase == 4;
                    for (int slot = 0; slot <= slots[layer]; slot++)
                        object_overlay_set(pc, fields[layer], slot,
                            active && slot == slots[layer] ? tig_art_id_frame_inc(art[layer]) : TIG_ART_ID_INVALID);
                }
            }
            uint8_t* batch = NULL;
            size_t size = 0;
            good = coop_world_capture(tracker, &batch, &size) && batch;
            for (unsigned i = 0; good && i < 4; i++) {
                int64_t pc = coop_campaign_object(i);
                unsigned flags = obj_field_int32_get(pc, OBJ_F_FLAGS);
                obj_field_int32_set(pc, OBJ_F_FLAGS, (flags & ~mask) | (mask ^ wanted));
            }
            if (good) good = coop_world_apply(batch, size);
            free(batch);
            for (unsigned i = 0; good && i < 4; i++) {
                int64_t pc = coop_campaign_object(i);
                good = ((unsigned)obj_field_int32_get(pc, OBJ_F_FLAGS) & mask) == wanted;
                for (unsigned layer = 0; good && layer < 3; layer++) {
                    bool active = phase == layer + 1 || phase == 4;
                    good = obj_arrayfield_uint32_get(pc, fields[layer], slots[layer])
                        == (active ? tig_art_id_frame_inc(art[layer]) : TIG_ART_ID_INVALID);
                }
                if (good) cases++;
            }
            uint8_t* unchanged = NULL;
            size_t unchanged_size = 0;
            if (good) good = coop_world_capture(tracker, &unchanged, &unchanged_size) && !unchanged;
            free(unchanged);
            if (!good) fprintf(stderr, "ARCMP overlay restoration failed separate_pc_pose=%u phase=%u\n", separate, phase);
        }
        coop_world_tracker_destroy(tracker);
    }
    bool restored = original && coop_world_apply(original, original_size);
    free(original);
    coop_world_tracker_destroy(restore);
    coop_authority_set(authority);
    good &= restored;
    fprintf(stderr, "ARCMP overlay presentation %s cases=%u simulation=replica\n", good ? "PASS" : "FAIL", cases);
    return good || fail("replica overlay visibility did not match native art arrays");
}

static bool move_party(int destination)
{
    int64_t x, y;
    if (!map_get_starting_location(destination, &x, &y)) return fail("test destination has no start position");
    TeleportData teleport = { 0 };
    teleport.obj = player_get_local_pc_obj();
    teleport.map = destination;
    teleport.loc = location_make(x, y);
    if (!teleport_do(&teleport)) return fail("travel request rejected");
    teleport_ping(0);
    if (map_current_map() != destination) return fail("destination did not load");
    return verify_party();
}

static bool verify_rebinding(void)
{
    CoopConfig host_config = { .name = "New host connection" };
    CoopConfig guest_config = { .name = "Changed nickname" };
    memcpy(host_config.identity, coop_campaign_member(0)->owner, COOP_IDENTITY_SIZE);
    memcpy(guest_config.identity, coop_campaign_member(2)->owner, COOP_IDENTITY_SIZE);
    CoopSession* host = coop_session_create(&host_config);
    CoopSession* guest = coop_session_create(&guest_config);
    bool good = host && guest;
    uint64_t start = SDL_GetTicks();
    if (good) good = coop_session_host(host, 0, start)
        && coop_session_join(guest, "127.0.0.1", coop_session_port(host), start);
    while (good && coop_session_phase(guest) != COOP_LOBBY && SDL_GetTicks() - start < 5000) {
        coop_session_poll(host, SDL_GetTicks());
        coop_session_poll(guest, SDL_GetTicks());
        SDL_Delay(1);
    }
    if (good) good = coop_session_phase(guest) == COOP_LOBBY
        && coop_session_local_slot(guest) == 1
        && coop_campaign_slot_for_owner(coop_session_players(host)[1].identity) == 2;
    coop_session_destroy(guest);
    coop_session_destroy(host);
    return good || fail("a returning owner did not retain their campaign character after changing connection slot/nickname");
}

static bool write_manifest(const uint8_t* bytes, size_t size)
{
    TigFile* stream = tig_file_fopen("Save\\Current\\arcmp-party.bin", "wb");
    if (!stream) return false;
    bool good = tig_file_fwrite(bytes, 1, size, stream) == size;
    if (tig_file_fclose(stream) != 0) good = false;
    return good;
}

static bool verify_bad_manifests(void)
{
    uint8_t saved[296], changed[297];
    TigFile* stream = tig_file_fopen("Save\\Current\\arcmp-party.bin", "rb");
    if (!stream) return false;
    bool good = tig_file_fread(saved, 1, sizeof(saved), stream) == sizeof(saved);
    tig_file_fclose(stream);
    if (!good) return false;
    CoopCampaignMember before[COOP_MAX_PLAYERS];
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        before[i] = *coop_campaign_member(i);
    for (unsigned test = 0; test < 7; test++) {
        memcpy(changed, saved, sizeof(saved));
        size_t size = sizeof(saved);
        switch (test) {
        case 0:
            size = 12;
            break;
        case 1:
            changed[11] = 99;
            break;
        case 2:
            memcpy(changed + 16 + 69 + 1, changed + 16 + 1, 16);
            break; // duplicate owner
        case 3:
            memcpy(changed + 16 + 69 + 17, changed + 16 + 17, 16);
            break; // duplicate PC
        case 4:
            memset(changed + 16 + 1, 0, 16);
            break;
        case 5:
            changed[size++] = 42;
            break;
        case 6:
            changed[32] ^= 0x40;
            break;
        }
        if (test >= 1 && test <= 4) {
            uint32_t checksum = (uint32_t)crc32(0, changed, sizeof(saved) - 4);
            for (unsigned j = 0; j < 4; j++)
                changed[sizeof(saved) - 4 + j] = (uint8_t)(checksum >> (24 - 8 * j));
        }
        if (!write_manifest(changed, size) || coop_campaign_load()) return fail("malformed ownership manifest was accepted");
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            const CoopCampaignMember* current = coop_campaign_member(i);
            if (!current || !objid_is_equal(before[i].object_id, current->object_id)
                || memcmp(before[i].owner, current->owner, COOP_IDENTITY_SIZE)) return fail("rejected manifest changed live ownership");
        }
    }
    if (!write_manifest(saved, sizeof(saved)) || !coop_campaign_load()) return false;
    return verify_party();
}

static bool verify_solo_compatibility(void)
{
    int64_t others[3];
    for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++)
        others[i - 1] = coop_campaign_object(i);
    coop_campaign_reset();
    for (unsigned i = 0; i < 3; i++)
        object_destroy(others[i]);
    if (!gamelib_save("SlotSolo", "Single-player compatibility probe")) return fail("single-player save failed");
    if (tig_file_exists("Save\\Current\\arcmp-party.bin", NULL)) return fail("single-player save retained stale ownership");
    mainmenu_ui_reset();
    if (!gamelib_load("SlotSolo") || coop_campaign_active() || !player_is_pc_obj(player_get_local_pc_obj())) return fail("single-player save load failed");
    /* Return the live engine to the four-PC save for further probes/captures. */
    mainmenu_ui_reset();
    return gamelib_load("SlotCoop") && verify_party();
}

static bool capture_combat(const char* stage, bool framebuffer)
{
    if (!framebuffer) return true;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        fprintf(stderr, "ARCMP combat visual stage=%s member=%u scale=%d aid=%08x flags=%08x\n", stage, i,
            obj_field_int32_get(pc, OBJ_F_BLIT_SCALE), (unsigned)obj_field_int32_get(pc, OBJ_F_CURRENT_AID),
            (unsigned)obj_field_int32_get(pc, OBJ_F_CRITTER_FLAGS));
    }
    iso_interface_refresh();
    intgame_show();
    gamelib_redraw();
    tig_window_display();
    char path[80];
    snprintf(path, sizeof(path), "arcmp-combat-%s.bmp", stage);
    return tig_video_screenshot_save(path) == TIG_OK;
}

static bool verify_optional_resources(void)
{
    /* Owned loose fixtures exercise both plain I/O and archived directories. */
    const char* empty = "arcmp-cache-empty.bin";
    const char* valid = "arcmp-cache-valid.bin";
    FILE* file = fopen(empty, "wb");
    if (!file || fclose(file)) return fail("empty cache fixture could not be created");
    file = fopen(valid, "wb");
    if (!file) return false;
    bool good = fwrite("abc", 1, 3, file) == 3;
    if (fclose(file)) good = false;
    TigFileCache* cache = tig_file_cache_create(2, 256);
    const char* rejected[] = { empty, "sound\\", "arcmp-cache-missing.bin" };
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); i++) {
        TigFileCacheEntry* entry = tig_file_cache_acquire(cache, rejected[i]);
        good = good && entry && !entry->data && entry->size == 0
            && cache->bytes == 0 && cache->items_count == 0;
    }
    TigFileCacheEntry* entry = tig_file_cache_acquire(cache, valid);
    good = good && entry && entry->data && entry->size == 3
        && !memcmp(entry->data, "abc", 3) && cache->bytes == 3 && cache->items_count == 1;
    if (entry && entry->data) tig_file_cache_release(cache, entry);
    tig_file_cache_destroy(cache);
    if (remove(empty) || remove(valid)) good = false;
    char path[TIG_MAX_PATH];
    /* Native SFX tables deliberately contain silent entries. */
    bool silent = false;
    for (int id = 0; id < 4000; id++) {
        if (gsound_resolve_path(id, path, sizeof(path)) == TIG_OK)
            good = good && strlen(path) > strlen("sound\\");
        else {
            good = good && path[0] == '\0';
            silent = true;
        }
    }
    return (good && silent) || fail("optional resources poisoned the file cache");
}

static bool verify_combat_projection(void)
{
    int64_t remote = coop_campaign_object(2);
    if (!sub_4B6C90(true)) return false;
    combat_critter_activate_combat_mode(remote);
    CoopCombatState before, after;
    uint8_t first[COOP_COMBAT_STATE_BYTES], second[COOP_COMBAT_STATE_BYTES];
    bool good = coop_engine_combat_capture(&before) && before.active
        && coop_combat_state_encode(&before, first);
    TigFile* stream = tig_file_fopen("arcmp-combat-save.bin", "wb");
    if (!stream) return false;
    good = good && combat_save(stream);
    if (tig_file_fclose(stream)) good = false;
    combat_reset();
    coop_authority_set(COOP_AUTHORITY_REPLICA);
    stream = tig_file_fopen("arcmp-combat-save.bin", "rb");
    if (stream) {
        GameLoadInfo info = { .stream = stream };
        good = good && combat_load(&info) && coop_engine_combat_capture(&after)
            && after.active && after.generation == 1 && after.points == before.points
            && after.round == before.round && !memcmp(after.actor.bytes, before.actor.bytes, sizeof(after.actor.bytes));
        if (tig_file_fclose(stream)) good = false;
    } else good = false;
    good = good && coop_engine_combat_install(&before)
        && !combat_consume_action_points(remote, 1) && !anim_goal_attack(remote, coop_campaign_object(0));
    combat_turn_based_end_critter_turn(remote);
    combat_critter_deactivate_combat_mode(remote);
    good = good && coop_engine_combat_capture(&after) && coop_combat_state_encode(&after, second)
        && !memcmp(first, second, sizeof(first));
    coop_authority_set(COOP_AUTHORITY_HOST);
    combat_reset();
    combat_critter_deactivate_combat_mode(remote);
    if (!sub_4B6C90(false) || tig_file_remove("arcmp-combat-save.bin")) good = false;
    return good || fail("active combat save loaded replica simulation instead of presentation");
}

static bool encounter_idle(int64_t obj)
{
    AnimID current, fidget;
    return !sub_423300(obj, &current) || (anim_is_current_goal_type(obj, AG_ANIM_FIDGET, &fidget)
        && anim_id_is_equal(&current, &fidget));
}
static void encounter_ping(void)
{
    tig_ping();
    gamelib_ping();
    dialog_ui_reset();
    TigMessage msg;
    while (tig_message_dequeue(&msg) == TIG_OK) {
        if (msg.type == TIG_MESSAGE_REDRAW) iso_redraw();
        else intgame_process_event(&msg);
    }
    SDL_Delay(2);
}
static bool verify_encounter_players_inner(bool framebuffer)
{
    if (!assemble_party_setup(true)) return false;
    mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
    teleport_ping(0);
    if (coop_campaign_count() != 4 || !coop_campaign_validate()) return fail("four legal encounter characters required");
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        fprintf(stderr, "ARCMP encounter build %u strength=%d dexterity=%d melee=%d unspent=%d\n", i,
            stat_base_get(pc, STAT_STRENGTH), stat_base_get(pc, STAT_DEXTERITY),
            basic_skill_points_get(pc, BASIC_SKILL_MELEE), stat_base_get(pc, STAT_UNSPENT_POINTS));
        if (stat_base_get(pc, STAT_STRENGTH) != 9 || stat_base_get(pc, STAT_DEXTERITY) != 10
            || basic_skill_points_get(pc, BASIC_SKILL_MELEE) != 2 || stat_base_get(pc, STAT_UNSPENT_POINTS))
            return fail("encounter character differs from the native legal point budget");
    }
    coop_authority_set(COOP_AUTHORITY_HOST);
    int64_t host = coop_campaign_object(0), remote = coop_campaign_object(1), enemy = OBJ_HANDLE_NULL;
    int64_t obj;
    int iterator;
    /* Keep the retail creature's scripts, HP, AI, inventory and XP budgets. */
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || critter_is_dead(obj)
            || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
        int64_t proto = obj_field_handle_get(obj, OBJ_F_PROTOTYPE_HANDLE);
        int distance = object_dist(host, obj);
        if (proto && obj_get_id(proto).d.a == 28447 && distance > 40 && distance < 100) {
            enemy = obj;
            break;
        }
    } while (obj_inst_next(&obj, &iterator));
    if (!enemy) return fail("retail crash-site enemy was not found");
    int64_t origin = obj_field_int64_get(host, OBJ_F_LOCATION);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (i == 1) continue;
        if (!coop_encounter_place_far(coop_campaign_object(i), location_make(location_get_x(origin) + 3 * i, location_get_y(origin))))
            return fail("off-host fixture has no clear floor tile");
    }
    bool placed = coop_encounter_place_near(remote, enemy);
    if (!placed || object_dist(host, enemy) <= 30 || coop_campaign_nearest_player(enemy) != remote)
        return fail("remote encounter could not be separated from the host");
    coop_authority_set(COOP_AUTHORITY_LOCAL);
    bool local_boundary = coop_campaign_nearest_player(enemy) == host;
    coop_authority_set(COOP_AUTHORITY_HOST);
    if (!local_boundary) return fail("party activity anchors escaped cooperative host authority");
    int initial_xp[4], expected[4];
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        initial_xp[i] = stat_base_get(pc, STAT_EXPERIENCE_POINTS);
        critter_give_xp(pc, 42);
        expected[i] = stat_base_get(pc, STAT_EXPERIENCE_POINTS) - initial_xp[i];
        stat_base_set(pc, STAT_EXPERIENCE_POINTS, initial_xp[i]);
    }
    critter_give_combat_xp(remote, 42);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        if (stat_base_get(pc, STAT_EXPERIENCE_POINTS) != initial_xp[i] + expected[i])
            return fail("shared combat reward differs from native XP modifiers");
        stat_base_set(pc, STAT_EXPERIENCE_POINTS, initial_xp[i]);
    }
    /* Exercise the same credit path used by an owned follower's damage. */
    int64_t original_leader = critter_leader_get(enemy);
    critter_leader_set(enemy, remote);
    critter_give_combat_xp(enemy, 42);
    critter_leader_set(enemy, original_leader);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        if (stat_base_get(pc, STAT_EXPERIENCE_POINTS) != initial_xp[i] + expected[i])
            return fail("owned follower combat credit did not share native XP");
        stat_base_set(pc, STAT_EXPERIENCE_POINTS, initial_xp[i]);
    }
    coop_authority_set(COOP_AUTHORITY_REPLICA);
    critter_give_xp(remote, 99);
    critter_give_combat_xp(remote, 99);
    coop_authority_set(COOP_AUTHORITY_HOST);
    if (stat_base_get(remote, STAT_EXPERIENCE_POINTS) != initial_xp[1])
        return fail("replica changed experience points");
    if (!sub_4B6C90(false)) return false;
    mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU);
    sub_5412D0();
    iso_interface_refresh();
    intgame_show();
    gamelib_redraw();
    tig_window_display();
    int hp = obj_field_int32_get(remote, OBJ_F_HP_DAMAGE), fatigue = critter_fatigue_damage_get(remote);
    int worth = obj_field_int32_get(enemy, OBJ_F_NPC_EXPERIENCE_WORTH);
    int pool = obj_field_int32_get(enemy, OBJ_F_NPC_EXPERIENCE_POOL);
    bool focused = false, damaged = false;
    uint64_t start = SDL_GetTicks(), diagnostic_at = start;
    while (SDL_GetTicks() - start < 45000 && !damaged && !critter_is_dead(remote)) {
        encounter_ping();
        focused = focused || obj_field_handle_get(enemy, OBJ_F_NPC_COMBAT_FOCUS) == remote;
        damaged = obj_field_int32_get(remote, OBJ_F_HP_DAMAGE) > hp || critter_fatigue_damage_get(remote) > fatigue;
        if (SDL_GetTicks() - diagnostic_at > 5000) {
            diagnostic_at = SDL_GetTicks();
            fprintf(stderr, "ARCMP encounter waiting distance=%lld enemy-active=%d enemy-attack=%d enemy-idle=%d remote-unconscious=%d\n",
                (long long)object_dist(enemy, remote), combat_critter_is_combat_mode_active(enemy),
                anim_goal_attack_pending(enemy, remote), encounter_idle(enemy), critter_is_unconscious(remote));
        }
    }
    fprintf(stderr, "ARCMP encounter AI focused=%d damaged=%d host-distance=%lld enemy-hp=%d remote-hp=%d\n",
        focused, damaged, (long long)object_dist(host, enemy), object_hp_current(enemy), object_hp_current(remote));
    if (!focused || !damaged || critter_is_dead(remote)) return fail("ordinary off-host enemy AI did not attack the remote PC");
    combat_auto_attack_set(true);
    location_origin_set(obj_field_int64_get(remote, OBJ_F_LOCATION));
    if (!capture_combat("encounter-ai", framebuffer)) return false;
    for (unsigned i = 2; i < COOP_MAX_PLAYERS; i++)
        if (!coop_encounter_place_near(coop_campaign_object(i), enemy)) return fail("party cannot reach the original enemy");
    start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 45000 && !critter_is_dead(enemy) && !critter_is_dead(remote)) {
        for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++) {
            int64_t pc = coop_campaign_object(i);
            if (!critter_is_dead(pc) && encounter_idle(pc)) {
                combat_critter_activate_combat_mode(pc);
                if (combat_check_attack(pc, enemy)) anim_goal_attack(pc, enemy);
            }
        }
        encounter_ping();
    }
    if (!critter_is_dead(enemy) || critter_is_dead(remote)) return fail("owned party could not defeat the ordinary enemy");
    int rewarded[4];
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        rewarded[i] = stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS);
        if (rewarded[i] <= initial_xp[i]) return fail("ordinary enemy kill did not reward the whole living party");
    }
    if (obj_field_int32_get(enemy, OBJ_F_NPC_EXPERIENCE_WORTH)) return fail("native kill reward budget was not retired");
    CombatContext corpse;
    sub_4B2210(remote, enemy, &corpse);
    corpse.flags = CF_HIT;
    corpse.dam[DAMAGE_TYPE_NORMAL] = 100;
    combat_dmg(&corpse);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if (stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS) != rewarded[i]) return fail("corpse damage rewarded duplicate XP");
    int64_t host_floor = obj_field_int64_get(host, OBJ_F_LOCATION);
    int64_t blocked = location_make(location_get_x(origin) + 80, location_get_y(origin));
    if (!tile_is_blocking(blocked, false)) return fail("blocked corpse cleanup fixture is not on blocking terrain");
    /* A script teleport/terrain change must not destroy an owned corpse. */
    sub_43E770(host, blocked, 0, 0);
    critter_kill(host);
    TimeEvent decay = { .type = TIMEEVENT_TYPE_DECAY_DEAD_BODIE };
    decay.params[0].object_value = host;
    if (critter_decay_timeevent_schedule(host) || !critter_decay_timeevent_process(&decay)
        || (obj_field_int32_get(host, OBJ_F_FLAGS) & OF_DESTROYED)) return fail("owned corpse was destroyed by native decay");
    int dead_xp = stat_base_get(host, STAT_EXPERIENCE_POINTS);
    critter_give_combat_xp(remote, 42);
    if (stat_base_get(host, STAT_EXPERIENCE_POINTS) != dead_xp) return fail("dead PC received combat XP");
    anim_ui_event_add_delay(ANIM_UI_EVENT_TYPE_END_DEATH, -1, 0);
    start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 6000) encounter_ping();
    if (!coop_campaign_active() || coop_campaign_count() != 4 || map_current_map() != map_by_type(MAP_TYPE_START_MAP)
        || !critter_is_dead(host) || critter_is_dead(remote)
        || (obj_field_int32_get(host, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) return fail("one controller's death reset the shared world or removed its body");
    sub_43E770(host, host_floor, 0, 0);
    if (!capture_combat("encounter-survivor", framebuffer)) return false;
    int64_t off_host_distance = object_dist(host, enemy);
    CoopCampaignMember members[4];
    int saved_xp[4], saved_damage[4];
    for (unsigned i = 0; i < 4; i++) {
        members[i] = *coop_campaign_member(i);
        saved_xp[i] = stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS);
        saved_damage[i] = obj_field_int32_get(coop_campaign_object(i), OBJ_F_HP_DAMAGE);
    }
    CoopCombatState saved_combat;
    if (!coop_engine_combat_capture(&saved_combat)) return false;
    size_t snapshot_size = 0;
    uint8_t* snapshot = coop_engine_snapshot_capture(&snapshot_size);
    bool restored = snapshot && coop_engine_snapshot_restore(snapshot, snapshot_size, members[0].owner);
    free(snapshot);
    if (!restored || !coop_engine_combat_install(&saved_combat)) return fail("dead owner's full save bundle did not restore");
    for (unsigned i = 0; i < 4; i++) {
        int64_t member = coop_campaign_object(i);
        const CoopCampaignMember* entry = coop_campaign_member(i);
        if (!entry || !member || !objid_is_equal(entry->object_id, members[i].object_id)
            || memcmp(entry->owner, members[i].owner, sizeof(entry->owner))
            || stat_base_get(member, STAT_EXPERIENCE_POINTS) != saved_xp[i]
            || obj_field_int32_get(member, OBJ_F_HP_DAMAGE) != saved_damage[i])
            return fail("owned character identity or death state changed across a full snapshot");
    }
    if (player_get_local_pc_obj() != coop_campaign_object(0) || !critter_is_dead(player_get_local_pc_obj()))
        return fail("dead snapshot owner did not retain its own character binding");
    DateTime frozen = sub_45A7C0();
    gamelib_ping();
    DateTime after = sub_45A7C0();
    if (datetime_compare(&frozen, &after)) return fail("dead owner's replica advanced campaign time");
    FILE* file = fopen("arcmp-encounter-engine-result.json", "wb");
    if (!file) return false;
    fprintf(file, "{\"verified\":true,\"native_prototype\":28447,\"off_host_distance\":%lld,\"native_ai_focus\":true,\"native_ai_damage\":true,\"enemy_died\":true,\"initial_worth\":%d,\"initial_pool\":%d,\"rewarded_players\":4,\"native_xp_modifiers\":true,\"follower_combat_xp_credit\":true,\"replica_xp_guard\":true,\"corpse_xp_replay_guard\":true,\"host_death_preserves_campaign\":true,\"dead_pc_excluded_from_xp\":true,\"owned_corpse_persists\":true,\"blocked_corpse_cleanup_guard\":true,\"dead_owner_full_snapshot\":true,\"local_anchor_boundary\":true}\n", (long long)off_host_distance, worth, pool);
    return fclose(file) == 0;
}

static bool verify_encounter_players(bool framebuffer)
{
    /* Observe an incoming native hit before allowing the fixture to retaliate. */
    bool previous = combat_auto_attack_get(player_get_local_pc_obj());
    combat_auto_attack_set(false);
    bool good = verify_encounter_players_inner(framebuffer);
    combat_auto_attack_set(previous);
    return good;
}

static bool verify_effect_references(void)
{
    if (!assemble_party()) return false;
    mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
    teleport_ping(0);
    coop_authority_set(COOP_AUTHORITY_HOST);
    if (!sub_4B6C90(false)) return false;
    int64_t origin = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
    int64_t effect[4], enemy, follower;
    ObjectID effect_ids[4];
    if (!object_create(sub_4685A0(17067), origin, &enemy)
        || !object_create(sub_4685A0(17067), origin, &follower)) return false;
    for (unsigned n = 0; n < 2; n++) {
        int64_t actor = n ? follower : enemy;
        Script none = { 0 };
        for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(actor, OBJ_F_SCRIPTS_IDX, sap, &none);
        object_flags_unset(actor, OF_OFF | OF_DONTDRAW);
        obj_field_int32_set(actor, OBJ_F_NPC_FLAGS, 0);
        obj_field_int32_set(actor, OBJ_F_NPC_FACTION, 0);
        object_hp_pts_set(actor, 200);
        for (int resistance = 0; resistance < 5; resistance++) obj_arrayfield_int32_set(actor, OBJ_F_RESISTANCE_IDX, resistance, 0);
    }
    if (!critter_follow(follower, coop_campaign_object(3), true)) return false;
    int hp[4], follower_hp = object_hp_damage_get(follower);
    for (unsigned i = 0; i < 4; i++) {
        sub_43E770(coop_campaign_object(i), origin, 0, 0);
        hp[i] = object_hp_damage_get(coop_campaign_object(i));
        if (!object_create(sub_4685A0(BP_SMALL_STONE), origin, &effect[i])) return false;
        effect_ids[i] = obj_get_id(effect[i]);
    }
    AnimRunInfo fire = { 0 };
    fire.cur_stack_data = &fire.goals[0];
    fire.params[0].obj = effect[0];
    AnimGoalSubNode* damage = &anim_goal_nodes[AG_ANIMATE_LOOP_FIRE_DMG]->subnodes[4];
    for (unsigned i = 0; i < 4; i++) {
        fire.params[1].obj = coop_campaign_object(i);
        int before = object_hp_damage_get(enemy);
        if (!damage->func(&fire) || object_hp_damage_get(enemy) <= before)
            return fail("real-time summoned fire did not damage an enemy");
        for (unsigned j = 0; j < 4; j++)
            if (object_hp_damage_get(coop_campaign_object(j)) != hp[j]) return fail("summoned fire damaged a human party member");
        if (object_hp_damage_get(follower) != follower_hp) return fail("summoned fire damaged another owner's follower");
    }
    if (!sub_4B6C90(true)) return false;
    combat_critter_activate_combat_mode(coop_campaign_object(0));
    if (!combat_turn_based_is_active()) return fail("turn-based effect check did not enter combat");
    fire.cur_stack_data->params[AGDATA_SCRATCH_VAL6].data = combat_turn_based_turn_get() - 1;
    int before = object_hp_damage_get(enemy);
    if (!damage->func(&fire) || object_hp_damage_get(enemy) <= before) return fail("turn-based fire did not damage an enemy");
    before = object_hp_damage_get(enemy);
    if (!damage->func(&fire) || object_hp_damage_get(enemy) != before) return fail("fire damaged twice in the same turn");
    if (!sub_4B6C90(false)) return false;
    // A fire with no caster must still hurt independent critters.
    fire.params[1].obj = OBJ_HANDLE_NULL;
    before = object_hp_damage_get(enemy);
    if (!damage->func(&fire) || object_hp_damage_get(enemy) <= before) return fail("unowned environmental fire skipped an enemy");

    Ryan identity = { 0 };
    sub_443EB0(coop_campaign_object(0), &identity);
    int64_t changed = coop_campaign_object(1), loaded = OBJ_HANDLE_NULL;
    TigFile* stream = tig_file_fopen("arcmp-effect-reference.bin", "wb");
    if (!stream) return false;
    bool good = object_save_obj_handle_safe(&changed, &identity, stream);
    if (tig_file_fclose(stream)) good = false;
    stream = tig_file_fopen("arcmp-effect-reference.bin", "rb");
    if (!stream) return false;
    good = good && object_load_obj_handle_safe(&loaded, NULL, stream) && loaded == changed;
    if (tig_file_fclose(stream)) good = false;
    tig_file_remove("arcmp-effect-reference.bin");
    if (!good) return fail("safe reference saved its old target");
    MagicTechObjectNode* node = mt_obj_node_create();
    sub_443EB0(changed, &node->field_8);
    mt_obj_node_destroy(node);
    node = mt_obj_node_create();
    good = node->obj == OBJ_HANDLE_NULL && node->field_8.objid.type == OID_TYPE_NULL;
    mt_obj_node_destroy(node);
    if (!good || sub_444020(NULL, &identity)) return fail("empty native reference retained an old identity");

    // Exercise real projectile redirection, nested goal ownership, and native
    // full-campaign serialization without advancing these synthetic projectiles.
    for (unsigned i = 0; i < 4; i++) {
        object_destroy(effect[i]);
        if (!object_create(sub_4685A0(BP_PROJECTILE), origin, &effect[i])) return false;
        effect_ids[i] = obj_get_id(effect[i]);
        AnimGoalData goal;
        AnimID id;
        if (!sub_44D500(&goal, effect[i], AG_PROJECTILE)) return false;
        goal.params[AGDATA_TARGET_OBJ].obj = enemy;
        goal.params[AGDATA_SCRATCH_OBJ].obj = enemy;
        if (!anim_goal_add(&goal, &id) || !anim_subgoal_add(id, &goal, __FILE__, __LINE__)
            || !sub_4372B0(effect[i], coop_campaign_object(i)) || !sub_435A00(effect[i], origin, enemy)) return false;
        AnimRunInfo* run;
        if (!anim_id_to_run_info(&id, &run)) return false;
        anim_goal_set_object(&run->goals[1], AGDATA_SCRATCH_OBJ, OBJ_HANDLE_NULL);
    }
    CoopCampaignMember owner = *coop_campaign_member(3);
    ObjectID enemy_id = obj_get_id(enemy);
    size_t size = 0;
    uint8_t* snapshot = coop_engine_snapshot_capture(&size);
    good = snapshot && coop_engine_snapshot_restore(snapshot, size, owner.owner);
    free(snapshot);
    if (!good) return fail("effect ownership snapshot did not restore");
    for (unsigned i = 0; i < 4; i++) {
        int64_t obj = obj_pool_perm_lookup(effect_ids[i]);
        int index = anim_find_first(obj);
        if (!obj || index < 0) return fail("saved projectile disappeared");
        AnimRunInfo* run = &anim_run_info[index];
        if (run->current_goal != 1 || run->goals[0].params[AGDATA_TARGET_OBJ].obj != coop_campaign_object(i)
            || run->goals[0].params[AGDATA_SCRATCH_OBJ].obj != obj_pool_perm_lookup(enemy_id)
            || run->goals[1].params[AGDATA_SCRATCH_OBJ].obj != OBJ_HANDLE_NULL)
            return fail("saved projectile targets reverted");
        for (int g = 0; g <= run->current_goal; g++)
            if (run->goals[g].params[AGDATA_PARENT_OBJ].obj != coop_campaign_object(i)) return fail("saved nested effect lost its caster");
        anim_force_interrupt(&run->id);
        if (obj_handle_is_valid(obj) && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & OF_DESTROYED)) object_destroy(obj);
    }
    fprintf(stderr, "ARCMP effects PASS casters=4 party-protection=5 real-time=4 turn-based-once=1 environmental=1 full-snapshot=%zu\n", size);
    return true;
}

static bool verify_sparse_party(unsigned mask)
{
    if (!(mask & 1) || mask > 15 || mask == 1 || !assemble_party_roster(false, mask))
        return fail("could not start the requested lobby roster");
    mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
    teleport_ping(0);
    coop_authority_set(COOP_AUTHORITY_HOST);
    unsigned count = 0, last = 0;
    int xp[COOP_MAX_PLAYERS] = { 0 };
    CoopCampaignMember members[COOP_MAX_PLAYERS] = { 0 };
    int64_t origin = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if ((coop_campaign_member(i) != NULL) != ((mask & (1u << i)) != 0)) return fail("lobby slots changed");
        if (!(mask & (1u << i))) continue;
        count++; last = i; members[i] = *coop_campaign_member(i);
        int64_t pc = coop_campaign_object(i);
        sub_43E770(pc, location_make(location_get_x(origin) + i, location_get_y(origin)), 0, 0);
        xp[i] = stat_base_get(pc, STAT_EXPERIENCE_POINTS);
    }
    if (coop_campaign_count() != count || !coop_campaign_validate()) return fail("roster population differs");
    int64_t actor = coop_campaign_object(last);
    coop_social_xp(actor, 20);
    coop_social_alignment(actor, '=', 7);
    coop_social_reputation(actor, 1000, false);
    quest_state_set(actor, 1000, QUEST_STATE_ACCEPTED, 0);
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc = coop_campaign_object(i);
        if (stat_base_get(pc, STAT_EXPERIENCE_POINTS) <= xp[i] || stat_base_get(pc, STAT_ALIGNMENT) != 7
            || !reputation_has(pc, 1000) || quest_state_get(pc, 1000) != QUEST_STATE_ACCEPTED)
            return fail("a surviving player missed shared story consequences");
        xp[i] = stat_base_get(pc, STAT_EXPERIENCE_POINTS);
    }
    coop_social_reputation(actor, 1000, true);
    critter_give_combat_xp(actor, 20);
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc = coop_campaign_object(i);
        if (reputation_has(pc, 1000) || stat_base_get(pc, STAT_EXPERIENCE_POINTS) <= xp[i])
            return fail("shared removal or combat reward skipped a slot");
        xp[i] = stat_base_get(pc, STAT_EXPERIENCE_POINTS);
    }
    int64_t key = 0;
    if (!object_create(sub_4685A0(BP_KEY), origin, &key)) return fail("key creation failed");
    obj_field_int32_set(key, OBJ_F_KEY_KEY_ID, 777);
    if (!item_transfer(key, actor) || !sub_463370(coop_campaign_object(0), 777)) return fail("last occupied slot cannot share a key");
    unsigned flags = obj_field_int32_get(actor, OBJ_F_SPELL_FLAGS);
    obj_field_int32_set(actor, OBJ_F_SPELL_FLAGS, flags | OSF_TEMPUS_FUGIT);
    bool time_spell = magictech_check_env_sf(OSF_TEMPUS_FUGIT);
    obj_field_int32_set(actor, OBJ_F_SPELL_FLAGS, flags);
    if (!time_spell) return fail("last occupied slot's time spell is ignored");
    CoopSectorStream* stream = coop_sector_stream_create();
    bool sectors = stream && coop_sector_stream_poll(stream);
    coop_sector_stream_destroy(stream);
    if (!sectors || !coop_engine_use_settled()) return fail("sparse party cannot settle its sectors/actions");
    CoopRest rest = { 0 };
    unsigned status = coop_engine_rest_begin(&rest, actor, 0, 0, 0);
    if (status != COOP_CONTROL_OK || rest.count != count) {
        fprintf(stderr, "ARCMP sparse rest status=%u actors=%u expected=%u\n", status, rest.count, count);
        coop_engine_rest_end(&rest); return fail("rest omitted a surviving character");
    }
    bool complete = false;
    status = coop_engine_rest_step(&rest, 200, false, &complete);
    coop_engine_rest_end(&rest);
    if (status != COOP_CONTROL_OK || !complete) return fail("sparse party cannot finish an hour of rest");
    size_t size = 0;
    uint8_t* snapshot = coop_engine_snapshot_capture(&size);
    bool restored = snapshot && coop_engine_snapshot_restore(snapshot, size, members[last].owner);
    free(snapshot);
    if (!restored || !coop_campaign_validate() || coop_campaign_count() != count
        || player_get_local_pc_obj() != coop_campaign_object(last)) return fail("sparse save cannot restore for its last owner");
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if ((member != NULL) != ((mask & (1u << i)) != 0)) return fail("save compacted campaign slots");
        if (!member) continue;
        int64_t pc = coop_campaign_object(i);
        if (!objid_is_equal(member->object_id, members[i].object_id)
            || memcmp(member->owner, members[i].owner, COOP_IDENTITY_SIZE)
            || stat_base_get(pc, STAT_EXPERIENCE_POINTS) != xp[i]
            || stat_base_get(pc, STAT_ALIGNMENT) != 7 || quest_state_get(pc, 1000) != QUEST_STATE_ACCEPTED)
            return fail("restored sparse owner/story state differs");
    }
    fprintf(stderr, "ARCMP sparse party PASS mask=%u count=%u rebound=%u snapshot=%zu\n", mask, count, last, size);
    return true;
}

static bool verify_dialogue_semantics(void)
{
    if (!assemble_party()) return false;
    mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
    teleport_ping(0);
    coop_authority_set(COOP_AUTHORITY_HOST);
    int64_t object, prototype = 0;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
        Script script;
        obj_arrayfield_script_get(object, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
        if (script.num == 1324) { prototype = obj_field_handle_get(object, OBJ_F_PROTOTYPE_HANDLE); break; }
    } while (obj_inst_next(&object, &iterator));
    if (!prototype) return false;
    for (unsigned member = coop_campaign_next_slot(0); member < COOP_MAX_PLAYERS; member = coop_campaign_next_slot(member + 1)) {
        int64_t pc = coop_campaign_object(member), actors[3];
        int others[COOP_MAX_PLAYERS];
        for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) others[i] = item_gold_get(coop_campaign_object(i));
        int gold = item_gold_get(pc);
        if (gold && !item_gold_transfer(pc, 0, gold, 0)) return false;
        if (!item_gold_transfer(0, pc, 30, 0)) return false;
        for (unsigned i = 0; i < 3; i++) {
            if (!object_create(prototype, obj_field_int64_get(pc, OBJ_F_LOCATION), &actors[i])) return false;
            Script none = { 0 };
            for (unsigned sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(actors[i], OBJ_F_SCRIPTS_IDX, sap, &none);
            int amount = item_gold_get(actors[i]);
            if (amount && !item_gold_transfer(actors[i], 0, amount, 0)) return false;
            if (i) {
                obj_field_int32_set(actors[i], OBJ_F_NPC_FLAGS, obj_field_int32_get(actors[i], OBJ_F_NPC_FLAGS) | ONF_FORCED_FOLLOWER);
                if (!critter_follow(actors[i], pc, true) || !item_gold_transfer(0, actors[i], i == 1 ? 40 : 80, 0)) return false;
            }
        }
        /* Original Schuyler conditions name Magnus (6411). His human owner
         * must not change party-presence branches or grant others commands. */
        int companion_name = obj_field_int32_get(actors[1], OBJ_F_NAME);
        obj_field_int32_set(actors[1], OBJ_F_NAME, 6411);
        for (unsigned i = 0; i < 4; i++) {
            DialogState state = { .pc_obj = coop_campaign_object(i), .npc_obj = actors[1] };
            char present[] = "pa6411", absent[] = "pa-6411", owner[] = "fo0", other[] = "fo1";
            if (!dialog_conditions_match(&state, present) || dialog_conditions_match(&state, absent)
                || dialog_conditions_match(&state, owner) != (i == member)
                || dialog_conditions_match(&state, other) != (i != member))
                return fail("shared companion presence or personal command ownership");
            int level = stat_level_get(state.pc_obj, STAT_LEVEL);
            for (int threshold = level > 1 ? level - 1 : 1; threshold <= level + 1; threshold++) {
                char minimum[24], maximum[24];
                snprintf(minimum, sizeof(minimum), "le%d", threshold);
                snprintf(maximum, sizeof(maximum), "le-%d", threshold);
                if (dialog_conditions_match(&state, minimum) != (level >= threshold)
                    || dialog_conditions_match(&state, maximum) != (level <= threshold))
                    return fail("dialogue level boundary");
            }
        }
        int flags = obj_field_int32_get(actors[1], OBJ_F_SPELL_FLAGS);
        obj_field_int32_set(actors[1], OBJ_F_SPELL_FLAGS, flags | OSF_MIND_CONTROLLED);
        DialogState companion = { .pc_obj = pc, .npc_obj = actors[1] };
        char present[] = "pa6411", absent[] = "pa-6411";
        if (dialog_conditions_match(&companion, present) || !dialog_conditions_match(&companion, absent))
            return fail("mind-controlled companion counted in story dialogue");
        obj_field_int32_set(actors[1], OBJ_F_SPELL_FLAGS, flags);
        ai_npc_wait(actors[1]);
        for (unsigned i = 0; i < 4; i++) {
            companion.pc_obj = coop_campaign_object(i);
            if (dialog_conditions_match(&companion, present) || !dialog_conditions_match(&companion, absent))
                return fail("waiting companion counted in story dialogue");
        }
        ai_npc_unwait(actors[1], true);
        companion.pc_obj = pc;
        if (!dialog_conditions_match(&companion, present)) return fail("returning companion absent in story dialogue");
        obj_field_int32_set(actors[1], OBJ_F_NAME, companion_name);
        fprintf(stderr, "ARCMP companion dialogue PASS owner=%u four choosers; level boundaries; waiting/mind-control; personal commands\n", member);
        if (!dialog_campaign_semantics_probe(pc, actors[0])) return fail("native dialogue payment, conditions or counters");
        if (!script_campaign_reactions_probe(pc, actors[0])) return fail("native scripted NPC reaction operands");
        for (unsigned test = 0; test < 4; test++) {
            bool reward = (test & 1) != 0, consume = test >= 2;
            int64_t item, parent = OBJ_HANDLE_NULL;
            int64_t source = reward ? actors[0] : pc, target = reward ? pc : actors[0];
            if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)
                || !item_transfer(item, source)) return fail("prepare dialogue handover");
            obj_field_int32_set(item, OBJ_F_NAME, 50000 + test);
            ObjectCallbackState original;
            if (!object_callback_capture(&original, item)) return false;
            if (consume) {
                Script hook = { .num = 999938 };
                obj_arrayfield_script_set(item, OBJ_F_SCRIPTS_IDX, SAP_REMOVE_ITEM, &hook);
            }
            char action[32];
            snprintf(action, sizeof(action), "in%d", (reward ? -1 : 1) * (50000 + (int)test));
            DialogState state = { .pc_obj = pc, .npc_obj = actors[0] };
            if (!dialog_execute_actions(&state, action, 0)) return fail("dialogue handover continuation");
            if (consume ? object_callback_live(&original)
                        : !object_callback_live(&original) || !item_parent(item, &parent) || parent != target)
                return fail("dialogue handover ownership");
            if (!consume) object_destroy(item);
        }
        for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
            if (i != member && item_gold_get(coop_campaign_object(i)) != others[i]) return fail("dialogue spent another human's money");
    }
    return true;
}

static bool verify_spell_cache(void)
{
    if (!assemble_party()) return false;
    mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
    teleport_ping(0);
    coop_authority_set(COOP_AUTHORITY_HOST);
    const char* map = "__arcmp_spell_cache";
    const char* cache = "Save\\Current\\maps\\__arcmp_spell_cache\\MT.dat";
    const char* table = "arcmp-spell-table.tmp";
    if (tig_file_exists(cache, NULL) || tig_file_exists(table, NULL)
        || !tig_file_mkdir("save\\current\\maps\\__arcmp_spell_cache")) return false;
    MagicTechInvocation invocation;
    magictech_invocation_init(&invocation, OBJ_HANDLE_NULL, SPELL_STRENGTH_OF_EARTH);
    sub_4440E0(coop_campaign_object(0), &invocation.target_obj);
    if (!magictech_invocation_submit(&invocation)) return fail("native cache fixture spell refused");
    magictech_save_nodes_to_map(map);
    if (tig_file_rename("SAVE\\CURRENT\\MAPS\\__ARCMP_SPELL_CACHE\\MT.DAT", "save\\current\\maps\\__arcmp_spell_cache\\retained.dat")
        || tig_file_rename("SAVE\\CURRENT\\MAPS\\__ARCMP_SPELL_CACHE\\RETAINED.DAT", cache)) return fail("repository-relative case rename failed");
    TigFile* file = tig_file_fopen(cache, "rb");
    if (!file) return false;
    int cache_bytes = tig_file_filelength(file);
    tig_file_fclose(file);
    bool reserved[512];
    for (unsigned i = 0; i < 512; i++) {
        reserved[i] = magictech_run_info[i].id == -1;
        if (reserved[i]) magictech_run_info[i].id = (int)i;
    }
    MagicTechRunInfo* before = MALLOC(512 * sizeof(*before));
    memcpy(before, magictech_run_info, 512 * sizeof(*before));
    bool good = !magictech_load_nodes_from_map(map) && tig_file_exists(cache, NULL)
        && !memcmp(before, magictech_run_info, 512 * sizeof(*before));
    for (unsigned i = 0; i < 512; i++) if (reserved[i]) magictech_run_info[i].id = -1;
    if (good) good = magictech_load_nodes_from_map(map) && !tig_file_exists(cache, NULL);
    unsigned active = 0;
    for (unsigned i = 0; i < 512; i++) if (magictech_run_token(i)) active++;
    if (!good || active < 2) { FREE(before); return fail("native cache pressure/import did not preserve both runs"); }
    file = tig_file_fopen(table, "wb");
    if (!file) { FREE(before); return false; }
    good = magictech_post_save(file);
    tig_file_fclose(file);
    file = tig_file_fopen(table, "rb");
    if (!file) { FREE(before); return false; }
    GameLoadInfo load = { .stream = file };
    good &= magictech_post_load(&load);
    tig_file_fclose(file);
    memcpy(before, magictech_run_info, 512 * sizeof(*before));
    uint64_t tokens[512];
    for (unsigned i = 0; i < 512; i++) tokens[i] = magictech_run_token(i);
    /* Invalid extents must neither loop/overflow nor replace the live pool. */
    const int invalid[][3] = {{0,513,0},{0,-1,0},{0,1,0},{0,1,2},{0,1,-2},{0,1,INT32_MIN},{0,1,1}};
    unsigned refusals = 0;
    for (unsigned i = 0; good && i < SDL_arraysize(invalid); i++) {
        file = tig_file_fopen(table, "wb");
        if (!file) { good = false; break; }
        good = tig_file_fwrite(invalid[i], sizeof(invalid[i]), 1, file) == 1;
        tig_file_fclose(file);
        file = tig_file_fopen(table, "rb");
        if (!file) { good = false; break; }
        load.stream = file;
        good &= !magictech_post_load(&load) && !memcmp(before, magictech_run_info, 512 * sizeof(*before));
        tig_file_fclose(file);
        for (unsigned j = 0; good && j < 512; j++) good = tokens[j] == magictech_run_token(j);
        if (good) refusals++;
    }
    FREE(before);
    anim_save_nodes_to_map(map);
    AnimRunInfo* goals = MALLOC(sizeof(anim_run_info));
    memcpy(goals, anim_run_info, sizeof(anim_run_info));
    for (unsigned i = 0; i < 216; i++) anim_run_info[i].flags |= 1;
    good &= !anim_load_nodes_from_map(map);
    memcpy(anim_run_info, goals, sizeof(anim_run_info));
    /* Ordinary map departure retires these old local goals. Test importing
     * their native cache with fresh IDs without executing a second AI tick. */
    for (unsigned i = 0; i < 216; i++) anim_run_info[i].flags = 0;
    dword_5E3500 = animNumActiveGoals = 0;
    good &= anim_load_nodes_from_map(map);
    memcpy(goals, anim_run_info, sizeof(anim_run_info));
    unsigned invalid_goals = 0;
    for (unsigned i = 0; good && i < 5; i++) {
        int header[20] = {0};
        header[18] = i == 0 ? 217 : 1;
        header[19] = i == 1 ? 0 : i == 2 ? 2 : i == 3 ? INT32_MIN : 1;
        file = tig_file_fopen(table, "wb");
        if (!file) { good = false; break; }
        good = tig_file_fwrite(header, sizeof(header), 1, file) == 1;
        tig_file_fclose(file);
        file = tig_file_fopen(table, "rb");
        if (!file) { good = false; break; }
        load.stream = file;
        good &= !anim_load(&load) && !memcmp(goals, anim_run_info, sizeof(anim_run_info));
        tig_file_fclose(file);
        if (good) invalid_goals++;
    }
    FREE(goals);
    tig_file_remove(table);
    if (!tig_file_rmdir("SAVE\\CURRENT\\MAPS\\__ARCMP_SPELL_CACHE")) good = false;
    if (!good) return fail("invalid native spell table mutated active runs");
    FILE* report = fopen("arcmp-spell-cache-result.json", "wb");
    if (!report) return false;
    fprintf(report, "{\"verified\":true,\"native_cache_bytes\":%d,\"full_pool_input_retained\":true,\"collision_import_active_runs\":%u,\"native_table_roundtrip\":true,\"invalid_table_refusals\":%u,\"live_pool_and_tokens_preserved\":true,\"native_animation_cache\":true,\"invalid_animation_tables\":%u,\"scope\":\"controlled native persistence, not a network travel or full campaign pass\"}\n", cache_bytes, active, refusals, invalid_goals);
    return fclose(report) == 0;
}

static bool verify_party_ai(bool framebuffer)
{
    if (!assemble_party()) return false;
    mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
    teleport_ping(0);
    coop_authority_set(COOP_AUTHORITY_HOST);
    /* Controlled native policy fixture, separate from ordinary campaign proof. */
    int64_t obj, actors[10], followers[7]; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC) object_flags_set(obj, OF_OFF);
    } while (obj_inst_next(&obj, &iterator));
    int64_t origin = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
    for (unsigned i = 0; i < 4; i++) actors[i] = coop_campaign_object(i);
    for (unsigned i = 0; i < 7; i++) {
        if (!object_create(sub_4685A0(17067), origin, &followers[i])) return false;
        Script none = { 0 };
        for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(followers[i], OBJ_F_SCRIPTS_IDX, sap, &none);
        object_flags_unset(followers[i], OF_OFF | OF_DONTDRAW);
        obj_field_int32_set(followers[i], OBJ_F_NPC_FLAGS, 0);
        obj_field_int32_set(followers[i], OBJ_F_NPC_FACTION, 0);
        obj_field_int32_set(followers[i], OBJ_F_NPC_ORIGIN, 0);
        stat_base_set(followers[i], STAT_ALIGNMENT, i == 6 ? -100 : 0);
        if (i < 6) {
            int64_t leader = i < 4 ? actors[i] : followers[i == 4 ? 0 : 4];
            if (!critter_follow(followers[i], leader, true)) return false;
            actors[4 + i] = followers[i];
        }
    }
    unsigned pairs = 0;
    for (unsigned i = 0; i < 10; i++) {
        for (unsigned j = 0; j < 10; j++) {
            if (!critter_faction_same(actors[i], actors[j])) return fail("cooperative native faction pair rejected");
            pairs++;
        }
        if (critter_faction_same(actors[i], followers[6]) || critter_faction_same(followers[6], actors[i]))
            return fail("unrelated native NPC acquired cooperative allegiance");
        int count, level;
        ai_calc_party_size_and_level(actors[i], &count, &level);
        if (count != 10) return fail("native AI strength omitted a human or nested companion");
    }
    if (!critter_follow(followers[0], followers[6], true)
        || critter_faction_same(actors[1], followers[5])
        || !critter_follow(followers[0], actors[2], true)
        || !critter_faction_same(actors[1], followers[5])) return fail("native changed leader did not update subtree allegiance");
    int64_t bodies[] = { actors[1], followers[3], followers[6] };
    for (unsigned i = 0; i < 3; i++) {
        int64_t before = obj_field_int64_get(bodies[i], OBJ_F_LOCATION), from = 0;
        if (!coop_encounter_place_far(bodies[i], location_make(location_get_x(origin) + 16, location_get_y(origin) + 16))) return false;
        int64_t to = obj_field_int64_get(bodies[i], OBJ_F_LOCATION), normal = 0, planned = 0; int type;
        if (!location_in_dir(to, 0, &from)) return false;
        int rot = location_rot(from, to);
        object_calc_traversal_cost(actors[0], from, rot, 0, &normal, &type, NULL);
        object_calc_traversal_cost(actors[0], from, rot, path_translate_flags(PATH_FLAG_IGNORE_PARTY_NPCS), &planned, &type, NULL);
        if (normal != bodies[i] || (i == 1 ? planned != 0 : planned != bodies[i])) return fail("party NPC planner changed human/enemy or physical collision");
        if (i == 1) {
            int npc_flags = obj_field_int32_get(bodies[i], OBJ_F_NPC_FLAGS), critter_flags = obj_field_int32_get(bodies[i], OBJ_F_CRITTER_FLAGS);
            for (unsigned state = 0; state < 3; state++) {
                obj_field_int32_set(bodies[i], OBJ_F_NPC_FLAGS, npc_flags | (state == 0 ? ONF_FIGHTING : 0));
                obj_field_int32_set(bodies[i], OBJ_F_CRITTER_FLAGS, critter_flags | (state == 1 ? OCF_FLEEING : state == 2 ? OCF_SURRENDERED : 0));
                object_calc_traversal_cost(actors[0], from, rot, path_translate_flags(PATH_FLAG_IGNORE_PARTY_NPCS), &planned, &type, NULL);
                if (planned != bodies[i]) return fail("party NPC planner ignored a fighting/fleeing/surrendered companion");
            }
            obj_field_int32_set(bodies[i], OBJ_F_NPC_FLAGS, npc_flags);
            obj_field_int32_set(bodies[i], OBJ_F_CRITTER_FLAGS, critter_flags);
        }
        sub_43E770(bodies[i], before, 0, 0);
    }
    if (!coop_encounter_place_near(followers[6], actors[0])) return false;
    ai_notify_explosion_dynamite(actors[0]);
    unsigned defenders = 0;
    for (unsigned i = 0; i < 6; i++) {
        int64_t focus = obj_field_handle_get(followers[i], OBJ_F_NPC_COMBAT_FOCUS);
        if (focus == actors[0]) return fail("party dynamite notification turned an owned companion hostile");
        if (focus == followers[6] && critter_pc_leader_get(followers[i]) != actors[0]) defenders++;
    }
    if (obj_field_handle_get(followers[6], OBJ_F_NPC_COMBAT_FOCUS) != actors[0] || !defenders)
        return fail("native bystander reaction or cross-owner defense was suppressed");
    if (!coop_campaign_save()) return false;
    coop_campaign_reset();
    bool local = !critter_faction_same(actors[0], actors[1]) && critter_faction_same(actors[2], followers[5]);
    if (!coop_campaign_load() || !local || !coop_campaign_validate()) return fail("single-player faction boundary changed");
    iso_interface_refresh(); intgame_show(); gamelib_redraw(); iso_redraw(); tig_window_display();
    if (framebuffer && tig_video_screenshot_save("arcmp-party-ai.bmp") != TIG_OK) return false;
    FILE* report = fopen("arcmp-party-ai-result.json", "wb"); if (!report) return false;
    fprintf(report, "{\"verified\":true,\"native_ally_pairs\":%u,\"unrelated_npc_refusals\":20,\"party_strength_actors\":10,\"nested_followers\":2,\"changed_leader_subtree\":true,\"dynamite_allied_targets\":0,\"native_bystander_aggro\":true,\"cross_owner_defenders\":%u,\"party_npc_plan_only\":true,\"human_and_unrelated_blockers_preserved\":true,\"single_player_boundary\":true}\n", pairs, defenders);
    if (fclose(report)) return false;
    fprintf(stderr, "ARCMP native party AI PASS ally-pairs=%u actors=10 cross-owner-defenders=%u\n", pairs, defenders);
    return true;
}

static bool verify_combat_players(bool framebuffer)
{
    if (!verify_optional_resources()) return false;
    if (!assemble_party()) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        stat_base_set(coop_campaign_object(i), STAT_STRENGTH, 11 + (int)i);
    mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
    teleport_ping(0);
    if (!verify_party()) return false;
    coop_authority_set(COOP_AUTHORITY_HOST);
    int64_t host = player_get_local_pc_obj();
    int64_t origin = obj_field_int64_get(host, OBJ_F_LOCATION);
    for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++) {
        int64_t position = location_make(location_get_x(origin) + 40 * i, location_get_y(origin));
        sub_43E770(coop_campaign_object(i), position, 0, 0);
    }
    /* This probe enters before the ordinary main loop's first world frame. */
    iso_interface_refresh();
    intgame_show();
    gamelib_redraw();
    tig_window_display();
    if (!sub_4B6C90(true)) return fail("could not select native turn-based combat");
    int64_t remote = coop_campaign_object(2);
    critter_set_concealed(remote, true);
    if (!critter_is_concealed(remote)
        || tig_art_id_anim_get(obj_field_int32_get(remote, OBJ_F_CURRENT_AID)) != TIG_ART_ANIM_CONCEAL_FIDGET)
        return fail("remote concealed stance fixture could not be prepared");
    combat_critter_activate_combat_mode(remote);
    if (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() != remote)
        return fail("a remote owned PC did not start turn-based combat");
    critter_set_concealed(remote, false);
    int remote_ap = combat_action_points_get();
    combat_critter_activate_combat_mode(host);
    if (combat_turn_based_whos_turn_get() != remote || combat_action_points_get() != remote_ap)
        return fail("another PC's stance change stole the current turn");
    unsigned seen = 0;
    for (unsigned n = 0; n < 128 && seen != 15; n++) {
        int64_t actor = combat_turn_based_whos_turn_get();
        if (!actor) return fail("native initiative lost its current actor");
        int member = coop_campaign_slot_for_object(actor);
        if (member >= 0) seen |= 1u << member;
        /* This fixture checks initiative coverage. NPC AI and network action
         * execution have separate acceptance requirements. */
        combat_turn_based_end_critter_turn(actor);
    }
    if (seen != 15) return fail("native initiative omitted a distant owned PC");
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        combat_turn_based_whos_turn_set(pc);
        int speed = stat_level_get(pc, STAT_SPEED);
        if (speed < 5) speed = 5;
        int fatigue = obj_field_int32_get(pc, OBJ_F_CRITTER_FATIGUE_DAMAGE);
        if (combat_action_points_get() != speed || !combat_check_action_points(pc, speed + 1))
            return fail("owned PC has different native overexertion eligibility");
        if (i == 0 && !capture_combat("host-turn", framebuffer)) return false;
        if (i == 2 && !capture_combat("remote-turn", framebuffer)) return false;
        if (!combat_consume_action_points(pc, speed + 1) || combat_action_points_get() != 0
            || obj_field_int32_get(pc, OBJ_F_CRITTER_FATIGUE_DAMAGE) != fatigue + 2)
            return fail("owned PC has different native overexertion cost");
        int64_t other = coop_campaign_object((i + 1) % COOP_MAX_PLAYERS);
        combat_turn_based_end_critter_turn(other);
        if (combat_turn_based_whos_turn_get() != pc) return fail("ending another actor's turn changed initiative");
    }
    combat_critter_activate_combat_mode(host);
    combat_critter_activate_combat_mode(remote);
    if (!combat_critter_is_combat_mode_active(host) || !combat_critter_is_combat_mode_active(remote))
        return fail("combat stance fixture could not prepare both participants");
    combat_critter_deactivate_combat_mode(remote);
    if (!combat_turn_based_is_active()) return fail("one player's stance change ended another player's combat");
    combat_critter_deactivate_combat_mode(host);
    if (combat_turn_based_is_active()) return fail("last player leaving combat retained native turn-based state");
    if (!capture_combat("ended", framebuffer)) return false;
    if (!sub_4B6C90(false)) return fail("could not select native real-time combat");
    combat_critter_activate_combat_mode(remote);
    if (combat_turn_based_is_active() || !combat_critter_is_combat_mode_active(remote))
        return fail("remote real-time stance did not preserve native combat mode");
    combat_critter_deactivate_combat_mode(remote);
    if (!verify_combat_projection()) return false;
    coop_authority_set(COOP_AUTHORITY_LOCAL);
    if (!sub_4B6C90(true)) return fail("single-player turn-based setup failed");
    combat_critter_activate_combat_mode(host);
    combat_turn_based_whos_turn_set(remote);
    if (combat_check_action_points(remote, combat_action_points_get() + 1))
        return fail("cooperative AP rules escaped the explicit host role");
    combat_turn_based_whos_turn_set(host);
    if (!combat_check_action_points(host, combat_action_points_get() + 1))
        return fail("single-player local AP rules changed");
    combat_critter_deactivate_combat_mode(host);
    if (combat_turn_based_is_active() || !sub_4B6C90(false)) return fail("single-player combat teardown failed");
    FILE* file = fopen("arcmp-combat-engine-result.json", "wb");
    if (!file) return false;
    fputs("{\"verified\":true,\"distant_players_in_initiative\":4,\"native_overexertion_players\":4,\"remote_starts_turn_based\":true,\"stance_preserves_other_turn\":true,\"other_actor_end_turn_ignored\":true,\"shared_combat_ends_after_last_stance\":true,\"real_time_stance\":true,\"single_player_rule_boundary\":true,\"optional_resource_cache\":true,\"active_combat_replica_load\":true,\"network_combat_verified\":false}\n", file);
    if (fclose(file)) return false;
    fprintf(stderr, "ARCMP native combat controllers PASS players=4 distant=4 overexertion=4\n");
    return true;
}

bool coop_engine_probe_report(const char* mode)
{
    FILE* file = fopen("arcmp-engine-result.json", "wb");
    if (!file) return false;
    char local_id[OID_STR_LEN];
    objid_id_to_str(local_id, obj_get_id(player_get_local_pc_obj()));
    fprintf(file, "{\"mode\":\"%s\",\"map\":%d,\"global0\":%d,\"local_pc\":\"%s\",\"local_member\":%d,\"members\":[",
        mode, map_current_map(), script_global_var_get(0), local_id, coop_campaign_slot_for_object(player_get_local_pc_obj()));
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopCampaignMember* entry = coop_campaign_member(i);
        if (!entry) continue;
        int64_t obj = coop_campaign_object(i);
        char oid[OID_STR_LEN];
        objid_id_to_str(oid, entry->object_id);
        if (i) fputc(',', file);
        fprintf(file, "{\"slot\":%u,\"id\":\"%s\",\"owner\":\"", i, oid);
        for (unsigned j = 0; j < COOP_IDENTITY_SIZE; j++)
            fprintf(file, "%02x", entry->owner[j]);
        fprintf(file, "\",\"strength\":%d,\"xp\":%d,\"alignment\":%d,\"inventory\":[", stat_base_get(obj, STAT_STRENGTH),
            stat_base_get(obj, STAT_EXPERIENCE_POINTS), stat_base_get(obj, STAT_ALIGNMENT));
        int count = obj_field_int32_get(obj, OBJ_F_CRITTER_INVENTORY_NUM);
        for (int index = 0; index < count; index++) {
            int64_t item = obj_arrayfield_handle_get(obj, OBJ_F_CRITTER_INVENTORY_LIST_IDX, index);
            objid_id_to_str(oid, obj_get_id(item));
            fprintf(file, "%s\"%s\"", index ? "," : "", oid);
        }
        fputs("]", file);
        const int equipment[] = { ITEM_INV_LOC_WEAPON, ITEM_INV_LOC_ARMOR };
        const char* labels[] = { "weapon", "armor" };
        for (unsigned j = 0; j < SDL_arraysize(equipment); j++) {
            int64_t item = item_wield_get(obj, equipment[j]);
            if (item)
                objid_id_to_str(oid, obj_get_id(item));
            else
                strcpy(oid, "none");
            fprintf(file, ",\"%s\":\"%s\"", labels[j], oid);
        }
        fputc('}', file);
    }
    CoopStoryState story;
    size_t story_size = 0;
    uint8_t* bytes = coop_engine_story_capture(&story) ? coop_story_state_encode(&story, &story_size) : NULL;
    if (!bytes) {
        fclose(file);
        return false;
    }
    fprintf(file, "],\"story_crc\":%u}\n", coop_snapshot_crc(bytes, story_size));
    FILE* story_file = fopen("arcmp-engine-story.bin", "wb");
    bool story_written = story_file && fwrite(bytes, 1, story_size, story_file) == story_size;
    if (story_file && fclose(story_file) != 0) story_written = false;
    free(bytes);
    bool closed = fclose(file) == 0;
    return closed && story_written;
}

static bool read_content(uint8_t id[32])
{
    FILE* file = fopen("arcmp-content.id", "rb");
    if (!file) return false;
    char hex[66] = { 0 };
    size_t size = fread(hex, 1, 65, file);
    fclose(file);
    if (size < 64 || (size == 65 && hex[64] != '\n')) return false;
    for (unsigned i = 0; i < 32; i++) {
        char byte[] = { hex[i * 2], hex[i * 2 + 1], 0 };
        char* end;
        if (!byte[0] || !byte[1] || !strchr("0123456789abcdefABCDEF", byte[0]) || !strchr("0123456789abcdefABCDEF", byte[1])) return false;
        id[i] = (uint8_t)strtoul(byte, &end, 16);
        if (*end) return false;
    }
    return true;
}

static bool movement_probe(CoopSession* session, bool owned_delete, bool raw_delete)
{
    CoopGameplay* gameplay = coop_gameplay_create(session, 1);
    if (!gameplay) return fail("could not bind gameplay session");
    bool host = coop_session_is_host(session);
    uint64_t start = SDL_GetTicks(), running_at = 0, finished_at = 0;
    int64_t initial[COOP_MAX_PLAYERS];
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        initial[i] = obj_field_int64_get(coop_campaign_object(i), OBJ_F_LOCATION);
    int local = coop_campaign_slot_for_object(player_get_local_pc_obj());
    int64_t target = location_make(location_get_x(initial[local]), location_get_y(initial[local]) + 8);
    bool route_mode = getenv("ARCMP_PROBE_LOCAL_ROUTE") != NULL;
    const uint64_t finish_after = route_mode ? 16000 : 8000;
    int64_t route[COOP_MOVE_WAYPOINTS];
    const int dx[COOP_MOVE_WAYPOINTS] = { 3, 3, 3, 0, 0, 0 };
    const int dy[COOP_MOVE_WAYPOINTS] = { 0, 2, 4, 4, 6, 8 };
    for (unsigned i = 0; i < COOP_MOVE_WAYPOINTS; i++)
        route[i] = location_make(location_get_x(initial[local]) + dx[i], location_get_y(initial[local]) + dy[i]);
    unsigned route_visited = 0;
    bool rejected = false, forged = false, requested = false, corrupted = false, finishing = false, good = true;
    bool simulation_advanced = false;
    bool delayed_world_ack = false;
    unsigned changes = 0;
    int64_t previous = initial[local];
    DateTime initial_time = sub_45A7C0();
    int initial_xp[COOP_MAX_PLAYERS], rewarded_xp[COOP_MAX_PLAYERS] = { 0 };
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        initial_xp[i] = stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS);
    int discovered_area = area_count() - 1;
    unsigned mutation_stage = 0;
    int64_t replicated_item = 0, transient_item = 0;
    ObjectID transient_owner = {0}, transient_id = {0};
    uint64_t deletion_chats = 0;
    ObjectID initial_inventory[COOP_MAX_PLAYERS];
    int64_t initial_inventory_handles[COOP_MAX_PLAYERS];
    bool inventory_changed = false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t first = obj_arrayfield_handle_get(coop_campaign_object(i), OBJ_F_CRITTER_INVENTORY_LIST_IDX, 0);
        initial_inventory_handles[i] = first;
        initial_inventory[i] = obj_get_id(first);
    }
    sub_45B360();
    while (good && SDL_GetTicks() - start < 25000) {
        tig_ping();
        coop_session_poll(session, SDL_GetTicks());
        /* Hold one client's world application/ACK across the host's finish
         * request. Poses keep arriving, exercising the final revision change. */
        uint64_t running_for = running_at ? SDL_GetTicks() - running_at : 0;
        bool delay_world = !host && local == 1 && running_for >= finish_after - 200 && running_for < finish_after + 600;
        delayed_world_ack |= delay_world;
        if (!delay_world) coop_gameplay_poll(gameplay, SDL_GetTicks());
        int64_t first_item = obj_arrayfield_handle_get(coop_campaign_object(0), OBJ_F_CRITTER_INVENTORY_LIST_IDX, 0);
        if (!inventory_changed && !objid_is_equal(obj_get_id(first_item), initial_inventory[0])) {
            fprintf(stderr, "ARCMP inventory changed after=%" PRIu64 " revision=%u old_handle=%" PRId64 " valid=%d new_handle=%" PRId64 " valid=%d original_lookup=%" PRId64 "\n",
                SDL_GetTicks() - start, coop_gameplay_world_revision(gameplay), initial_inventory_handles[0],
                obj_handle_is_valid(initial_inventory_handles[0]), first_item, obj_handle_is_valid(first_item), obj_pool_perm_lookup(initial_inventory[0]));
            inventory_changed = true;
        }
        if (coop_gameplay_failed(gameplay)) {
            fail(coop_gameplay_error(gameplay));
            good = false;
            break;
        }
        if (coop_gameplay_running(gameplay)) {
            if (owned_delete) for (; deletion_chats < coop_session_chat_count(session); deletion_chats++) {
                const CoopChat* chat = coop_session_chat_at(session, deletion_chats);
                char text[OID_STR_LEN];
                if (!chat || chat->slot) continue;
                if (sscanf(chat->text, "delete-owner:%39s", text) == 1) good = objid_id_from_str(&transient_owner, text);
                if (sscanf(chat->text, "delete-item:%39s", text) == 1) good = objid_id_from_str(&transient_id, text);
            }
            if (!running_at) running_at = SDL_GetTicks();
            if (!rejected) {
                int64_t invalid[] = { route[0], location_make(UINT32_MAX, UINT32_MAX) };
                good = route_mode ? coop_gameplay_move_route(gameplay, invalid, 2, false)
                                  : coop_gameplay_move(gameplay, invalid[1], false);
                rejected = true;
            }
            bool accepted = false;
            uint32_t sequence = coop_gameplay_last_result(gameplay, &accepted);
            if (!forged && sequence == 1) {
                if (accepted) good = fail("out-of-bounds movement was accepted");
                const CoopCampaignMember* other = coop_campaign_member((unsigned)((local + 1) % COOP_MAX_PLAYERS));
                if (good) good = route_mode ? coop_gameplay_move_route_actor(gameplay, other->object_id.d.g.data, route, COOP_MOVE_WAYPOINTS, false)
                                           : coop_gameplay_move_actor(gameplay, other->object_id.d.g.data, target, false);
                forged = true;
            }
            if (!requested && sequence == 2) {
                if (accepted) good = fail("movement for another owner's character was accepted");
                if (good) good = route_mode ? coop_gameplay_move_route(gameplay, route, COOP_MOVE_WAYPOINTS, local % 2 == 0)
                                           : coop_gameplay_move(gameplay, target, local % 2 == 0);
                requested = true;
            }
            if (!finishing) {
                DateTime before_ping = sub_45A7C0();
                gamelib_ping(); // Replica role must suppress local advancement.
                DateTime after_ping = sub_45A7C0();
                simulation_advanced |= datetime_compare(&before_ping, &after_ping) != 0;
            }
            if (host && SDL_GetTicks() - running_at > 1000 && mutation_stage == 0) {
                int64_t source = item_wield_get(coop_campaign_object(0), ITEM_INV_LOC_WEAPON);
                int64_t prototype = source ? obj_field_handle_get(source, OBJ_F_PROTOTYPE_HANDLE) : 0;
                good = prototype && object_create(prototype, initial[0], &replicated_item);
                if (good) {
                    obj_field_int32_set(replicated_item, OBJ_F_ITEM_WORTH, 987654);
                    obj_field_int32_set(replicated_item, OBJ_F_ITEM_WEIGHT, 17);
                    Script script = { .hdr = { .flags = 0, .counters = 0x10203040 }, .num = 0 };
                    obj_arrayfield_script_set(replicated_item, OBJ_F_SCRIPTS_IDX, SAP_USE, &script);
                    if (stat_base_set(coop_campaign_object(1), STAT_STRENGTH, 18) != 18)
                        good = fail("world fixture could not set a legal strength value");
                    obj_field_string_set(coop_campaign_object(0), OBJ_F_PC_PLAYER_NAME, "Host renamed hero");
                    script_global_var_set(1999, -2147483000);
                    script_global_flag_set(31, 1);
                    script_global_flag_set(3199, 1);
                    script_pc_flag_set(coop_campaign_object(1), 63, 1);
                    script_story_state_set(3);
                    good = area_set_known(coop_campaign_object(2), discovered_area)
                        && quest_state_set(coop_campaign_object(2), 1000, QUEST_STATE_ACCEPTED, 0) == QUEST_STATE_ACCEPTED
                        && quest_state_set(coop_campaign_object(3), 1001, QUEST_STATE_ACCEPTED, 0) == QUEST_STATE_ACCEPTED;
                    rumor_known_set(coop_campaign_object(2), 1000);
                    rumor_qstate_set(2999);
                    monstergen_disable(255);
                    newspaper_enqueue(1001, false);
                }
                mutation_stage++;
            }
            if (host && SDL_GetTicks() - running_at > 2000 && mutation_stage == 1) {
                good = item_transfer(replicated_item, coop_campaign_object(1));
                good = good && quest_state_set(coop_campaign_object(2), 1000, QUEST_STATE_ACHIEVED, 0) == QUEST_STATE_ACHIEVED
                    && quest_state_set(coop_campaign_object(3), 1001, QUEST_STATE_BOTCHED, 0) == QUEST_STATE_BOTCHED;
                mutation_stage++;
            }
            if (host && SDL_GetTicks() - running_at > 3000 && mutation_stage == 2) {
                int64_t prototype = obj_field_handle_get(replicated_item, OBJ_F_PROTOTYPE_HANDLE);
                good = object_create(prototype, initial[0], &transient_item);
                if (good) obj_field_int32_set(transient_item, OBJ_F_ITEM_WORTH, 987655);
                if (good && owned_delete) {
                    int64_t npc = 0;
                    int iterator;
                    if (obj_inst_first(&npc, &iterator)) do {
                        if (obj_field_int32_get(npc, OBJ_F_TYPE) == OBJ_TYPE_NPC && !critter_is_dead(npc)) break;
                        npc = 0;
                    } while (obj_inst_next(&npc, &iterator));
                    good = npc && item_transfer(transient_item, npc);
                    if (good) {
                        char owner[OID_STR_LEN], item_id[OID_STR_LEN], text[64];
                        objid_id_to_str(owner, obj_get_id(npc));
                        objid_id_to_str(item_id, obj_get_id(transient_item));
                        snprintf(text, sizeof(text), "delete-owner:%s", owner);
                        good = coop_session_chat(session, text);
                        snprintf(text, sizeof(text), "delete-item:%s", item_id);
                        good = good && coop_session_chat(session, text);
                    }
                }
                good = good && quest_unbotch(coop_campaign_object(1), 1001) == QUEST_STATE_ACCEPTED
                    && quest_state_set(coop_campaign_object(2), 1000, QUEST_STATE_COMPLETED, 0) == QUEST_STATE_COMPLETED;
                for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                    rewarded_xp[i] = stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS);
                    if (rewarded_xp[i] <= initial_xp[i]) good = fail("shared quest completion did not reward each PC");
                }
                mutation_stage++;
            }
            if (host && SDL_GetTicks() - running_at > 4000 && mutation_stage == 3) {
                obj_field_reset(replicated_item, OBJ_F_ITEM_WEIGHT);
                script_global_var_set(1999, -654321);
                script_global_flag_set(31, 0);
                script_story_state_set(4);
                for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                    good = good && quest_state_set(coop_campaign_object(i), 1000, QUEST_STATE_COMPLETED, 0) == QUEST_STATE_COMPLETED
                        && stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS) == rewarded_xp[i];
                }
                mutation_stage++;
            }
            if (host && SDL_GetTicks() - running_at > 5000 && mutation_stage == 4) {
                int64_t parent = 0;
                item_parent(transient_item, &parent);
                char owner[OID_STR_LEN], item_id[OID_STR_LEN];
                objid_id_to_str(owner, parent ? obj_get_id(parent) : (ObjectID){0});
                objid_id_to_str(item_id, obj_get_id(transient_item));
                fprintf(stderr, "ARCMP transient deletion item=%s owner=%s parent-type=%d\n", item_id, owner, parent ? obj_field_int32_get(parent, OBJ_F_TYPE) : -1);
                if (owned_delete && (!parent || obj_field_int32_get(parent, OBJ_F_TYPE) != OBJ_TYPE_NPC)) good = fail("owned deletion fixture lost its NPC parent");
                /* Native AI can pick up the ground weapon between stages.
                 * Gameplay destruction removes its owner's inventory entry;
                 * raw pool deletion would leave a dangling canonical GUID. */
                if (!raw_delete) {
                    object_destroy(transient_item);
                    if (!(obj_field_int32_get(transient_item, OBJ_F_FLAGS) & OF_DESTROYED)) good = fail("native destruction was refused");
                }
                /* Explicitly dispose the destroyed entry to exercise a wire
                 * deletion without waiting for a later sector unload. */
                if (good) {
                    object_replica_detach(transient_item);
                    object_delete(transient_item);
                }
                mutation_stage++;
            }
            if (!host && !corrupted && SDL_GetTicks() - running_at > 2500) {
                /* Verify a stale/corrupted local pose is repaired by the next
                 * host publication; this does not mutate the host's world. */
                object_move_replica(player_get_local_pc_obj(), initial[local], 13, -7);
                script_global_var_set(1999, 12345);
                script_global_flag_set(3199, 0);
                script_story_state_set(99);
                quest_global_state_set(1001, QUEST_STATE_COMPLETED);
                corrupted = true;
            }
            if (host && !finishing && SDL_GetTicks() - running_at > finish_after) {
                good = coop_gameplay_finish(gameplay);
                finishing = true;
            }
            int64_t current = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
            if (route_mode && route_visited < COOP_MOVE_WAYPOINTS && current == route[route_visited]) route_visited++;
            if (current != previous) changes++;
            previous = current;
        }
        if (coop_gameplay_finished(gameplay)) {
            if (!finished_at) finished_at = SDL_GetTicks();
            if (SDL_GetTicks() - finished_at > 600) break;
        }
        SDL_Delay(2);
    }
    good = good && coop_gameplay_finished(gameplay) && requested && changes >= 2;
    if (route_mode && route_visited != COOP_MOVE_WAYPOINTS) good = fail("a plotted local waypoint was skipped");
    bool accepted;
    good = good && coop_gameplay_last_result(gameplay, &accepted) == 3 && accepted;
    DateTime final_time = sub_45A7C0();
    if (simulation_advanced != host || datetime_compare(&initial_time, &final_time) == 0)
        good = fail("campaign simulation advanced on the wrong peer");
    FILE* report = fopen("arcmp-movement-result.json", "wb");
    if (!report) good = false;
    if (report) {
        fprintf(report, "{\"host\":%s,\"local_member\":%d,\"pose_changes\":%u,\"accepted\":%u,\"rejected\":%u,\"local_route\":%s,\"waypoints_visited\":%u,\"poses\":[",
            host ? "true" : "false", local, changes, coop_gameplay_accepted(gameplay), coop_gameplay_rejected(gameplay), route_mode ? "true" : "false", route_visited);
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            int64_t obj = coop_campaign_object(i);
            int64_t loc = obj_field_int64_get(obj, OBJ_F_LOCATION);
            int64_t expected = location_make(location_get_x(initial[i]), location_get_y(initial[i]) + 8);
            if (loc != expected) good = fail("an independent character did not reach its authorized target");
            fprintf(report, "%s{\"x\":%" PRId64 ",\"y\":%" PRId64 ",\"offset_x\":%d,\"offset_y\":%d,\"aid\":%u,\"hp_damage\":%d,\"fatigue_damage\":%d}",
                i ? "," : "", location_get_x(loc), location_get_y(loc), obj_field_int32_get(obj, OBJ_F_OFFSET_X), obj_field_int32_get(obj, OBJ_F_OFFSET_Y),
                (unsigned)obj_field_int32_get(obj, OBJ_F_CURRENT_AID), obj_field_int32_get(obj, OBJ_F_HP_DAMAGE), obj_field_int32_get(obj, OBJ_F_CRITTER_FATIGUE_DAMAGE));
        }
        fprintf(report, "],\"finished\":%s,\"client_local_pose_repair_exercised\":%s,\"campaign_time_advanced\":%s,\"local_simulation_advanced\":%s,\"delayed_world_ack\":%s}\n",
            good ? "true" : "false", corrupted ? "true" : "false", datetime_compare(&initial_time, &final_time) != 0 ? "true" : "false", simulation_advanced ? "true" : "false", delayed_world_ack ? "true" : "false");
        if (fclose(report) != 0) good = false;
    }
    if (host) good = good && coop_gameplay_accepted(gameplay) == 4 && coop_gameplay_rejected(gameplay) == 8;
    unsigned retained = 0, removed = 0;
    int64_t item = 0, candidate;
    int iterator;
    if (obj_inst_first(&candidate, &iterator)) do {
            if (!obj_type_is_item(obj_field_int32_get(candidate, OBJ_F_TYPE))) continue;
            int worth = obj_field_int32_get(candidate, OBJ_F_ITEM_WORTH);
            if (worth == 987654) {
                retained++;
                item = candidate;
            }
            if (worth == 987655) removed++;
        } while (obj_inst_next(&candidate, &iterator));
    PcQuestState quest;
    Script script;
    memset(&quest, 0, sizeof(quest));
    memset(&script, 0, sizeof(script));
    obj_arrayfield_pc_quest_get(coop_campaign_object(3), OBJ_F_PC_QUEST_IDX, 1001, &quest);
    if (item) obj_arrayfield_script_get(item, OBJ_F_SCRIPTS_IDX, SAP_USE, &script);
    char* name = NULL;
    obj_field_string_get(coop_campaign_object(0), OBJ_F_PC_PLAYER_NAME, &name);
    bool world_good = retained == 1 && !removed && item && obj_field_handle_get(item, OBJ_F_ITEM_PARENT) == coop_campaign_object(1)
        && stat_base_get(coop_campaign_object(1), STAT_STRENGTH) == 18
        && name && !strcmp(name, "Host renamed hero") && quest.state == QUEST_STATE_ACCEPTED
        && datetime_compare(&quest.datetime, &initial_time) >= 0 && script.hdr.counters == 0x10203040
        && coop_gameplay_world_revision(gameplay) >= 4 && coop_gameplay_world_deletions(gameplay) >= 1;
    if (!world_good) {
        fprintf(stderr, "ARCMP world fixture mismatch retained=%u removed=%u strength=%d name=%s quest=%d time=%" PRIu64 "/%" PRIu64 " script=%u revision=%u deletions=%" PRIu64 "\n",
            retained, removed, stat_base_get(coop_campaign_object(1), STAT_STRENGTH), name ? name : "(null)",
            quest.state, quest.datetime.value, initial_time.value, script.hdr.counters,
            coop_gameplay_world_revision(gameplay), coop_gameplay_world_deletions(gameplay));
    }
    good = good && world_good;
    bool owner_clear = true;
    if (owned_delete) {
        int64_t owner = obj_pool_perm_lookup(transient_owner);
        owner_clear = owner && transient_id.type == OID_TYPE_GUID && !coop_engine_object_refers_to(owner, &transient_id, 1);
        good = good && owner_clear;
    }
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t first = obj_arrayfield_handle_get(coop_campaign_object(i), OBJ_F_CRITTER_INVENTORY_LIST_IDX, 0);
        ObjectID id = obj_get_id(first);
        char before[OID_STR_LEN], after[OID_STR_LEN];
        objid_id_to_str(before, initial_inventory[i]);
        objid_id_to_str(after, id);
        if (!objid_is_equal(initial_inventory[i], id)) good = fail("an unchanged inventory reference diverged");
        fprintf(stderr, "ARCMP world inventory slot=%u initial=%s final=%s\n", i, before, after);
    }
    if (name) FREE(name);
    FILE* world = fopen("arcmp-world-result.json", "wb");
    if (!world) good = false;
    if (world) {
        char id[OID_STR_LEN];
        objid_id_to_str(id, item ? obj_get_id(item) : (ObjectID) { 0 });
        fprintf(world, "{\"verified\":%s,\"item\":\"%s\",\"parent_member\":%d,\"weight\":%d,\"script_counter\":%u,\"quest_state\":%d,\"revision\":%u,\"bytes\":%" PRIu64 ",\"deletions\":%" PRIu64 ",\"owned_transient_delete\":%s,\"npc_inventory_reference_cleared\":%s}\n",
            good ? "true" : "false", id, item ? coop_campaign_slot_for_object(obj_field_handle_get(item, OBJ_F_ITEM_PARENT)) : -1,
            item ? obj_field_int32_get(item, OBJ_F_ITEM_WEIGHT) : -1, script.hdr.counters, quest.state,
            coop_gameplay_world_revision(gameplay), coop_gameplay_world_bytes(gameplay), coop_gameplay_world_deletions(gameplay), owned_delete ? "true" : "false", owner_clear ? "true" : "false");
        if (fclose(world) != 0) good = false;
    }
    CoopStoryState story_state;
    bool story_good = coop_engine_story_capture(&story_state)
        && script_global_var_get(1999) == -654321 && !script_global_flag_get(31) && script_global_flag_get(3199)
        && script_pc_flag_get(coop_campaign_object(1), 63) && script_story_state_get() == 4
        && quest_global_state_get(1000) == QUEST_STATE_COMPLETED && quest_global_state_get(1001) == QUEST_STATE_ACCEPTED
        && rumor_qstate_get(2999) && monstergen_is_disabled(255);
    bool newspaper_found = false;
    for (unsigned i = 0; i < story_state.newspaper_count && i < COOP_STORY_NEWSPAPERS; i++)
        newspaper_found |= story_state.newspapers[i] == 1001;
    story_good = story_good && newspaper_found;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        story_good = story_good && area_is_known(pc, discovered_area) && rumor_known_get(pc, 1000)
            && quest_state_get(pc, 1000) == QUEST_STATE_COMPLETED && quest_state_get(pc, 1001) == QUEST_STATE_ACCEPTED
            && stat_base_get(pc, STAT_EXPERIENCE_POINTS) > initial_xp[i];
    }
    good = good && story_good;
    QuestLogbookEntry* journal_entries = calloc(2000, sizeof(*journal_entries));
    int journal_count = journal_entries ? quest_get_logbook_data(player_get_local_pc_obj(), journal_entries) : 0;
    if (journal_count != 2 || journal_entries[0].num != 1000 || journal_entries[1].num != 1001)
        good = fail("shared journal entries are missing or ordered differently");
    size_t story_size = 0;
    uint8_t* story_bytes = coop_story_state_encode(&story_state, &story_size);
    FILE* story_binary = fopen("arcmp-story-state.bin", "wb");
    if (!story_bytes || !story_binary) good = false;
    if (story_binary) {
        if (!story_bytes || fwrite(story_bytes, 1, story_size, story_binary) != story_size) good = false;
        if (fclose(story_binary) != 0) good = false;
    }
    FILE* story_report = fopen("arcmp-story-result.json", "wb");
    if (!story_report) good = false;
    if (story_report) {
        fprintf(story_report, "{\"verified\":%s,\"crc\":%u,\"bytes\":%zu,\"story\":%d,\"last_variable\":%d,\"area\":%d,\"game_days\":%u,\"game_ms\":%u,\"animation_days\":%u,\"animation_ms\":%u,\"xp\":[",
            good ? "true" : "false", story_bytes ? coop_snapshot_crc(story_bytes, story_size) : 0, story_size,
            script_story_state_get(), script_global_var_get(1999), discovered_area, story_state.game_time.days, story_state.game_time.milliseconds,
            story_state.animation_time.days, story_state.animation_time.milliseconds);
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
            fprintf(story_report, "%s%d", i ? "," : "", stat_base_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS));
        fprintf(story_report, "],\"journal_order\":[%d,%d]}\n", journal_count > 0 ? journal_entries[0].num : -1,
            journal_count > 1 ? journal_entries[1].num : -1);
        if (fclose(story_report) != 0) good = false;
    }
    free(story_bytes);
    free(journal_entries);
    fprintf(stderr, "ARCMP shared campaign state %s\n", story_good ? "PASS" : "FAIL");
    fprintf(stderr, "ARCMP movement probe %s changes=%u accepted=%u rejected=%u\n", good ? "PASS" : "FAIL", changes, coop_gameplay_accepted(gameplay), coop_gameplay_rejected(gameplay));
    coop_gameplay_destroy(gameplay);
    return good;
}

static bool dialogue_probe(CoopSession* session)
{
    held_gameplay = coop_gameplay_create(session, 1);
    if (!held_gameplay) return fail("could not bind shared dialogue gameplay");
    CoopDialogue* dialogue = coop_gameplay_dialogue(held_gameplay);
    bool host = coop_session_is_host(session), started = false, reopened = false, finishing = false;
    unsigned submissions = 0, denials = 0;
    bool handled[2] = { false, false };
    int64_t fixture_npc = 0;
    bool good = true;
    uint64_t start = SDL_GetTicks(), running_at = 0, page_at = 0, finished_at = 0;
    uint32_t recorded = 0;
    unsigned input_sent = 0;
    FILE* transcript = fopen("arcmp-dialogue-transcript.bin", "wb");
    if (!transcript) return false;
    while (good && SDL_GetTicks() - start < 25000) {
        tig_ping();
        coop_session_poll(session, SDL_GetTicks());
        coop_gameplay_poll(held_gameplay, SDL_GetTicks());
        if (coop_gameplay_failed(held_gameplay)) { good = fail(coop_gameplay_error(held_gameplay)); break; }
        if (!coop_gameplay_running(held_gameplay)) { SDL_Delay(2); continue; }
        if (!running_at) running_at = SDL_GetTicks();
        if (host && !started && SDL_GetTicks() - running_at > 1500) {
            int64_t npc = 0, candidate;
            int iterator;
            if (obj_inst_first(&candidate, &iterator)) do {
                if (obj_field_int32_get(candidate, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
                Script script = { 0 };
                obj_arrayfield_script_get(candidate, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
                const char* name = description_get(obj_field_int32_get(candidate, OBJ_F_DESCRIPTION));
                if (script.num == 1324) {
                    fprintf(stderr, "ARCMP retail dialogue fixture name=%s script=%d\n", name, script.num);
                    npc = candidate;
                }
            } while (obj_inst_next(&candidate, &iterator));
            if (!npc) { good = fail("retail Virgil fixture not found"); break; }
            /* Use the normal NPC script entry point with a remote-owned PC. */
            object_script_execute(coop_campaign_object(1), npc, OBJ_HANDLE_NULL, SAP_DIALOG, 0);
            fixture_npc = npc;
            started = true;
        }
        const CoopDialogueView* view = coop_dialogue_current(dialogue);
        if (view->revision > recorded) {
            size_t size = 0;
            uint8_t* bytes = coop_dialogue_view_encode(view, &size);
            uint8_t length[4];
            coop_object_write32(length, (uint32_t)size);
            if (!bytes || fwrite(length, 1, 4, transcript) != 4 || fwrite(bytes, 1, size, transcript) != size) good = false;
            free(bytes);
            fprintf(stderr, "ARCMP shared dialogue page=%u conversation=%u chooser=%u mode=%u options=%u reply=%.120s\n",
                view->revision, view->conversation, view->chooser, view->mode, view->option_count, view->reply);
            CoopDialogueView local = *view;
            coop_language_view(&local);
            fprintf(stderr, "ARCMP personal dialogue language=%s source=%s:%d reply=%.160s\n",
                coop_language_active(),view->text_refs[0].path,view->text_refs[0].key,local.reply);
            recorded = view->revision;
            page_at = SDL_GetTicks();
        }
        unsigned turn = view->conversation == 2 ? 1 : 0;
        if ((view->revision == 1 || view->revision == 4) && view->active && view->option_count && !handled[turn]) {
            if (view->chooser == coop_session_local_slot(session)) {
                if (SDL_GetTicks() - page_at > 1200) {
                    TigMessage input = { .type = TIG_MESSAGE_KEYBOARD };
                    input.data.keyboard.scancode = SDL_SCANCODE_1;
                    input.data.keyboard.pressed = true;
                    tig_message_enqueue(&input);
                    submissions++;
                    input_sent++;
                    handled[turn] = true;
                }
            } else {
                if (coop_dialogue_choose(dialogue, view->conversation, view->revision, 0)) good = fail("an observer submitted a retail dialogue choice");
                else denials++;
                TigMessage input = { .type = TIG_MESSAGE_KEYBOARD };
                input.data.keyboard.scancode = SDL_SCANCODE_1;
                input.data.keyboard.pressed = true;
                tig_message_enqueue(&input);
                input_sent++;
                handled[turn] = true;
            }
        }
        TigMessage input;
        while (tig_message_dequeue(&input) == TIG_OK) {
            if (input.type == TIG_MESSAGE_QUIT) good = false;
        }
        if (host && !reopened && view->revision == 2 && coop_dialogue_settled(dialogue)) {
            dialog_ui_end_dialog(coop_campaign_object(1), 0);
            object_script_execute(coop_campaign_object(0), fixture_npc, OBJ_HANDLE_NULL, SAP_DIALOG, 0);
            reopened = true;
        }
        if (host && !finishing && view->revision >= 5 && coop_dialogue_settled(dialogue)) {
            good = coop_dialogue_accepted(dialogue) == 2 && coop_gameplay_finish(held_gameplay);
            finishing = true;
        }
        if (coop_gameplay_finished(held_gameplay)) {
            if (!finished_at) finished_at = SDL_GetTicks();
            if (SDL_GetTicks() - finished_at > 600) break;
        }
        SDL_Delay(2);
    }
    if (fclose(transcript) != 0) good = false;
    const CoopDialogueView* view = coop_dialogue_current(dialogue);
    good = good && input_sent == 2 && recorded == 5 && coop_gameplay_finished(held_gameplay)
        && submissions + denials == 2;
    FILE* result = fopen("arcmp-dialogue-result.json", "wb");
    if (!result) good = false;
    if (result) {
        fprintf(result, "{\"verified\":%s,\"host\":%s,\"revision\":%u,\"conversation\":%u,\"chooser\":%u,\"submissions\":%u,\"observer_denials\":%u,\"accepted\":%u,\"rejected\":%u}\n",
            good ? "true" : "false", host ? "true" : "false", view->revision, view->conversation, view->chooser,
            submissions, denials, coop_dialogue_accepted(dialogue), coop_dialogue_rejected(dialogue));
        if (fclose(result) != 0) good = false;
    }
    fprintf(stderr, "ARCMP shared retail dialogue %s\n", good ? "PASS" : "FAIL");
    return good;
}

static bool bootstrap_probe(int argc, char** argv, bool movement, bool dialogue, bool training, bool framebuffer)
{
    bool host = false, owned_delete = false, raw_delete = false;
    const char* address = "127.0.0.1";
    unsigned port = 35742, player = COOP_MAX_PLAYERS;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-engine-probe=movement-owned-delete")) owned_delete = true;
        if (!strcmp(argv[i], "--coop-engine-probe=movement-owned-delete-raw")) owned_delete = raw_delete = true;
        if (!strncmp(argv[i], "--coop-probe-address=", 21)) address = argv[i] + 21;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (player >= COOP_MAX_PLAYERS || (host && player != 0) || (!host && player == 0) || !port || port > 65535) return fail("invalid bootstrap probe arguments");
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Bootstrap player %u", player);
    if (!read_content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return fail("bootstrap runtime has no valid content/profile");
    CoopSession* session = coop_session_create(&config);
    if (!session) return false;
    uint64_t start = SDL_GetTicks();
    bool good = host ? coop_session_host(session, (uint16_t)port, start)
                     : coop_session_join(session, address, (uint16_t)port, start);
    CoopBootstrap* bootstrap = good ? coop_bootstrap_create(session) : NULL;
    if (!bootstrap) good = false;
    bool selected = false, assembled = false, loaded = false;
    uint8_t* snapshot = NULL;
    uint64_t committed_at = 0;
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port);
    fflush(stderr);
    while (good && SDL_GetTicks() - start < 30000) {
        tig_ping();
        coop_session_poll(session, SDL_GetTicks());
        if (coop_session_phase(session) == COOP_OFFLINE && coop_bootstrap_phase(bootstrap) != COOP_BOOT_COMMITTED) {
            fail(coop_session_status(session));
            good = false;
            break;
        }
        if (coop_session_phase(session) == COOP_LOBBY && coop_bootstrap_phase(bootstrap) == COOP_BOOT_IDLE) {
            if (!selected) {
                char name[COOP_NAME_SIZE];
                snprintf(name, sizeof(name), "Network hero %u", player);
                selected = coop_session_character(session, 16067 + player, name);
            } else {
                const CoopPlayer* local = &coop_session_players(session)[coop_session_local_slot(session)];
                if (local->prototype && !local->ready) coop_session_ready(session, true);
            }
        }
        unsigned count = 0;
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
            count += coop_session_players(session)[i].connected;
        if (host && !assembled && count == 4 && coop_session_all_ready(session)) {
            good = coop_campaign_begin(coop_session_players(session));
            if (good) {
                /* Independent values track campaign slots, not connection arrival order. */
                for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
                    stat_base_set(coop_campaign_object(i), STAT_STRENGTH, 11 + (int)i);
                mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
                teleport_ping(0);
                script_global_var_set(0, 24601);
                good = verify_party();
                if (good && movement) {
                    good = move_party(map_by_type(MAP_TYPE_SHOPPING_MAP));
                    int64_t origin = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                    for (unsigned i = 0; good && i < COOP_MAX_PLAYERS; i++)
                        sub_43E770(coop_campaign_object(i), location_make(location_get_x(origin) + i * 3, location_get_y(origin)), 0, 0);
                }
            }
            size_t size = 0;
            if (good) snapshot = coop_engine_snapshot_capture(&size);
            good = good && snapshot && coop_bootstrap_begin(bootstrap, 1, snapshot, size, SDL_GetTicks());
            fprintf(stderr, "ARCMP bootstrap snapshot bytes=%zu\n", size);
            fflush(stderr);
            assembled = true;
        }
        if (!host && !loaded && coop_bootstrap_phase(bootstrap) == COOP_BOOT_RECEIVED) {
            size_t size;
            const uint8_t* bytes = coop_bootstrap_data(bootstrap, &size);
            good = coop_engine_snapshot_restore(bytes, size, config.identity) && verify_party()
                && script_global_var_get(0) == 24601
                && coop_campaign_slot_for_object(player_get_local_pc_obj()) == coop_campaign_slot_for_owner(config.identity);
            if (good) good = coop_bootstrap_loaded(bootstrap);
            if (!good) coop_bootstrap_cancel(bootstrap);
            loaded = true;
        }
        coop_bootstrap_poll(bootstrap, SDL_GetTicks());
        if (coop_bootstrap_phase(bootstrap) == COOP_BOOT_FAILED) {
            fail(coop_bootstrap_error(bootstrap));
            good = false;
        }
        if (coop_bootstrap_phase(bootstrap) == COOP_BOOT_COMMITTED) {
            if (!host || movement || dialogue || training) break;
            if (!committed_at) committed_at = SDL_GetTicks();
            /* Keep polling until queued commit frames reach every local peer. */
            if (SDL_GetTicks() - committed_at >= 500) break;
        }
        SDL_Delay(1);
    }
    good = good && bootstrap && coop_bootstrap_phase(bootstrap) == COOP_BOOT_COMMITTED
        && (host ? assembled : loaded) && verify_party();
    if (!good) fail("network campaign bootstrap did not commit");
    coop_bootstrap_destroy(bootstrap);
    if (good && movement) good = movement_probe(session, owned_delete, raw_delete);
    if (good && (dialogue || training)) {
        mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU);
        sub_5412D0();
        held_session = session;
        good = training ? coop_training_probe(session, framebuffer, &held_gameplay) : dialogue_probe(session);
    } else coop_session_destroy(session);
    free(snapshot);
    if (good && !dialogue && !training) {
        mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU);
        sub_5412D0();
    }
    return good;
}

int coop_engine_probe(int argc, char** argv)
{
    const char* mode = NULL;
    unsigned hold_ms = 0;
    bool journal = false;
    bool framebuffer = false;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--coop-engine-probe=", 20)) mode = argv[i] + 20;
        if (!strncmp(argv[i], "--coop-probe-hold=", 18)) hold_ms = (unsigned)atoi(argv[i] + 18);
        if (!strcmp(argv[i], "--coop-probe-journal")) journal = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strcmp(argv[i], "--coop-live-probe")) coop_runtime_probe_enable();
    }
    if (!mode) return -1;
    tig_movie_probe_mode(true);
    bool good = false;
    if (!strcmp(mode, "sector-identities")) {
        good = inspect_sector_ids(argc, argv);
    } else if (!strcmp(mode, "lobby-lan")) {
        good=coop_lobby_probe(argc,argv)==0;
    } else if (!strcmp(mode, "dropin-lobby")) {
        return coop_lobby_dropin_probe(framebuffer) ? 0 : 1;
    } else if (!strcmp(mode, "characters")) {
        good = coop_character_probe();
    } else if (!strcmp(mode, "character-ui")) {
        good = coop_character_ui_probe(0, framebuffer, NULL);
    } else if (!strcmp(mode, "preparation-ui")) {
        good = coop_lobby_preparation_probe(0, framebuffer, NULL);
    } else if (!strcmp(mode, "character-edit")) {
        good = coop_character_edit_probe(0, framebuffer, NULL);
    } else if (!strcmp(mode, "combat-engine")) {
        good = verify_combat_players(framebuffer);
    } else if (!strcmp(mode, "encounter-engine")) {
        good = verify_encounter_players(framebuffer);
    } else if (!strcmp(mode, "party-ai")) {
        good = verify_party_ai(framebuffer);
    } else if (!strcmp(mode, "spell-cache")) {
        good = verify_spell_cache();
    } else if (!strcmp(mode, "script-queries")) {
        good = assemble_party();
        if (good) {
            mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
            teleport_ping(0);
            coop_authority_set(COOP_AUTHORITY_HOST);
            for (unsigned i = coop_campaign_next_slot(0); good && i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
                good = script_campaign_queries_probe(coop_campaign_object(i));
        }
    } else if (!strcmp(mode, "script-regions")) {
        good = assemble_party();
        if (good) {
            mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
            teleport_ping(0);
            coop_authority_set(COOP_AUTHORITY_HOST);
            for (unsigned i = coop_campaign_next_slot(0); good && i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
                good = script_campaign_regions_probe(coop_campaign_object(i));
        }
    } else if (!strcmp(mode, "effect-references")) {
        good = verify_effect_references();
    } else if (!strcmp(mode, "sparse-party")) {
        unsigned mask = 9;
        for (int i = 1; i < argc; i++)
            if (!strncmp(argv[i], "--coop-probe-members=", 21)) mask = (unsigned)atoi(argv[i] + 21);
        good = verify_sparse_party(mask);
    } else if (!strcmp(mode, "dialogue-semantics")) {
        good = verify_dialogue_semantics();
    } else if (!strcmp(mode, "runtime-encounter")) {
        good = coop_encounter_probe(argc, argv);
    } else if ((!strcmp(mode, "runtime-campaign-progress") || !strcmp(mode, "runtime-regions") || !strcmp(mode, "runtime-quests") || !strcmp(mode, "runtime-voyage") || !strcmp(mode, "runtime-forest") || !strcmp(mode, "runtime-dark") || !strcmp(mode, "runtime-legacy") || !strcmp(mode, "runtime-finale") || !strcmp(mode, "runtime-traversal") || !strcmp(mode, "runtime-rescue") || !strcmp(mode, "runtime-evil") || !strcmp(mode, "runtime-evil-ending") || !strcmp(mode, "runtime-combat-finale"))) {
        good = coop_campaign_progress_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-ordinary")) {
        good = coop_ordinary_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-sustained")) {
        good = coop_sustained_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-sectors")) {
        good = coop_sector_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-reconnect-barriers") || !strcmp(mode, "runtime-reconnect-actions") || !strcmp(mode, "runtime-reconnect-journey")) {
        good = coop_reconnect_barrier_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-reconnect")) {
        good = coop_reconnect_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-environment") || !strcmp(mode, "runtime-environment-ui")) {
        good = coop_environment_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-continuity") || !strcmp(mode, "runtime-companion-ui")) {
        good = coop_continuity_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-story") || !strcmp(mode, "runtime-story-tail") || !strcmp(mode, "runtime-journey") || !strcmp(mode, "runtime-ending") || !strcmp(mode, "runtime-defeat")) {
        good = coop_story_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-native-tools") || !strcmp(mode, "runtime-preferences") || !strcmp(mode, "runtime-controls") || !strcmp(mode, "runtime-controls-tail") || !strcmp(mode, "runtime-controls-ui") || !strcmp(mode, "runtime-recover") || !strcmp(mode, "runtime-recover-ui")) {
        good = coop_controls_probe(argc, argv);
    } else if (!strcmp(mode, "runtime-callbacks") || !strcmp(mode, "runtime-service-recovery") || !strcmp(mode, "runtime-services") || !strcmp(mode, "runtime-commerce") || !strcmp(mode, "runtime-followers")) {
        good = coop_services_probe(argc, argv);
    } else if (!strncmp(mode, "runtime-combat", 14) || !strcmp(mode, "runtime-weapons") || !strcmp(mode, "runtime-magic") || !strcmp(mode, "runtime-use") || !strcmp(mode, "runtime-skills") || !strcmp(mode, "runtime-progression")) {
        good = coop_battle_probe(argc, argv);
    } else if (!strcmp(mode, "roundtrip") || !strcmp(mode, "objects")) {
        CoopCampaignMember before[COOP_MAX_PLAYERS];
        good = assemble_party();
        if (good) {
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
                before[i] = *coop_campaign_member(i);
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
                stat_base_set(coop_campaign_object(i), STAT_STRENGTH, 11 + (int)i);
            mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype);
            teleport_ping(0);
            good = map_current_map() == map_by_type(MAP_TYPE_START_MAP) && verify_party();
        }
        if (good && !strcmp(mode, "objects")) good = verify_pool_restoration() && verify_carried_coordinates() && coop_sector_persistence_probe() && verify_hidden_static_ids() && verify_story_fields() && verify_object_fields() && verify_world_fields() && verify_world_visuals();
        if (good) good = move_party(map_by_type(MAP_TYPE_SHOPPING_MAP)) && move_party(map_by_type(MAP_TYPE_START_MAP));
        if (good) {
            script_global_var_set(0, 24601);
            good = gamelib_save("SlotCoop", "Four-player ownership integration probe");
        }
        if (good) {
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
                stat_base_set(coop_campaign_object(i), STAT_STRENGTH, 3);
            script_global_var_set(0, 7);
            mainmenu_ui_reset();
            good = !coop_campaign_active() && gamelib_load("SlotCoop") && verify_party()
                && script_global_var_get(0) == 24601;
            for (unsigned i = 0; good && i < COOP_MAX_PLAYERS; i++) {
                const CoopCampaignMember* current = coop_campaign_member(i);
                good = objid_is_equal(before[i].object_id, current->object_id)
                    && !memcmp(before[i].owner, current->owner, COOP_IDENTITY_SIZE);
            }
        }
        if (good) good = verify_bad_manifests() && verify_rebinding() && verify_solo_compatibility();
    } else if (!strcmp(mode, "bootstrap")) {
        good = bootstrap_probe(argc, argv, false, false, false, framebuffer);
    } else if (!strcmp(mode, "movement") || !strcmp(mode, "movement-owned-delete") || !strcmp(mode, "movement-owned-delete-raw")) {
        good = bootstrap_probe(argc, argv, true, false, false, framebuffer);
    } else if (!strcmp(mode, "runtime") || !strcmp(mode, "runtime-loss") || (!strcmp(mode, "runtime-characters") || !strcmp(mode, "runtime-preparation"))
        || !strcmp(mode, "runtime-travel") || !strcmp(mode, "runtime-travel-loss") || !strcmp(mode, "runtime-travel-stale") || !strcmp(mode, "runtime-items") || !strcmp(mode, "runtime-loot") || !strcmp(mode, "runtime-loot-loss")) {
        good = coop_runtime_probe(argc, argv);
    } else if (!strcmp(mode, "dialogue")) {
        good = bootstrap_probe(argc, argv, false, true, false, framebuffer);
    } else if (!strcmp(mode, "training")) {
        good = bootstrap_probe(argc, argv, false, false, true, framebuffer);
    } else if (!strcmp(mode, "load")) {
        mainmenu_ui_reset();
        good = gamelib_load("SlotCoop") && verify_party() && script_global_var_get(0) == 24601;
        if (good) {
            mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU);
            sub_5412D0();
        }
    } else {
        fail("unknown probe mode");
    }
    if (!good) mainmenu_ui_cancel_party_character();
    /* The connected original ending writes its healthy party report before
     * the native ending clears it, then verifies the shared menu return. */
    bool connected_ending = !strcmp(mode,"runtime-ordinary") && good
        && coop_runtime_phase() == COOP_RUNTIME_OFF && !coop_campaign_active();
    if (good && !connected_ending && !(getenv("ARCMP_PROBE_NATIVE_FAILURE") && !strcmp(mode, "runtime-loss")) && strcmp(mode, "lobby-lan") && strcmp(mode, "preparation-ui") && strcmp(mode, "characters") && strcmp(mode, "character-ui") && strcmp(mode, "character-edit") && strcmp(mode, "runtime-ending") && strcmp(mode, "runtime-defeat") && strcmp(mode, "runtime-finale") && strcmp(mode, "runtime-evil-ending") && strcmp(mode, "runtime-combat-finale")) good = coop_engine_probe_report(mode);
    if (good && !strncmp(mode,"runtime",7)) {
        FILE* events=fopen("arcmp-play-events-result.json","wt");
        if (events) {
            uint32_t hash=2166136261u; unsigned combat=0,effects=0,world_pings=0,map_pings=0;
            for (uint32_t n=coop_play_event_first();n<coop_play_event_end();n++) {
                const PlayEvent* e=coop_play_event_at(n); if (!e) continue;
                combat+=e->kind==PLAY_COMBAT; effects+=e->kind==PLAY_EFFECT;
                if (e->kind==PLAY_PING) world_pings|=e->members;
                if (e->kind==PLAY_MAP_PING) map_pings|=e->members;
                const char* fields[]={e->summary,e->details};
                hash=(hash^e->sequence)*16777619u; hash=(hash^e->map)*16777619u; hash=(hash^e->kind)*16777619u;
                for (unsigned i=0;i<2;i++) for (const unsigned char* p=(const unsigned char*)fields[i];*p;p++) hash=(hash^*p)*16777619u;
            }
            fprintf(events,"{\"first\":%u,\"end\":%u,\"combat\":%u,\"effects\":%u,\"world_ping_members\":%u,\"map_ping_members\":%u,\"pause_policy\":%u,\"hash\":%u}\n",coop_play_event_first(),coop_play_event_end(),combat,effects,world_pings,map_pings,coop_play_pause_policy(),hash);
            fclose(events);
        }
    }
    fprintf(stderr, "ARCMP engine probe %s mode=%s count=%u map=%d\n", good ? "PASS" : "FAIL", mode, coop_campaign_count(), map_current_map());
    fflush(stderr);
    if (good && hold_ms && !connected_ending && !(getenv("ARCMP_PROBE_NATIVE_FAILURE") && !strcmp(mode, "runtime-loss")) && strcmp(mode, "preparation-ui") && strcmp(mode, "characters") && strcmp(mode, "character-ui") && strcmp(mode, "character-edit") && strcmp(mode, "runtime-ending") && strcmp(mode, "runtime-defeat") && strcmp(mode, "runtime-finale") && strcmp(mode, "runtime-evil-ending") && strcmp(mode, "runtime-combat-finale")) {
        scroll_set_center(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
        int64_t origin = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
        if (!strncmp(mode, "movement", 8)) {
            int64_t x = 0, y = 0;
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                int64_t position = obj_field_int64_get(coop_campaign_object(i), OBJ_F_LOCATION);
                x += location_get_x(position);
                y += location_get_y(position);
            }
            location_origin_set(location_make(x / COOP_MAX_PLAYERS, y / COOP_MAX_PLAYERS));
        }
        for (unsigned i = 0; strncmp(mode, "movement", 8) && strcmp(mode, "dialogue") && strcmp(mode, "training") && strncmp(mode, "runtime", 7) && i < COOP_MAX_PLAYERS; i++) {
            if (!coop_campaign_member(i)) continue;
            const int offset[] = { 0, -4, -2, 2 };
            int64_t position = location_make(location_get_x(origin) + offset[i], location_get_y(origin) - offset[i]);
            sub_43E770(coop_campaign_object(i), position, 0, 0);
        }
        iso_interface_refresh();
        intgame_show();
        if (journal) logbook_ui_open_quests(player_get_local_pc_obj());
        gamelib_redraw();
        if (journal || !strcmp(mode, "dialogue")) iso_redraw();
        tig_window_display();
        if (framebuffer && tig_video_screenshot_save("arcmp-probe.bmp") != TIG_OK) good = fail("could not capture the composed game frame");
        fprintf(stderr, "ARCMP engine visual ready\n");
        fflush(stderr);
        uint64_t until = SDL_GetTicks() + hold_ms;
        /* Keep game time paused during visual inspection of the verified state. */
        while (SDL_GetTicks() < until) {
            tig_ping();
            gamelib_redraw();
            if (journal || !strcmp(mode, "dialogue")) iso_redraw();
            tig_window_display();
            SDL_Delay(16);
        }
    }
    coop_gameplay_destroy(held_gameplay);
    held_gameplay = NULL;
    coop_session_destroy(held_session);
    held_session = NULL;
    if (!strncmp(mode, "runtime-combat", 14) || !strcmp(mode, "runtime-weapons") || !strcmp(mode, "runtime-magic") || !strcmp(mode, "runtime-use") || !strcmp(mode, "runtime-skills") || !strcmp(mode, "runtime-progression")) coop_battle_probe_cleanup();
    if (!strcmp(mode, "runtime-encounter")) coop_encounter_probe_cleanup();
    if (!strcmp(mode, "runtime-callbacks") || !strcmp(mode, "runtime-service-recovery") || !strcmp(mode, "runtime-services") || !strcmp(mode, "runtime-commerce") || !strcmp(mode, "runtime-followers")) coop_services_probe_cleanup();
    if (!strcmp(mode, "runtime-native-tools") || !strcmp(mode, "runtime-preferences") || !strcmp(mode, "runtime-controls") || !strcmp(mode, "runtime-controls-tail") || !strcmp(mode, "runtime-controls-ui") || !strcmp(mode, "runtime-recover") || !strcmp(mode, "runtime-recover-ui")) coop_controls_probe_cleanup();
    if (!strcmp(mode, "runtime-story") || !strcmp(mode, "runtime-story-tail") || !strcmp(mode, "runtime-journey") || !strcmp(mode, "runtime-ending") || !strcmp(mode, "runtime-defeat")) coop_story_probe_cleanup();
    if (!strcmp(mode, "runtime-continuity") || !strcmp(mode, "runtime-companion-ui")) coop_continuity_probe_cleanup();
    if (!strcmp(mode, "runtime-environment") || !strcmp(mode, "runtime-environment-ui")) coop_environment_probe_cleanup();
    if (!strncmp(mode, "runtime-reconnect", 17)) coop_reconnect_probe_cleanup();
    if (!strcmp(mode,"runtime-ordinary")) coop_ordinary_probe_cleanup();
    if (!strcmp(mode,"runtime-sectors") || !strcmp(mode,"runtime-sustained")) coop_sector_probe_cleanup();
    if ((!strcmp(mode, "runtime-campaign-progress") || !strcmp(mode, "runtime-regions") || !strcmp(mode, "runtime-quests") || !strcmp(mode, "runtime-voyage") || !strcmp(mode, "runtime-forest") || !strcmp(mode, "runtime-dark") || !strcmp(mode, "runtime-legacy") || !strcmp(mode, "runtime-finale") || !strcmp(mode, "runtime-traversal") || !strcmp(mode, "runtime-rescue") || !strcmp(mode, "runtime-evil") || !strcmp(mode, "runtime-evil-ending") || !strcmp(mode, "runtime-combat-finale"))) coop_campaign_progress_probe_cleanup();
    return good ? 0 : 1;
}
