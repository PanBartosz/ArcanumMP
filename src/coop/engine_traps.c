#include "coop/engine_traps.h"
#include "coop/authority.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "game/critter.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RECORD_BYTES = 24, MAX_TRAPS = 65536 };
typedef struct OwnedTrap { CoopObjectId id; uint32_t map; uint8_t member; } OwnedTrap;
static OwnedTrap* records;
static size_t count, capacity;
static const char path[] = "Save\\Current\\arcmp-traps.bin";

int coop_engine_trap_member(int64_t actor)
{
    if (!actor || !obj_handle_is_valid(actor)) return -1;
    if (obj_field_int32_get(actor, OBJ_F_TYPE) == OBJ_TYPE_NPC) actor = critter_pc_leader_get(actor);
    return coop_campaign_slot_for_object(actor);
}
static int compare(const OwnedTrap* a, const OwnedTrap* b)
{
    if (a->map != b->map) return a->map < b->map ? -1 : 1;
    return memcmp(a->id.bytes, b->id.bytes, sizeof(a->id.bytes));
}
static bool key(int64_t trap, OwnedTrap* out)
{
    if (!trap || !obj_handle_is_valid(trap) || map_current_map() < 0) return false;
    memset(out, 0, sizeof(*out)); out->map = (uint32_t)map_current_map();
    return coop_object_id_from_engine(obj_get_id(trap), &out->id)
        && (out->id.bytes[0] == 2 || out->id.bytes[0] == 3);
}
static size_t position(const OwnedTrap* entry)
{
    size_t low = 0, high = count;
    while (low < high) { size_t mid = low + (high - low) / 2; if (compare(&records[mid], entry) < 0) low = mid + 1; else high = mid; }
    return low;
}
bool coop_engine_trap_remember(int64_t trap, int64_t actor)
{
    if (!coop_campaign_active()) return true;
    if (!coop_authority_can_simulate()) return false;
    int member = coop_engine_trap_member(actor);
    if (member < 0) return true;
    OwnedTrap entry; if (!key(trap, &entry)) return false;
    entry.member = (uint8_t)member;
    size_t at = position(&entry);
    if (at < count && !compare(&records[at], &entry)) { records[at].member = entry.member; return true; }
    if (count == MAX_TRAPS) return false;
    if (count == capacity) {
        size_t next = capacity ? capacity * 2 : 64;
        if (next > MAX_TRAPS) next = MAX_TRAPS;
        OwnedTrap* grown = realloc(records, next * sizeof(*records));
        if (!grown) return false;
        records = grown; capacity = next;
    }
    memmove(records + at + 1, records + at, (count - at) * sizeof(*records)); records[at] = entry; count++;
    return true;
}
int64_t coop_engine_trap_owner(int64_t trap)
{
    if (!trap || !(obj_field_int32_get(trap, OBJ_F_FLAGS) & OF_TRAP_PC)) return OBJ_HANDLE_NULL;
    if (coop_campaign_active()) {
        OwnedTrap entry;
        if (key(trap, &entry)) {
            size_t at = position(&entry);
            if (at < count && !compare(&records[at], &entry)) return coop_campaign_object(records[at].member);
        }
    }
    /* Older native saves contain only OF_TRAP_PC, without a placer identity.
     * Preserve their original attribution; every new placement records one. */
    return player_get_local_pc_obj();
}
void coop_engine_trap_forget(int64_t trap)
{
    OwnedTrap entry;
    if (!coop_authority_can_simulate() || !count || !key(trap, &entry)) return;
    size_t at = position(&entry);
    if (at < count && !compare(&records[at], &entry)) { count--; memmove(records + at, records + at + 1, (count - at) * sizeof(*records)); }
}
void coop_engine_traps_reset(void) { free(records); records = NULL; count = capacity = 0; }
bool coop_engine_traps_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(path, NULL) || tig_file_remove(path) == 0;
    size_t size = 16 + count * RECORD_BYTES;
    uint8_t* bytes = calloc(1, size); if (!bytes) return false;
    memcpy(bytes, "ACTRAPS1", 8); coop_object_write32(bytes + 8, (uint32_t)count);
    for (size_t i = 0; i < count; i++) {
        uint8_t* p = bytes + 12 + i * RECORD_BYTES;
        memcpy(p, records[i].id.bytes, 17); coop_object_write32(p + 17, records[i].map); p[21] = records[i].member;
    }
    coop_object_write32(bytes + size - 4, coop_snapshot_crc(bytes, size - 4));
    TigFile* file = tig_file_fopen(path, "wb");
    bool good = file && tig_file_fwrite(bytes, 1, size, file) == size;
    if (file && tig_file_fclose(file)) good = false;
    free(bytes); return good;
}
bool coop_engine_traps_load(void)
{
    coop_engine_traps_reset();
    if (!coop_campaign_active() || !tig_file_exists(path, NULL)) return true;
    TigFile* file = tig_file_fopen(path, "rb"); if (!file) return false;
    uint8_t header[12];
    bool good = tig_file_fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header, "ACTRAPS1", 8);
    uint32_t entries = good ? coop_object_read32(header + 8) : 0;
    if (entries > MAX_TRAPS) good = false;
    size_t size = 16 + (size_t)entries * RECORD_BYTES;
    uint8_t* bytes = good ? malloc(size) : NULL;
    OwnedTrap* loaded = good && entries ? calloc(entries, sizeof(*loaded)) : NULL;
    good = good && bytes && (!entries || loaded);
    if (good) {
        memcpy(bytes, header, sizeof(header));
        good = tig_file_fread(bytes + 12, 1, size - 12, file) == size - 12 && tig_file_fgetc(file) == EOF && !tig_file_ferror(file);
    }
    if (tig_file_fclose(file)) good = false;
    if (good) good = coop_object_read32(bytes + size - 4) == coop_snapshot_crc(bytes, size - 4);
    for (uint32_t i = 0; good && i < entries; i++) {
        const uint8_t* p = bytes + 12 + (size_t)i * RECORD_BYTES;
        OwnedTrap* entry = &loaded[i]; memcpy(entry->id.bytes, p, 17); entry->map = coop_object_read32(p + 17); entry->member = p[21];
        good = coop_object_id_valid(&entry->id, false) && (entry->id.bytes[0] == 2 || entry->id.bytes[0] == 3)
            && entry->map <= INT32_MAX && !p[22] && !p[23] && coop_campaign_member(entry->member)
            && (entry->id.bytes[0] != 3 || coop_object_read32(entry->id.bytes + 13) == entry->map)
            && (!i || compare(&loaded[i - 1], entry) < 0);
    }
    free(bytes);
    if (!good) { free(loaded); return false; }
    records = loaded; count = capacity = entries; return true;
}
