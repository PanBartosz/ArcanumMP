#include "coop/environment_state.h"
#include "coop/object_state.h"
#include <limits.h>
#include <string.h>
static bool pair_valid(const uint32_t pair[2]) { return pair[0] <= pair[1] && pair[1] <= INT32_MAX; }
bool coop_environment_valid(const CoopEnvironmentState* s)
{
    if (!s || s->difficulty > 2 || (s->sound_members & ~15u) || (!s->scripted_sound && s->sound_members)) return false;
    for (unsigned i = 0; i < 4; i++)
        if (s->owner_options[i] && !(s->owner_options[i] & 0x80)) return false;
    for (unsigned i = 0; i < 4; i++)
        if (s->sound_origins[i][0] >= 0x4000000 || s->sound_origins[i][1] >= 0x4000000
            || (!(s->sound_members & (1u << i)) && (s->sound_origins[i][0] || s->sound_origins[i][1]))) return false;
    return s->light_scheme <= INT32_MAX && s->light_default <= INT32_MAX && s->hour < 24
        && s->indoor <= 0xffffff && s->outdoor <= 0xffffff && pair_valid(s->schemes)
        && pair_valid(s->after_combat) && pair_valid(s->after_song) && s->combat_track <= 6
        && (s->combat || (!s->combat_track && !s->after_combat[0] && !s->after_combat[1]))
        && (s->pending_song || (!s->after_song[0] && !s->after_song[1]));
}
bool coop_environment_encode(const CoopEnvironmentState* s, uint8_t bytes[COOP_ENVIRONMENT_BYTES])
{
    if (!bytes || !coop_environment_valid(s)) return false;
    memcpy(bytes, "ACE3", 4);
    const uint32_t fields[] = {s->light_scheme, s->light_default, s->hour, s->indoor, s->outdoor,
        s->schemes[0], s->schemes[1], s->after_combat[0], s->after_combat[1], s->after_song[0], s->after_song[1]};
    for (unsigned i = 0; i < 11; i++) coop_object_write32(bytes + 4 + i*4, fields[i]);
    bytes[48] = (s->combat ? 1 : 0) | (s->pending_song ? 2 : 0) | (s->scripted_sound ? 4 : 0);
    bytes[49] = s->combat_track; bytes[50] = s->difficulty; bytes[51] = s->fast_turn_based;
    coop_object_write32(bytes + 52, s->sound_revision);
    bytes[56] = s->sound_members;
    bytes[57] = bytes[58] = bytes[59] = 0;
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 2; j++)
            coop_object_write32(bytes + 60 + i * 8 + j * 4, s->sound_origins[i][j]);
    memcpy(bytes + 92, s->owner_options, 4);
    return true;
}
bool coop_environment_decode(const void* bytes, size_t size, CoopEnvironmentState* out)
{
    if (!bytes || !out || (size != COOP_ENVIRONMENT_BYTES && size != COOP_ENVIRONMENT_V2_BYTES && size != COOP_ENVIRONMENT_LEGACY_BYTES)) return false;
    const uint8_t* p = bytes;
    bool legacy = size == COOP_ENVIRONMENT_LEGACY_BYTES;
    bool current = size == COOP_ENVIRONMENT_BYTES;
    if (memcmp(p, current ? "ACE3" : legacy ? "ACE1" : "ACE2", 4) || (p[48] & ~(legacy ? 3 : 7)) || (!current && p[50]) || p[51] > (current ? 1 : 0)) return false;
    if (!legacy && (p[57] || p[58] || p[59])) return false;
    CoopEnvironmentState s = { .light_scheme = coop_object_read32(p + 4), .light_default = coop_object_read32(p + 8), .hour = coop_object_read32(p + 12), .indoor = coop_object_read32(p + 16), .outdoor = coop_object_read32(p + 20), .schemes = { coop_object_read32(p + 24), coop_object_read32(p + 28) }, .after_combat = { coop_object_read32(p + 32), coop_object_read32(p + 36) }, .after_song = { coop_object_read32(p + 40), coop_object_read32(p + 44) }, .combat = (p[48] & 1) != 0, .pending_song = (p[48] & 2) != 0, .combat_track = p[49], .scripted_sound = !legacy && (p[48] & 4) != 0, .sound_revision = legacy ? 0 : coop_object_read32(p + 52) };
    if (!legacy) {
        s.sound_members = p[56];
        for (unsigned i = 0; i < 4; i++)
            for (unsigned j = 0; j < 2; j++)
                s.sound_origins[i][j] = coop_object_read32(p + 60 + i * 8 + j * 4);
    }
    s.difficulty = current ? p[50] : 1;
    s.fast_turn_based = current && p[51];
    if (current) memcpy(s.owner_options, p + 92, 4);
    if (!coop_environment_valid(&s)) return false;
    *out = s; return true;
}
