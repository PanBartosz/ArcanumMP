#include "coop/story_state.h"
#include "coop/object_state.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER = 36,
    FIXED_BYTES = HEADER + 4 * COOP_STORY_VARIABLES + 4 * COOP_STORY_FLAG_WORDS
        + COOP_STORY_QUESTS + COOP_STORY_RUMOR_BYTES + COOP_STORY_GENERATORS + 4 * COOP_STORY_NEWSPAPERS + COOP_COMBAT_STATE_BYTES };
static bool clock_valid(CoopStoryClock clock)
{
    return clock.milliseconds < 86400000 && (uint64_t)clock.days * 86400 + clock.milliseconds / 1000 <= INT32_MAX;
}
bool coop_story_state_valid(const CoopStoryState* state)
{
    if (!state || state->story < 0 || state->area_count > COOP_STORY_AREAS
        || state->newspaper_count < 5 || state->newspaper_count > COOP_STORY_NEWSPAPERS
        || !clock_valid(state->game_time) || !clock_valid(state->animation_time) || !coop_combat_state_valid(&state->combat)
        || !coop_magic_view_valid(&state->magic) || !coop_environment_valid(&state->environment)) return false;
    for (unsigned i = 0; i < COOP_STORY_QUESTS; i++)
        if (state->quests[i] > 6) return false;
    for (unsigned i = 0; i < COOP_STORY_GENERATORS; i++)
        if (state->generators[i] & ~0x9f) return false;
    for (unsigned i = 0; i < state->area_count; i++)
        if (state->areas[i] > 1) return false;
    for (unsigned i = 0; i < COOP_STORY_NEWSPAPERS; i++)
        if (state->newspapers[i] < -1) return false;
    return true;
}
uint8_t* coop_story_state_encode(const CoopStoryState* state, size_t* size)
{
    if (!size || !coop_story_state_valid(state)) return NULL;
    size_t magic_size = coop_magic_view_size(&state->magic);
    size_t length = FIXED_BYTES + state->area_count + magic_size + COOP_ENVIRONMENT_BYTES;
    uint8_t* bytes = calloc(1, length);
    if (!bytes) return NULL;
    memcpy(bytes, "ACS4", 4);
    coop_object_write32(bytes + 4, (uint32_t)magic_size);
    coop_object_write32(bytes + 8, state->area_count);
    coop_object_write32(bytes + 12, state->newspaper_count);
    coop_object_write32(bytes + 16, (uint32_t)state->story);
    coop_object_write32(bytes + 20, state->game_time.days);
    coop_object_write32(bytes + 24, state->game_time.milliseconds);
    coop_object_write32(bytes + 28, state->animation_time.days);
    coop_object_write32(bytes + 32, state->animation_time.milliseconds);
    size_t at = HEADER;
    for (unsigned i = 0; i < COOP_STORY_VARIABLES; i++, at += 4)
        coop_object_write32(bytes + at, (uint32_t)state->variables[i]);
    for (unsigned i = 0; i < COOP_STORY_FLAG_WORDS; i++, at += 4)
        coop_object_write32(bytes + at, state->flags[i]);
    memcpy(bytes + at, state->quests, COOP_STORY_QUESTS);
    at += COOP_STORY_QUESTS;
    memcpy(bytes + at, state->rumors, COOP_STORY_RUMOR_BYTES);
    at += COOP_STORY_RUMOR_BYTES;
    memcpy(bytes + at, state->generators, COOP_STORY_GENERATORS);
    at += COOP_STORY_GENERATORS;
    for (unsigned i = 0; i < COOP_STORY_NEWSPAPERS; i++, at += 4)
        coop_object_write32(bytes + at, (uint32_t)state->newspapers[i]);
    memcpy(bytes + at, state->areas, state->area_count);
    if (!coop_combat_state_encode(&state->combat, bytes + at + state->area_count)) { free(bytes); return NULL; }
    if (!coop_magic_view_encode(&state->magic, bytes + at + state->area_count + COOP_COMBAT_STATE_BYTES, magic_size)) { free(bytes); return NULL; }
    if (!coop_environment_encode(&state->environment, bytes + length - COOP_ENVIRONMENT_BYTES)) { free(bytes); return NULL; }
    *size = length;
    return bytes;
}
bool coop_story_state_decode(const void* data, size_t size, CoopStoryState* out)
{
    const uint8_t* bytes = data;
    if (!bytes || !out || size < FIXED_BYTES + COOP_MAGIC_VIEW_HEADER + COOP_ENVIRONMENT_BYTES || size > COOP_STORY_LIMIT
        || memcmp(bytes, "ACS4", 4)) return false;
    CoopStoryState state = { 0 };
    state.area_count = coop_object_read32(bytes + 8);
    size_t magic_size = coop_object_read32(bytes + 4);
    if (state.area_count > COOP_STORY_AREAS || magic_size > size || size != FIXED_BYTES + state.area_count + magic_size + COOP_ENVIRONMENT_BYTES) return false;
    state.newspaper_count = coop_object_read32(bytes + 12);
    state.story = (int32_t)coop_object_read32(bytes + 16);
    state.game_time = (CoopStoryClock) { coop_object_read32(bytes + 20), coop_object_read32(bytes + 24) };
    state.animation_time = (CoopStoryClock) { coop_object_read32(bytes + 28), coop_object_read32(bytes + 32) };
    size_t at = HEADER;
    for (unsigned i = 0; i < COOP_STORY_VARIABLES; i++, at += 4)
        state.variables[i] = (int32_t)coop_object_read32(bytes + at);
    for (unsigned i = 0; i < COOP_STORY_FLAG_WORDS; i++, at += 4)
        state.flags[i] = coop_object_read32(bytes + at);
    memcpy(state.quests, bytes + at, COOP_STORY_QUESTS);
    at += COOP_STORY_QUESTS;
    memcpy(state.rumors, bytes + at, COOP_STORY_RUMOR_BYTES);
    at += COOP_STORY_RUMOR_BYTES;
    memcpy(state.generators, bytes + at, COOP_STORY_GENERATORS);
    at += COOP_STORY_GENERATORS;
    for (unsigned i = 0; i < COOP_STORY_NEWSPAPERS; i++, at += 4)
        state.newspapers[i] = (int32_t)coop_object_read32(bytes + at);
    memcpy(state.areas, bytes + at, state.area_count);
    if (!coop_combat_state_decode(bytes + at + state.area_count, COOP_COMBAT_STATE_BYTES, &state.combat)) return false;
    if (!coop_magic_view_decode(bytes + at + state.area_count + COOP_COMBAT_STATE_BYTES, magic_size, &state.magic)) return false;
    if (!coop_environment_decode(bytes + size - COOP_ENVIRONMENT_BYTES, COOP_ENVIRONMENT_BYTES, &state.environment)) return false;
    if (!coop_story_state_valid(&state)) return false;
    *out = state;
    return true;
}
