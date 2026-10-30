#ifndef ARCMP_STORY_STATE_H
#define ARCMP_STORY_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "coop/combat_state.h"
#include "coop/magic_state.h"
#include "coop/environment_state.h"

#define COOP_STORY_VARIABLES 2000
#define COOP_STORY_FLAG_WORDS 100
#define COOP_STORY_QUESTS 1000
#define COOP_STORY_RUMOR_BYTES 251
#define COOP_STORY_GENERATORS 256
#define COOP_STORY_NEWSPAPERS 25
#define COOP_STORY_AREAS 4096
#define COOP_STORY_LIMIT 65536
typedef struct CoopStoryClock {
    uint32_t days;
    uint32_t milliseconds;
} CoopStoryClock;
typedef struct CoopStoryState {
    int32_t variables[COOP_STORY_VARIABLES];
    uint32_t flags[COOP_STORY_FLAG_WORDS];
    int32_t story;
    uint8_t quests[COOP_STORY_QUESTS];
    uint8_t rumors[COOP_STORY_RUMOR_BYTES];
    uint8_t generators[COOP_STORY_GENERATORS];
    uint32_t newspaper_count;
    int32_t newspapers[COOP_STORY_NEWSPAPERS];
    uint32_t area_count;
    uint8_t areas[COOP_STORY_AREAS];
    CoopStoryClock game_time;
    CoopStoryClock animation_time;
    CoopCombatState combat;
    CoopMagicView magic;
    CoopEnvironmentState environment;
} CoopStoryState;
/* Explicit values only. Decode commits to out only after complete validation. */
bool coop_story_state_valid(const CoopStoryState* state);
uint8_t* coop_story_state_encode(const CoopStoryState* state, size_t* size);
bool coop_story_state_decode(const void* bytes, size_t size, CoopStoryState* out);
#endif
