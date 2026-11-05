#ifndef ARCMP_SPELL_SOUNDS_H
#define ARCMP_SPELL_SOUNDS_H
#include <stdbool.h>
#include <stddef.h>

/* Match resources, not numeric sound IDs: compatible sound indexes can map
 * the same sample to different IDs. Both path separators and case are local. */
static inline bool coop_spell_sound_repeats(const char* path, int loops)
{
    if (!path) return false;
    const char* name = path;
    for (const char* p = path; *p; p++) if (*p == '/' || *p == '\\') name = p + 1;
    char lower[128]; size_t size = 0;
    for (; name[size] && size + 1 < sizeof(lower); size++)
        lower[size] = name[size] >= 'A' && name[size] <= 'Z' ? name[size] + 'a' - 'A' : name[size];
    if (name[size]) return false;
    lower[size] = 0;
    const char* known[] = {
        "spell_elemental_air_vitality.wav", "spell_elemental_earth_strength.wav",
        "spell_elemental_fire_agility.wav", "spell_elemental_water_purity.wav",
        "spell_temporal_tempus_fugit.wav", "krag_fidgeting.wav"
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(*known); i++) {
        size_t j = 0; while (lower[j] && lower[j] == known[i][j]) j++;
        if (!lower[j] && !known[i][j]) return true;
    }
    /* Some maintained effects use an actual infinite sound instance. The six
     * classic resources also recur as one-shots/fidgets, so match those above. */
    return loops == 0 && size > 10 && lower[0] == 's' && lower[1] == 'p'
        && lower[2] == 'e' && lower[3] == 'l' && lower[4] == 'l' && lower[5] == '_'
        && lower[size - 4] == '.' && lower[size - 3] == 'w' && lower[size - 2] == 'a' && lower[size - 1] == 'v';
}
#endif
