#include "coop/content_identity.h"
#include <string.h>

static uint8_t current_id[32], legacy_ids[COOP_CONTENT_LEGACY_MAX][32];
static size_t legacy_count;

void coop_content_legacy_set(const uint8_t current[32], const uint8_t* legacy, size_t count)
{
    legacy_count = 0;
    if (!current || !legacy) return;
    memcpy(current_id, current, 32);
    if (count > COOP_CONTENT_LEGACY_MAX) count = COOP_CONTENT_LEGACY_MAX;
    memcpy(legacy_ids, legacy, count * 32);
    legacy_count = count;
}
const uint8_t* coop_content_legacy_at(const uint8_t current[32], size_t index)
{
    return current && index < legacy_count && !memcmp(current, current_id, 32) ? legacy_ids[index] : NULL;
}
bool coop_content_matches(const uint8_t current[32], const uint8_t saved[32])
{
    if (!current || !saved) return false;
    if (!memcmp(current, saved, 32)) return true;
    for (size_t i = 0; i < legacy_count; i++) {
        const uint8_t* previous = coop_content_legacy_at(current, i);
        if (previous && !memcmp(previous, saved, 32)) return true;
    }
    return false;
}
