#include "coop/world.h"
#include "coop/campaign.h"
#include "coop/engine_combat.h"
#include "coop/engine_documents.h"
#include "coop/engine_notes.h"
#include "coop/engine_magic.h"
#include "coop/engine_object.h"
#include "coop/engine_sector.h"
#include "coop/engine_story.h"
#include "game/location.h"
#include "game/gamelib.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/stat.h"
#include "game/townmap.h"
#include "ui/charedit_ui.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/schematic_ui.h"
#include "ui/wmap_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TrackedObject {
    CoopObjectId id;
    uint8_t* bytes;
    size_t size;
    bool present;
    bool sector_owned;
} TrackedObject;
struct CoopWorldTracker {
    int map;
    CoopSectorTracker* sectors;
    unsigned count;
    unsigned capacity;
    TrackedObject* objects;
    bool separate_pc_poses;
    uint32_t* lookup;
    size_t cached_bytes;
    uint8_t* story;
    size_t story_size;
    uint8_t* charts;
    size_t charts_size;
    uint8_t* documents;
    size_t documents_size;
    uint8_t* notes;
    size_t notes_size;
};
enum { WORLD_HEADER = 36 };
CoopWorldTracker* coop_world_tracker_create(bool separate_pc_poses)
{
    CoopWorldTracker* tracker = calloc(1, sizeof(*tracker));
    if (tracker) {
        tracker->map = map_current_map();
        tracker->sectors = coop_sector_tracker_create();
        tracker->separate_pc_poses = separate_pc_poses;
        tracker->lookup = calloc(COOP_WORLD_OBJECT_LIMIT * 2, sizeof(*tracker->lookup));
        if (!tracker->lookup || !tracker->sectors) {
            coop_sector_tracker_destroy(tracker->sectors);
            free(tracker->lookup);
            free(tracker);
            return NULL;
        }
    }
    return tracker;
}
void coop_world_tracker_destroy(CoopWorldTracker* tracker)
{
    if (!tracker) return;
    for (unsigned i = 0; i < tracker->count; i++)
        free(tracker->objects[i].bytes);
    free(tracker->objects);
    free(tracker->lookup);
    free(tracker->story);
    free(tracker->charts);
    free(tracker->documents);
    free(tracker->notes);
    coop_sector_tracker_destroy(tracker->sectors);
    free(tracker);
}
static int find_tracked(const CoopWorldTracker* tracker, CoopObjectId id)
{
    uint32_t hash = UINT32_C(2166136261);
    for (unsigned i = 0; i < sizeof(id); i++)
        hash = (hash ^ id.bytes[i]) * UINT32_C(16777619);
    for (unsigned probe = 0; probe < COOP_WORLD_OBJECT_LIMIT * 2; probe++) {
        unsigned bucket = (hash + probe) & (COOP_WORLD_OBJECT_LIMIT * 2 - 1);
        uint32_t entry = tracker->lookup[bucket];
        if (!entry) return -(int)bucket - 1;
        if (!memcmp(&tracker->objects[entry - 1].id, &id, sizeof(id))) return (int)entry - 1;
    }
    return -1;
}
/* Eviction is not destruction. Forget sector-owned static records without sending
 * deletions; native DIF files remain authoritative and reloads must republish.
 * Retire published GUID tombstones as well, so exploration stays bounded. */
static void forget_absent(CoopWorldTracker* tracker, bool sector_only)
{
    bool changed = false;
    for (unsigned i = 0; i < tracker->count;) {
        TrackedObject* object = &tracker->objects[i];
        if (object->present || (sector_only && !object->sector_owned)) {
            i++;
            continue;
        }
        tracker->cached_bytes -= object->size;
        free(object->bytes);
        *object = tracker->objects[--tracker->count];
        changed = true;
    }
    if (!changed) return;
    memset(tracker->lookup, 0, COOP_WORLD_OBJECT_LIMIT * 2 * sizeof(*tracker->lookup));
    for (unsigned i = 0; i < tracker->count; i++) {
        int bucket = find_tracked(tracker, tracker->objects[i].id);
        tracker->lookup[(unsigned)(-bucket - 1)] = i + 1;
    }
}
unsigned coop_world_tracked_objects(const CoopWorldTracker* tracker) { return tracker ? tracker->count : 0; }
static bool append(uint8_t** bytes, size_t* size, size_t* capacity, const void* data, size_t length)
{
    if (length > COOP_WORLD_LIMIT - *size) return false;
    if (*size + length > *capacity) {
        size_t next = *capacity;
        while (next < *size + length)
            next *= 2;
        if (next > COOP_WORLD_LIMIT) next = COOP_WORLD_LIMIT;
        uint8_t* resized = realloc(*bytes, next);
        if (!resized) return false;
        *bytes = resized;
        *capacity = next;
    }
    memcpy(*bytes + *size, data, length);
    *size += length;
    return true;
}
bool coop_world_capture(CoopWorldTracker* tracker, uint8_t** out, size_t* out_size)
{
    *out = NULL;
    *out_size = 0;
    if (!tracker || tracker->map != map_current_map()) return false;
    uint8_t* bytes = calloc(1, WORLD_HEADER);
    if (!bytes) return false;
    size_t size = WORLD_HEADER;
    size_t capacity = WORLD_HEADER;
    unsigned upserts = 0, deletions = 0, live_objects = 0;
    for (unsigned i = 0; i < tracker->count; i++)
        tracker->objects[i].present = false;
    bool good = true;
    int64_t handle;
    int iterator;
    if (obj_inst_first(&handle, &iterator)) do {
            live_objects++;
            CoopObjectId id;
            if (!coop_object_id_from_engine(obj_get_id(handle), &id)) {
                fprintf(stderr,"ARCMP world capture invalid identity handle=%lld type=%d\n",(long long)handle,obj_field_int32_get(handle,OBJ_F_TYPE));
                free(bytes);
                return false;
            }
            int index = find_tracked(tracker, id);
            if (index >= 0) tracker->objects[index].present = true;
        } while (obj_inst_next(&handle, &iterator));
    forget_absent(tracker, true);
    if (obj_inst_first(&handle, &iterator)) do {
            size_t length;
            uint8_t* object = coop_engine_object_export_fields(handle, &length, tracker->separate_pc_poses);
            CoopObjectId id;
            if (!object || !coop_object_id_from_engine(obj_get_id(handle), &id)) {
                fprintf(stderr,"ARCMP world capture object export failed handle=%lld type=%d name=%d flags=%x\n",(long long)handle,obj_field_int32_get(handle,OBJ_F_TYPE),obj_field_int32_get(handle,OBJ_F_NAME),obj_field_int32_get(handle,OBJ_F_FLAGS));
                free(object);
                good = false;
                break;
            }
            int index = find_tracked(tracker, id);
            if (index < 0) {
                unsigned bucket = (unsigned)(-index - 1);
                if (tracker->count == COOP_WORLD_OBJECT_LIMIT) {
                    free(object);
                    good = false;
                    break;
                }
                if (tracker->count == tracker->capacity) {
                    unsigned capacity = tracker->capacity ? tracker->capacity * 2 : 256;
                    if (capacity > COOP_WORLD_OBJECT_LIMIT) capacity = COOP_WORLD_OBJECT_LIMIT;
                    TrackedObject* grown = realloc(tracker->objects, capacity * sizeof(*grown));
                    if (!grown) {
                        free(object);
                        good = false;
                        break;
                    }
                    tracker->objects = grown;
                    tracker->capacity = capacity;
                }
                index = (int)tracker->count++;
                tracker->lookup[bucket] = (uint32_t)index + 1;
                tracker->objects[index] = (TrackedObject) { .id = id };
            }
            TrackedObject* tracked = &tracker->objects[index];
            tracked->present = true;
            tracked->sector_owned = object_is_static(handle);
            if (tracked->bytes && length == tracked->size && !memcmp(object, tracked->bytes, length)) {
                free(object);
                continue;
            }
            uint8_t prefix[4];
            coop_object_write32(prefix, (uint32_t)length);
            good = length <= COOP_WORLD_LIMIT - (tracker->cached_bytes - tracked->size)
                && append(&bytes, &size, &capacity, prefix, 4) && append(&bytes, &size, &capacity, object, length);
            if (!good) {
                free(object);
                break;
            }
            free(tracked->bytes);
            tracker->cached_bytes = tracker->cached_bytes - tracked->size + length;
            tracked->bytes = object;
            tracked->size = length;
            upserts++;
        } while (obj_inst_next(&handle, &iterator));
    for (unsigned i = 0; good && i < tracker->count; i++) {
        TrackedObject* tracked = &tracker->objects[i];
        if (tracked->present || !tracked->bytes || tracked->sector_owned) continue;
        good = append(&bytes, &size, &capacity, &tracked->id, sizeof(tracked->id));
        if (good) {
            free(tracked->bytes);
            tracker->cached_bytes -= tracked->size;
            tracked->bytes = NULL;
            tracked->size = 0;
            deletions++;
        }
    }
    if (!good) {
        fprintf(stderr,"ARCMP world capture object batch failed objects=%u live=%u cached=%zu bytes=%zu\n",tracker->count,live_objects,tracker->cached_bytes,size);
        free(bytes);
        return false;
    }
    forget_absent(tracker,false);
    CoopStoryState story;
    size_t story_size = 0;
    uint8_t* story_bytes = NULL;
    if (!coop_engine_story_capture(&story) || !(story_bytes = coop_story_state_encode(&story, &story_size))) {
        fprintf(stderr,"ARCMP world capture story failed\n");
        free(bytes);
        return false;
    }
    bool changed = !tracker->story || story_size != tracker->story_size || memcmp(story_bytes, tracker->story, story_size);
    if (changed) {
        if (!append(&bytes, &size, &capacity, story_bytes, story_size)) {
            free(story_bytes);
            free(bytes);
            return false;
        }
        free(tracker->story);
        tracker->story = story_bytes;
        tracker->story_size = story_size;
    } else {
        free(story_bytes);
    }
    uint8_t* sector_bytes = NULL;
    size_t sector_size = 0;
    if (!coop_sector_capture(tracker->sectors, &sector_bytes, &sector_size)
        || (sector_size && !append(&bytes, &size, &capacity, sector_bytes, sector_size))) {
        fprintf(stderr,"ARCMP world capture sectors failed bytes=%zu\n",sector_size);
        free(sector_bytes); free(bytes); return false;
    }
    free(sector_bytes);
    size_t charts_size = 0;
    uint8_t* charts = townmap_campaign_capture(&charts_size);
    if (!charts) {
        fprintf(stderr,"ARCMP world capture charts failed\n");
        free(bytes);
        return false;
    }
    bool chart_change = !tracker->charts || charts_size != tracker->charts_size || memcmp(charts, tracker->charts, charts_size);
    if (chart_change) {
        if (!append(&bytes, &size, &capacity, charts, charts_size)) {
            free(charts);
            free(bytes);
            return false;
        }
        free(tracker->charts);
        tracker->charts = charts;
        tracker->charts_size = charts_size;
    } else
        free(charts);
    size_t documents_size = 0;
    uint8_t* documents = coop_documents_encode(coop_engine_documents(), &documents_size);
    if (!documents) {
        fprintf(stderr,"ARCMP world capture documents failed\n");
        free(bytes);
        return false;
    }
    bool documents_change = !tracker->documents || documents_size != tracker->documents_size || memcmp(documents, tracker->documents, documents_size);
    if (documents_change) {
        if (!append(&bytes, &size, &capacity, documents, documents_size)) {
            free(documents);
            free(bytes);
            return false;
        }
        free(tracker->documents);
        tracker->documents = documents;
        tracker->documents_size = documents_size;
    } else
        free(documents);
    size_t notes_size = 0;
    uint8_t* notes = coop_map_notes_encode(coop_engine_notes(), &notes_size);
    if (!notes) { fprintf(stderr,"ARCMP world capture map notes failed\n"); free(bytes); return false; }
    bool notes_change = !tracker->notes || notes_size != tracker->notes_size || memcmp(notes, tracker->notes, notes_size);
    if (notes_change) {
        if (!append(&bytes, &size, &capacity, notes, notes_size)) { free(notes); free(bytes); return false; }
        free(tracker->notes); tracker->notes = notes; tracker->notes_size = notes_size;
    } else free(notes);
    if (!upserts && !deletions && !changed && !sector_size && !chart_change && !documents_change && !notes_change) {
        free(bytes);
        return true;
    }
    memcpy(bytes, "ACW6", 4);
    coop_object_write32(bytes + 4, (uint32_t)tracker->map);
    coop_object_write32(bytes + 8, upserts);
    coop_object_write32(bytes + 12, deletions);
    coop_object_write32(bytes + 16, changed ? (uint32_t)story_size : 0);
    coop_object_write32(bytes + 20, (uint32_t)sector_size);
    coop_object_write32(bytes + 24, chart_change ? (uint32_t)charts_size : 0);
    coop_object_write32(bytes + 28, documents_change ? (uint32_t)documents_size : 0);
    coop_object_write32(bytes + 32, notes_change ? (uint32_t)notes_size : 0);
    *out = bytes;
    *out_size = size;
    return true;
}
typedef struct StagedEntry {
    CoopStagedObject* state;
    ObjectID id;
    int64_t handle;
    bool created;
} StagedEntry;
static bool header(const void* data, size_t size)
{
    const uint8_t* p = data;
    return p && size >= WORLD_HEADER && size <= COOP_WORLD_LIMIT && !memcmp(p, "ACW6", 4)
        && coop_object_read32(p + 8) <= COOP_WORLD_OBJECT_LIMIT
        && coop_object_read32(p + 12) <= COOP_WORLD_OBJECT_LIMIT;
}
unsigned coop_world_upserts(const void* data, size_t size) { return header(data, size) ? coop_object_read32((const uint8_t*)data + 8) : 0; }
unsigned coop_world_deletions(const void* data, size_t size) { return header(data, size) ? coop_object_read32((const uint8_t*)data + 12) : 0; }
bool coop_world_apply(const void* data, size_t size)
{
    int64_t local_actor = player_get_local_pc_obj();
    int previous_level = local_actor ? stat_base_get(local_actor, STAT_LEVEL) : 0;
    unsigned previous_spells = local_actor ? (unsigned)obj_field_int32_get(local_actor, OBJ_F_SPELL_FLAGS) : 0;
    if (!header(data, size)) {
        fprintf(stderr, "ARCMP world preflight rejected header\n");
        return false;
    }
    const uint8_t* bytes = data;
    if (coop_object_read32(bytes + 4) != (uint32_t)map_current_map()) {
        fprintf(stderr, "ARCMP world preflight rejected map\n");
        return false;
    }
    size_t story_size = coop_object_read32(bytes + 16);
    size_t sector_size = coop_object_read32(bytes + 20);
    size_t chart_size = coop_object_read32(bytes + 24);
    size_t documents_size = coop_object_read32(bytes + 28);
    size_t notes_size = coop_object_read32(bytes + 32);
    CoopMapNotes* notes = notes_size ? malloc(sizeof(*notes)) : NULL;
    if (notes_size > size - WORLD_HEADER || (notes_size && (!notes || !coop_map_notes_decode(notes, bytes + size - notes_size, notes_size)
        || !coop_engine_notes_matches(notes)))) {
        fprintf(stderr, "ARCMP world preflight rejected map notes\n"); free(notes); return false;
    }
    /* Preflight has no native mutations; installation occurs with the rest of
     * the world after object references are validated. Keep the decoded view
     * on the heap to avoid a large Windows engine-thread stack frame. */
    size -= notes_size;
    CoopDocuments documents;
    if (documents_size > size - WORLD_HEADER || (documents_size && (!coop_documents_decode(&documents, bytes + size - documents_size, documents_size) || !coop_engine_documents_matches(&documents)))) {
        fprintf(stderr, "ARCMP world preflight rejected documents\n");
        free(notes); return false;
    }
    size -= documents_size;
    CoopStoryState story;
    if (chart_size > size - WORLD_HEADER || sector_size > size - WORLD_HEADER - chart_size || story_size > size - WORLD_HEADER - sector_size - chart_size
        || (story_size && (!coop_story_state_decode(bytes + size - chart_size - sector_size - story_size, story_size, &story) || !coop_engine_story_matches_content(&story)))) {
        fprintf(stderr, "ARCMP world preflight rejected story\n");
        free(notes); return false;
    }
    CoopCartography charts = { 0 };
    if (chart_size && (!coop_cartography_decode(&charts, bytes + size - chart_size, chart_size) || !townmap_campaign_matches(&charts))) {
        fprintf(stderr, "ARCMP world preflight rejected charts\n");
        coop_cartography_clear(&charts);
        free(notes); return false;
    }
    CoopSectorBatch* sectors = sector_size ? coop_sector_stage(bytes + size - chart_size - sector_size, sector_size) : NULL;
    if (sector_size && !sectors) {
        fprintf(stderr, "ARCMP world preflight rejected sectors bytes=%zu\n", sector_size);
        coop_cartography_clear(&charts);
        free(notes); return false;
    }
    size -= story_size + sector_size + chart_size;
    unsigned count = coop_world_upserts(data, size), deleted = coop_world_deletions(data, size);
    StagedEntry* entries = calloc(count ? count : 1, sizeof(*entries));
    ObjectID* removals = calloc(deleted ? deleted : 1, sizeof(*removals));
    if (!entries || !removals) {
        free(entries);
        free(removals);
        coop_sector_discard(sectors);
        coop_cartography_clear(&charts);
        free(notes); return false;
    }
    size_t at = WORLD_HEADER;
    bool good = true;
    const char* phase = "object schema/identity/placement";
    int64_t limit_x, limit_y;
    location_limits_get(&limit_x, &limit_y);
    for (unsigned i = 0; good && i < count; i++) {
        if (size - at < 4) {
            good = false;
            break;
        }
        size_t length = coop_object_read32(bytes + at);
        at += 4;
        if (length > size - at) {
            good = false;
            break;
        }
        StagedEntry* entry = &entries[i];
        entry->state = coop_engine_object_stage(bytes + at, length);
        at += length;
        if (!entry->state) {
            good = false;
            break;
        }
        entry->id = coop_engine_object_id(entry->state);
        entry->handle = obj_pool_perm_lookup(entry->id);
        int offset_x, offset_y;
        int64_t location = coop_engine_object_location(entry->state, &offset_x, &offset_y);
        good = location_get_x(location) < limit_x && location_get_y(location) < limit_y
            && offset_x >= -1024 && offset_x <= 1024 && offset_y >= -1024 && offset_y <= 1024
            && (!entry->handle || coop_engine_object_matches(entry->state, entry->handle));
        for (unsigned j = 0; good && j < i; j++)
            if (objid_is_equal(entries[j].id, entry->id)) good = false;
        if (entry->id.type == OID_TYPE_P && !entry->handle) good = false;
        if (!good) {
            char id[OID_STR_LEN];
            objid_id_to_str(id, entry->id);
            fprintf(stderr, "ARCMP rejected object index=%u id=%s handle=%" PRId64 " x=%" PRId64 " y=%" PRId64 " limits=%" PRId64 ",%" PRId64 " offsets=%d,%d matches=%d\n",
                i, id, entry->handle, location_get_x(location), location_get_y(location), limit_x, limit_y, offset_x, offset_y,
                entry->handle ? coop_engine_object_matches(entry->state, entry->handle) : -1);
            if (entry->handle) {
                char expected[OID_STR_LEN], actual[OID_STR_LEN], actual_id[OID_STR_LEN];
                objid_id_to_str(expected, coop_engine_object_prototype(entry->state));
                objid_id_to_str(actual, obj_get_id(obj_field_handle_get(entry->handle, OBJ_F_PROTOTYPE_HANDLE)));
                objid_id_to_str(actual_id, obj_get_id(entry->handle));
                fprintf(stderr, "ARCMP identity expected-prototype=%s actual-prototype=%s actual-id=%s\n", expected, actual, actual_id);
            }
        }
    }
    if (good) phase = "deletion identity";
    for (unsigned i = 0; good && i < deleted; i++) {
        if (size - at < COOP_OBJECT_ID_BYTES) {
            good = false;
            break;
        }
        CoopObjectId id;
        memcpy(&id, bytes + at, sizeof(id));
        at += sizeof(id);
        if (!coop_object_id_valid(&id, false) || id.bytes[0] != 2) {
            good = false;
            break;
        }
        removals[i] = coop_object_id_to_engine(id);
        int64_t handle = obj_pool_perm_lookup(removals[i]);
        if (handle && (obj_is_proto(handle) || coop_campaign_slot_for_object(handle) >= 0)) {
            good = false;
            break;
        }
        for (unsigned j = 0; good && j < count; j++)
            if (objid_is_equal(entries[j].id, removals[i])) good = false;
        for (unsigned j = 0; good && j < i; j++)
            if (objid_is_equal(removals[j], removals[i])) good = false;
    }
    if (good) phase = "exact batch size";
    good = good && at == size;
    if (good) phase = "references to deleted objects";
    if (good && deleted) {
        int64_t handle;
        int iterator;
        if (obj_inst_first(&handle, &iterator)) do {
                ObjectID id = obj_get_id(handle);
                bool replaced = false;
                for (unsigned i = 0; i < count; i++)
                    replaced |= objid_is_equal(id, entries[i].id);
                for (unsigned i = 0; i < deleted; i++)
                    replaced |= objid_is_equal(id, removals[i]);
                if (!replaced && coop_engine_object_refers_to(handle, removals, deleted)) {
                    good = false;
                    break;
                }
            } while (obj_inst_next(&handle, &iterator));
    }
    if (good) phase = "object allocation";
    for (unsigned i = 0; good && i < count; i++) {
        StagedEntry* entry = &entries[i];
        if (entry->handle) continue;
        int x, y;
        int64_t location = coop_engine_object_location(entry->state, &x, &y);
        int64_t prototype = obj_pool_perm_lookup(coop_engine_object_prototype(entry->state));
        obj_create_inst_with_oid(prototype, location, entry->id, &entry->handle);
        entry->created = true;
        if (!entry->handle) good = false;
    }
    if (good) phase = "object reference resolution";
    for (unsigned i = 0; good && i < count; i++)
        good = coop_engine_object_resolve_except(entries[i].state, removals, deleted);
    if (good && story_size) { phase = "combat turn resolution"; good = coop_engine_combat_resolve(&story.combat, removals, deleted); }
    if (good && story_size) { phase = "maintained spell resolution"; good = coop_engine_magic_resolve(&story.magic, removals, deleted); }
    if (good) {
        coop_sector_install(sectors);
        for (unsigned i = 0; i < count; i++)
            if (!entries[i].created) object_replica_detach(entries[i].handle);
        for (unsigned i = 0; i < count; i++)
            coop_engine_object_install(entries[i].state, entries[i].handle);
        for (unsigned i = 0; i < deleted; i++) {
            int64_t handle = obj_pool_perm_lookup(removals[i]);
            if (handle) {
                object_replica_detach(handle);
                object_delete(handle);
            }
        }
        if (story_size) good = coop_engine_story_install(&story);
        if (chart_size) townmap_campaign_install(&charts);
        if (documents_size) coop_engine_documents_install(&documents);
        if (notes_size) coop_engine_notes_install(notes);
        for (unsigned i = 0; i < count; i++)
            object_replica_attach(entries[i].handle);
        inven_ui_coop_refresh();
        schematic_ui_coop_refresh();
        charedit_refresh();
        if (good) charedit_coop_level_applied(previous_level);
        if (good) intgame_counters_refresh();
        if (good && local_actor && previous_spells != (unsigned)obj_field_int32_get(local_actor, OBJ_F_SPELL_FLAGS)) gamelib_invalidate_rect(NULL);
        if (good && chart_size) wmap_ui_coop_refresh();
    } else {
        fprintf(stderr, "ARCMP world batch rejected phase=%s upserts=%u deletions=%u parsed=%zu bytes=%zu\n", phase, count, deleted, at, size);
        for (unsigned i = 0; i < count; i++)
            if (entries[i].created) obj_deallocate(entries[i].handle);
    }
    for (unsigned i = 0; i < count; i++)
        coop_engine_object_discard(entries[i].state);
    free(entries);
    free(removals);
    coop_sector_discard(sectors);
    coop_cartography_clear(&charts);
    free(notes);
    return good;
}
