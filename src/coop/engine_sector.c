#include "coop/engine_sector.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/object_state.h"
#include "coop/sector_state.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/player.h"
#include "game/roof.h"
#include "game/sector.h"
#include "game/tile.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { CAPACITY = 128,
    ACTIVE_LIMIT = 36 };
struct CoopSectorStream {
    int map;
    unsigned count;
    int64_t ids[ACTIVE_LIMIT];
    int64_t local_location;
};
typedef struct SectorRecord {
    int64_t id;
    uint8_t* bytes;
    size_t size;
} SectorRecord;
struct CoopSectorTracker {
    SectorRecord records[CAPACITY];
    unsigned count;
};
struct CoopSectorBatch {
    unsigned count, locked;
    CoopSectorState** states;
    Sector** sectors;
    Light*** lights;
};
CoopSectorStream* coop_sector_stream_create(void)
{
    if (!sector_cache_reserve(CAPACITY)) return NULL;
    CoopSectorStream* stream = calloc(1, sizeof(*stream));
    if (stream) stream->map = map_current_map();
    if (stream && !coop_sector_stream_poll(stream)) {
        coop_sector_stream_destroy(stream);
        return NULL;
    }
    return stream;
}
bool coop_sector_stream_poll(CoopSectorStream* stream)
{
    if (!stream || stream->map != map_current_map()) return false;
    int64_t wanted[ACTIVE_LIMIT], max_x, max_y;
    unsigned count = 0;
    sector_limits_get(&max_x, &max_y);
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc = coop_campaign_object(i);
        if (!pc) return false;
        int64_t centre = sector_id_from_loc(obj_field_int64_get(pc, OBJ_F_LOCATION));
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int64_t x = SECTOR_X(centre) + dx, y = SECTOR_Y(centre) + dy;
                if (x < 0 || y < 0 || x >= max_x || y >= max_y) continue;
                int64_t id = SECTOR_MAKE(x, y);
                unsigned j = 0;
                while (j < count && wanted[j] != id)
                    j++;
                if (j == count) wanted[count++] = id;
            }
    }
    /* Acquire the next complete union before releasing the previous one. */
    unsigned acquired = 0;
    for (; acquired < count; acquired++) {
        Sector* sector;
        if (!sector_lock(wanted[acquired], &sector)) break;
    }
    if (acquired != count) {
        while (acquired)
            sector_unlock(wanted[--acquired]);
        return false;
    }
    bool changed = count != stream->count || memcmp(stream->ids, wanted, count * sizeof(*wanted));
    for (unsigned i = 0; i < stream->count; i++)
        sector_unlock(stream->ids[i]);
    memcpy(stream->ids, wanted, count * sizeof(*wanted));
    stream->count = count;
    int64_t local = player_get_local_pc_obj();
    if (local) {
        int64_t location = obj_field_int64_get(local, OBJ_F_LOCATION);
        if (changed || location != stream->local_location) roof_coop_rebuild(location);
        stream->local_location = location;
    }
    return true;
}
void coop_sector_stream_destroy(CoopSectorStream* stream)
{
    if (!stream) return;
    if (stream->map == map_current_map())
        for (unsigned i = 0; i < stream->count; i++)
            sector_unlock(stream->ids[i]);
    free(stream);
}
unsigned coop_sector_stream_count(const CoopSectorStream* stream) { return stream ? stream->count : 0; }
CoopSectorTracker* coop_sector_tracker_create(void) { return calloc(1, sizeof(CoopSectorTracker)); }
void coop_sector_tracker_destroy(CoopSectorTracker* tracker)
{
    if (!tracker) return;
    for (unsigned i = 0; i < tracker->count; i++)
        free(tracker->records[i].bytes);
    free(tracker);
}
/* sector_enumerate deliberately forbids sector_lock; only collect IDs here. */
static int64_t collected[CAPACITY];
static unsigned collected_count;
static bool collect(Sector* sector)
{
    if (collected_count == CAPACITY) return false;
    collected[collected_count++] = sector->id;
    return true;
}
static int light_compare(const void* a, const void* b)
{
    const CoopSectorLight *x = a, *y = b;
    const uint32_t first[] = { x->tile, x->offset_x, x->offset_y, x->flags, x->art, x->color };
    const uint32_t second[] = { y->tile, y->offset_x, y->offset_y, y->flags, y->art, y->color };
    for (unsigned i = 0; i < 6; i++)
        if (first[i] != second[i]) return first[i] < second[i] ? -1 : 1;
    return 0;
}
static bool capture_visuals(Sector* sector, CoopSectorState* s)
{
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++) {
        tig_art_id_t art = sector->roofs.art_ids[i];
        s->roofs[i] = art == TIG_ART_ID_INVALID ? art : art & ~0x3000u;
    }
    s->lights = 0;
    for (SectorBlockListNode* node = sector->lights.head; node; node = node->next) {
        Light* light = node->data;
        if (!sub_4DD110(light)) continue;
        if (s->lights == COOP_SECTOR_LIGHT_LIMIT) return false;
        unsigned flags = light->flags & 0x3bu;
        if (flags & LF_INDOOR) flags &= ~LF_OUTDOOR;
        tig_art_id_t art = light->art_id;
        /* Animation phase and ambient-derived RGB are local render state. */
        if (!(flags & LF_00000020)) art = tig_art_id_frame_set(art, 0);
        s->static_lights[s->lights++] = (CoopSectorLight) {
            tile_id_from_loc(light->loc), (uint32_t)light->offset_x, (uint32_t)light->offset_y,
            flags, art, (flags & (LF_INDOOR | LF_OUTDOOR)) ? 0 : (uint32_t)light->r << 16 | (uint32_t)light->g << 8 | light->b
        };
    }
    qsort(s->static_lights, s->lights, sizeof(*s->static_lights), light_compare);
    return true;
}
static void free_lights(Light** lights, unsigned count)
{
    if (!lights) return;
    for (unsigned i = 0; i < count; i++)
        if (lights[i]) sub_4D9570(lights[i]);
    free(lights);
}
static bool prepare_lights(Sector* sector, const CoopSectorState* s, Light*** result)
{
    *result = NULL;
    CoopSectorState* current = calloc(1, sizeof(*current));
    if (!current) return false;
    bool same = capture_visuals(sector, current) && current->lights == s->lights
        && !memcmp(current->static_lights, s->static_lights, s->lights * sizeof(*s->static_lights));
    free(current);
    if (same) return true;
    /* A nonnull, empty replacement also represents deletion of all lights. */
    Light** lights = calloc(s->lights + 1, sizeof(*lights));
    if (!lights) return false;
    int64_t origin = sector_loc_from_id(SECTOR_MAKE(s->x, s->y));
    for (unsigned i = 0; i < s->lights; i++) {
        const CoopSectorLight* l = &s->static_lights[i];
        int64_t loc = location_make(location_get_x(origin) + (l->tile & 63), location_get_y(origin) + (l->tile >> 6));
        lights[i] = light_static_create(loc, (int32_t)l->offset_x, (int32_t)l->offset_y, l->flags, l->art, l->color);
        if (!lights[i]) {
            free_lights(lights, s->lights);
            return false;
        }
    }
    *result = lights;
    return true;
}
static void install_visuals(Sector* sector, const CoopSectorState* s, Light** lights)
{
    sector->roofs.empty = true;
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++) {
        tig_art_id_t before = sector->roofs.art_ids[i], art = s->roofs[i];
        /* Preserve this process's cutaway when the structural piece is equal. */
        if (art != TIG_ART_ID_INVALID && (before & ~0x3000u) == art) art |= before & 0x3000u;
        sector->roofs.art_ids[i] = art;
        if (art != TIG_ART_ID_INVALID) sector->roofs.empty = false;
    }
    if (!lights) return;
    for (SectorBlockListNode* node = sector->lights.head; node;) {
        SectorBlockListNode* next = node->next;
        Light* light = node->data;
        if (sub_4DD110(light)) {
            sector_light_list_remove(&sector->lights, light);
            sub_4D9570(light);
        }
        node = next;
    }
    for (unsigned i = 0; i < s->lights; i++) {
        sector_light_list_add(&sector->lights, lights[i]);
        lights[i] = NULL;
    }
}
static CoopSectorState* capture(Sector* sector)
{
    CoopSectorState* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (!capture_visuals(sector, s)) {
        free(s);
        return NULL;
    }
    s->x = (uint32_t)SECTOR_X(sector->id);
    s->y = (uint32_t)SECTOR_Y(sector->id);
    s->townmap = (uint32_t)sector->townmap_info;
    s->aptitude = (uint32_t)sector->aptitude_adj;
    s->light = (uint32_t)sector->light_scheme;
    s->sound_flags = sector->sounds.flags & ~0x08000000u;
    s->music = (uint32_t)sector->sounds.music_scheme_idx;
    s->ambient = (uint32_t)sector->sounds.ambient_scheme_idx;
    s->script_flags = sector->sector_scripts.scr.hdr.flags;
    s->script_counters = sector->sector_scripts.scr.hdr.counters;
    s->script_number = (uint32_t)sector->sector_scripts.scr.num;
    s->blocked = sector_is_blocked(sector->id);
    memcpy(s->tiles, sector->tiles.art_ids, sizeof(s->tiles));
    memcpy(s->blocks, sector->blocks.mask, sizeof(s->blocks));
    for (TileScriptListNode* node = sector->tile_scripts.head; node; node = node->next) {
        if (s->scripts == COOP_SECTOR_SCRIPT_LIMIT) {
            free(s);
            return NULL;
        }
        s->tile_scripts[s->scripts++] = (CoopSectorScript) { node->id, node->scr.hdr.flags, node->scr.hdr.counters, (uint32_t)node->scr.num };
    }
    return s;
}
bool coop_sector_capture(CoopSectorTracker* tracker, uint8_t** data, size_t* size)
{
    *data = NULL;
    *size = 0;
    if (!tracker) return false;
    collected_count = 0;
    sector_enumerate(collect);
    /* Forget evicted regions. A later native reload must republish its state. */
    for (unsigned i = 0; i < tracker->count;) {
        bool loaded = false;
        for (unsigned j = 0; j < collected_count; j++)
            loaded |= tracker->records[i].id == collected[j];
        if (!loaded) {
            free(tracker->records[i].bytes);
            tracker->records[i] = tracker->records[--tracker->count];
        } else
            i++;
    }
    uint8_t* result = calloc(1, 4);
    if (!result) return false;
    size_t length = 4;
    unsigned changed = 0;
    for (unsigned i = 0; i < collected_count; i++) {
        Sector* sector;
        if (!sector_lock(collected[i], &sector)) {
            free(result);
            return false;
        }
        CoopSectorState* s = capture(sector);
        sector_unlock(collected[i]);
        size_t n = 0;
        uint8_t* bytes = s ? coop_sector_encode(s, &n) : NULL;
        free(s);
        if (!bytes) {
            free(result);
            return false;
        }
        unsigned index = 0;
        while (index < tracker->count && tracker->records[index].id != collected[i])
            index++;
        if (index == tracker->count) tracker->records[tracker->count++] = (SectorRecord) { .id = collected[i] };
        SectorRecord* r = &tracker->records[index];
        if (r->bytes && n == r->size && !memcmp(r->bytes, bytes, n)) {
            free(bytes);
            continue;
        }
        uint8_t* grown = realloc(result, length + 4 + n);
        if (!grown) {
            free(bytes);
            free(result);
            return false;
        }
        result = grown;
        coop_object_write32(result + length, (uint32_t)n);
        memcpy(result + length + 4, bytes, n);
        length += 4 + n;
        changed++;
        free(r->bytes);
        r->bytes = bytes;
        r->size = n;
    }
    if (!changed) {
        free(result);
        return true;
    }
    coop_object_write32(result, changed);
    *data = result;
    *size = length;
    return true;
}
CoopSectorBatch* coop_sector_stage(const void* data, size_t size)
{
    const uint8_t* p = data;
    if (!p || size < 4) return NULL;
    unsigned count = coop_object_read32(p);
    if (!count || count > CAPACITY) return NULL;
    CoopSectorBatch* b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->count = count;
    b->states = calloc(count, sizeof(*b->states));
    b->sectors = calloc(count, sizeof(*b->sectors));
    b->lights = calloc(count, sizeof(*b->lights));
    if (!b->states || !b->sectors || !b->lights) {
        coop_sector_discard(b);
        return NULL;
    }
    size_t at = 4;
    int64_t max_x, max_y;
    sector_limits_get(&max_x, &max_y);
    for (unsigned i = 0; i < count; i++) {
        if (size - at < 4) goto bad;
        size_t n = coop_object_read32(p + at);
        at += 4;
        b->states[i] = calloc(1, sizeof(*b->states[i]));
        CoopSectorState* s = b->states[i];
        if (n > size - at || !s || !coop_sector_decode(p + at, n, s) || s->x >= max_x || s->y >= max_y) goto bad;
        at += n;
        for (unsigned j = 0; j < i; j++)
            if (s->x == b->states[j]->x && s->y == b->states[j]->y) goto bad;
    }
    if (at != size || !sector_cache_reserve(CAPACITY)) goto bad;
    for (; b->locked < count; b->locked++) {
        CoopSectorState* s = b->states[b->locked];
        if (!sector_lock(SECTOR_MAKE(s->x, s->y), &b->sectors[b->locked])) goto bad;
    }
    for (unsigned i = 0; i < count; i++)
        if (!prepare_lights(b->sectors[i], b->states[i], &b->lights[i])) goto bad;
    return b;
bad:
    coop_sector_discard(b);
    return NULL;
}
void coop_sector_install(CoopSectorBatch* b)
{
    if (!b) return;
    for (unsigned i = 0; i < b->count; i++) {
        CoopSectorState* s = b->states[i];
        Sector* sector = b->sectors[i];
        install_visuals(sector, s, b->lights[i]);
        for (unsigned tile = 0; tile < COOP_SECTOR_TILES; tile++)
            if (sector->tiles.art_ids[tile] != s->tiles[tile]) {
                sector->tiles.art_ids[tile] = s->tiles[tile];
                sector->tiles.difmask[tile / 32] |= 1u << (tile % 32);
                sector->tiles.dif = true;
            }
        if (memcmp(sector->blocks.mask, s->blocks, sizeof(s->blocks))) {
            memcpy(sector->blocks.mask, s->blocks, sizeof(s->blocks));
            sector->blocks.modified = true;
        }
        sector->townmap_info = (int)s->townmap;
        sector->aptitude_adj = (int)s->aptitude;
        sector->light_scheme = (int)s->light;
        sector->flags |= SECTOR_CHANGED_MASK;
        sector->sounds.flags = s->sound_flags | 0x08000000u;
        sector->sounds.music_scheme_idx = (int)s->music;
        sector->sounds.ambient_scheme_idx = (int)s->ambient;
        Script script = { .hdr = { s->script_flags, s->script_counters }, .num = (int)s->script_number };
        sector_script_list_set(&sector->sector_scripts, &script);
        /* Keep a zero-script tombstone for native DIF persistence of removals. */
        for (TileScriptListNode* node = sector->tile_scripts.head; node; node = node->next) {
            bool found = false;
            for (unsigned j = 0; j < s->scripts; j++)
                found |= s->tile_scripts[j].tile == node->id;
            if (!found) {
                node->scr = (Script) { 0 };
                node->flags |= TILE_SCRIPT_LIST_NODE_MODIFIED;
                sector->tile_scripts.flags |= TILE_SCRIPT_LIST_MODIFIED;
            }
        }
        for (unsigned j = 0; j < s->scripts; j++) {
            CoopSectorScript* t = &s->tile_scripts[j];
            script = (Script) { .hdr = { t->flags, t->counters }, .num = (int)t->number };
            tile_script_list_set(&sector->tile_scripts, t->tile, &script);
        }
        sector_block_set(sector->id, s->blocked);
    }
    object_invalidate_rect(NULL);
    int64_t local = player_get_local_pc_obj();
    if (local) roof_coop_rebuild(obj_field_int64_get(local, OBJ_F_LOCATION));
}
void coop_sector_discard(CoopSectorBatch* b)
{
    if (!b) return;
    for (unsigned i = 0; i < b->locked; i++)
        sector_unlock(b->sectors[i]->id);
    if (b->states)
        for (unsigned i = 0; i < b->count; i++) {
            if (b->lights) free_lights(b->lights[i], b->states[i] ? b->states[i]->lights : 0);
            free(b->states[i]);
        }
    free(b->lights);
    free(b->states);
    free(b->sectors);
    free(b);
}

bool coop_sector_visual_save(Sector* sector, const char* directory)
{
    if (!coop_campaign_active()) return true;
    CoopSectorState* state = capture(sector);
    size_t size = 0;
    uint8_t* bytes = state ? coop_sector_visual_encode(state, &size) : NULL;
    free(state);
    if (!bytes) return false;
    char path[TIG_MAX_PATH], temporary[TIG_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%" PRId64 ".acsector", directory, sector->id);
    snprintf(temporary, sizeof(temporary), "%s\\%" PRId64 ".acsector.tmp", directory, sector->id);
    if (sector->coop_visual_base && sector->coop_visual_base_size == size && !memcmp(bytes, sector->coop_visual_base, size)) {
        free(bytes);
        return !tig_file_exists(path, NULL) || tig_file_remove(path) == 0;
    }
    uint8_t crc[4];
    coop_object_write32(crc, coop_snapshot_crc(bytes, size));
    TigFile* file = tig_file_fopen(temporary, "wb");
    bool good = file && tig_file_fwrite(bytes, 1, size, file) == size && tig_file_fwrite(crc, 1, 4, file) == 4;
    if (file && tig_file_fclose(file)) good = false;
    free(bytes);
    if (good) good = tig_file_rename(temporary, path) == 0;
    if (!good && file) tig_file_remove(temporary);
    return good;
}
void coop_sector_visual_reset(Sector* sector)
{
    free(sector->coop_visual_base);
    sector->coop_visual_base = NULL;
    sector->coop_visual_base_size = 0;
}
bool coop_sector_visual_load(Sector* sector, int64_t id, const char* directory, bool native_light_dif)
{
    if (!coop_campaign_active()) return true;
    coop_sector_visual_reset(sector);
    /* Native light differences already modify the base view. Preserve a
     * supplement conservatively before the native DIF is rewritten without it. */
    if (!native_light_dif) {
        CoopSectorState* base = calloc(1, sizeof(*base));
        if (!base) return false;
        base->x = (uint32_t)SECTOR_X(id);
        base->y = (uint32_t)SECTOR_Y(id);
        base->sound_flags = sector->sounds.flags & ~0x08000000u;
        base->music = (uint32_t)sector->sounds.music_scheme_idx;
        base->ambient = (uint32_t)sector->sounds.ambient_scheme_idx;
        bool captured = capture_visuals(sector, base);
        if (captured) sector->coop_visual_base = coop_sector_visual_encode(base, &sector->coop_visual_base_size);
        free(base);
        if (!sector->coop_visual_base) return false;
    }
    char path[TIG_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%" PRId64 ".acsector", directory, id);
    if (!tig_file_exists(path, NULL)) return true;
    TigFile* file = tig_file_fopen(path, "rb");
    if (!file) return false;
    long length = tig_file_filelength(file);
    size_t maximum = COOP_SECTOR_FIXED_BYTES + COOP_SECTOR_SCRIPT_LIMIT * 16 + COOP_SECTOR_LIGHT_LIMIT * COOP_SECTOR_LIGHT_BYTES;
    uint8_t* bytes = length >= COOP_SECTOR_VISUAL_BYTES + 4 && (size_t)length <= maximum + 4 ? malloc((size_t)length) : NULL;
    bool good = bytes && tig_file_fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (tig_file_fclose(file)) good = false;
    CoopSectorState* state = calloc(1, sizeof(*state));
    good = good && state && coop_object_read32(bytes + length - 4) == coop_snapshot_crc(bytes, (size_t)length - 4)
        && (coop_sector_visual_decode(bytes, (size_t)length - 4, state) || coop_sector_decode(bytes, (size_t)length - 4, state))
        && state->x == SECTOR_X(id) && state->y == SECTOR_Y(id);
    Light** lights = NULL;
    if (good) good = prepare_lights(sector, state, &lights);
    if (good) {
        install_visuals(sector, state, lights);
        /* Native sector sound DIF read/write are no-ops. Keep local-region
         * schemes in the already validated supplemental sector record. */
        sector->sounds.flags = state->sound_flags | 0x08000000u;
        sector->sounds.music_scheme_idx = (int)state->music;
        sector->sounds.ambient_scheme_idx = (int)state->ambient;
    }
    free_lights(lights, state ? state->lights : 0);
    free(bytes);
    free(state);
    return good;
}

static bool probe_restore_visuals(Sector* sector, const CoopSectorState* state)
{
    Light** lights = NULL;
    if (!prepare_lights(sector, state, &lights)) return false;
    install_visuals(sector, state, lights);
    free_lights(lights, state->lights);
    sector->sounds.flags = state->sound_flags | 0x08000000u;
    sector->sounds.music_scheme_idx = (int)state->music;
    sector->sounds.ambient_scheme_idx = (int)state->ambient;
    return true;
}
bool coop_sector_persistence_probe(void)
{
    int64_t pc = player_get_local_pc_obj();
    int64_t id = sector_id_from_loc(obj_field_int64_get(pc, OBJ_F_LOCATION));
    Sector* sector;
    if (!sector_lock(id, &sector)) return false;
    CoopSectorState* original = capture(sector);
    const char* directory = "arcmp-sector-persistence-fixture";
    char path[TIG_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%" PRId64 ".acsector", directory, id);
    bool created = tig_file_mkdir(directory), good = original && created;
    uint8_t* wanted = NULL;
    size_t wanted_size = 0, compact_size = 0;
    if (good) {
        sector->roofs.art_ids[0] = 0xa0000040u;
        sector->roofs.empty = false;
        Light* light = light_static_create(sector_loc_from_id(id), 17, -9, LF_00000020, 0x90000000u, 0x314588);
        good = light != NULL;
        if (light) sector_light_list_add(&sector->lights, light);
        sector->sounds.music_scheme_idx = sector->sounds.music_scheme_idx == 11 ? 12 : 11;
        CoopSectorState* changed = capture(sector);
        wanted = changed ? coop_sector_visual_encode(changed, &wanted_size) : NULL;
        good &= wanted && coop_sector_visual_save(sector, directory);
        TigFile* f = good ? tig_file_fopen(path, "rb") : NULL;
        good &= f != NULL;
        if (f) {
            compact_size = (size_t)tig_file_filelength(f);
            good &= compact_size == wanted_size + 4;
            if (tig_file_fclose(f)) good = false;
        }
        for (unsigned legacy = 0; good && legacy < 2; legacy++) {
            if (legacy) {
                size_t size = 0;
                uint8_t* bytes = coop_sector_encode(changed, &size);
                uint8_t crc[4];
                coop_object_write32(crc, coop_snapshot_crc(bytes, size));
                f = bytes ? tig_file_fopen(path, "wb") : NULL;
                good = f && tig_file_fwrite(bytes, 1, size, f) == size && tig_file_fwrite(crc, 1, 4, f) == 4;
                if (f && tig_file_fclose(f)) good = false;
                free(bytes);
            }
            good &= probe_restore_visuals(sector, original) && coop_sector_visual_load(sector, id, directory, false);
            CoopSectorState* loaded = capture(sector);
            size_t size = 0;
            uint8_t* actual = loaded ? coop_sector_visual_encode(loaded, &size) : NULL;
            good &= actual && wanted_size == size && !memcmp(actual, wanted, size);
            free(actual);
            free(loaded);
        }
        free(changed);
        good &= probe_restore_visuals(sector, original) && coop_sector_visual_save(sector, directory) && !tig_file_exists(path, NULL);
    }
    if (original) good &= probe_restore_visuals(sector, original);
    if (created) {
        if (tig_file_exists(path, NULL)) tig_file_remove(path);
        good &= tig_file_rmdir(directory);
    }
    free(wanted);
    free(original);
    sector_unlock(id);
    fprintf(stderr, "ARCMP compact sector persistence %s bytes=%zu legacy_ASC2=1 reverted_file_removed=1\n", good ? "PASS" : "FAIL", compact_size);
    return good;
}
