#include "coop/engine_transition.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "game/map.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
enum { BYTES = 112 };
static uint8_t pending[BYTES];
static const char path[] = "Save\\Current\\arcmp-transition.bin";
static bool active;
static void put_fade(uint8_t* p, const FadeData* fade)
{
    uint32_t duration;
    memcpy(&duration, &fade->duration, 4);
    coop_object_write32(p, fade->flags); coop_object_write32(p + 4, fade->color);
    coop_object_write32(p + 8, (uint32_t)fade->steps); coop_object_write32(p + 12, duration);
}
static bool get_fade(const uint8_t* p, FadeData* fade)
{
    uint32_t duration = coop_object_read32(p + 12);
    fade->flags = coop_object_read32(p); fade->color = coop_object_read32(p + 4);
    fade->steps = (int32_t)coop_object_read32(p + 8);
    memcpy(&fade->duration, &duration, 4);
    return !(fade->flags & ~1u) && fade->steps >= 0 && fade->steps <= 1024
        && isfinite(fade->duration) && fade->duration >= 0 && fade->duration <= 60;
}
static bool decode(const uint8_t* p, TeleportData* data, ObjectID* actor)
{
    CoopObjectId id;
    memcpy(id.bytes, p + 12, sizeof(id.bytes));
    if (!coop_object_id_valid(&id, false) || id.bytes[0] != 2 || p[29] || p[30] || p[31]) return false;
    *actor = coop_object_id_to_engine(id);
    *data = (TeleportData) { .flags = coop_object_read32(p + 36), .loc = (int64_t)coop_object_read64(p + 40),
        .map = (int32_t)coop_object_read32(p + 48), .movie1 = (int32_t)coop_object_read32(p + 52),
        .movie_flags1 = coop_object_read32(p + 56), .movie2 = (int32_t)coop_object_read32(p + 60),
        .movie_flags2 = coop_object_read32(p + 64), .sound_id = (int32_t)coop_object_read32(p + 68),
        .time = (int32_t)coop_object_read32(p + 72) };
    if ((data->flags & ~0x37fu) || data->map < 0 || (int32_t)coop_object_read32(p + 32) < 0) return false;
    if ((data->flags & TELEPORT_FADE_OUT) && !get_fade(p + 76, &data->fade_out)) return false;
    if ((data->flags & TELEPORT_FADE_IN) && !get_fade(p + 92, &data->fade_in)) return false;
    return true;
}
void coop_engine_transition_reset(void) { active = false; memset(pending, 0, sizeof(pending)); }
bool coop_engine_transition_pending(void) { return active; }
bool coop_engine_transition_get(TeleportData* data, ObjectID* actor)
{
    return active && (int32_t)coop_object_read32(pending + 32) == map_current_map() && decode(pending, data, actor);
}
bool coop_engine_transition_set(const TeleportData* data, ObjectID actor)
{
    if (!data || active) return false;
    CoopObjectId id;
    if (!coop_object_id_from_engine(actor, &id)) return false;
    uint8_t bytes[BYTES] = { 0 };
    memcpy(bytes, "ACTRANS1", 8); coop_object_write32(bytes + 8, 1);
    memcpy(bytes + 12, id.bytes, sizeof(id.bytes));
    coop_object_write32(bytes + 32, (uint32_t)map_current_map());
    coop_object_write32(bytes + 36, data->flags); coop_object_write64(bytes + 40, (uint64_t)data->loc);
    coop_object_write32(bytes + 48, (uint32_t)data->map);
    coop_object_write32(bytes + 52, (uint32_t)data->movie1); coop_object_write32(bytes + 56, data->movie_flags1);
    coop_object_write32(bytes + 60, (uint32_t)data->movie2); coop_object_write32(bytes + 64, data->movie_flags2);
    coop_object_write32(bytes + 68, (uint32_t)data->sound_id); coop_object_write32(bytes + 72, (uint32_t)data->time);
    if (data->flags & TELEPORT_FADE_OUT) put_fade(bytes + 76, &data->fade_out);
    if (data->flags & TELEPORT_FADE_IN) put_fade(bytes + 92, &data->fade_in);
    TeleportData checked; ObjectID checked_actor;
    if (!decode(bytes, &checked, &checked_actor)) return false;
    memcpy(pending, bytes, sizeof(pending)); active = true;
    return true;
}
bool coop_engine_transition_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(path, NULL) || tig_file_remove(path) == 0;
    uint8_t bytes[BYTES] = { 0 };
    if (active) memcpy(bytes, pending, sizeof(bytes));
    else memcpy(bytes, "ACTRANS1", 8);
    coop_object_write32(bytes + BYTES - 4, coop_snapshot_crc(bytes, BYTES - 4));
    TigFile* file = tig_file_fopen(path, "wb");
    bool good = file && tig_file_fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    if (file && tig_file_fclose(file)) good = false;
    return good;
}
bool coop_engine_transition_load(void)
{
    coop_engine_transition_reset();
    if (!coop_campaign_active() || !tig_file_exists(path, NULL)) return true;
    uint8_t bytes[BYTES];
    TigFile* file = tig_file_fopen(path, "rb");
    if (!file) return false;
    bool good = tig_file_fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && tig_file_fgetc(file) == EOF && !tig_file_ferror(file);
    if (tig_file_fclose(file)) good = false;
    if (!good || memcmp(bytes, "ACTRANS1", 8) || coop_object_read32(bytes + 8) > 1
        || coop_object_read32(bytes + BYTES - 4) != coop_snapshot_crc(bytes, BYTES - 4)) return false;
    if (!coop_object_read32(bytes + 8)) return true;
    TeleportData checked; ObjectID actor;
    if (!decode(bytes, &checked, &actor)) return false;
    memcpy(pending, bytes, sizeof(pending)); active = true;
    return true;
}
