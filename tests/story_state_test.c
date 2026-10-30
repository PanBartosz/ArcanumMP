#include "coop/object_state.h"
#include "coop/story_state.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                \
        }                                                           \
    } while (0)
int main(void)
{
    CoopStoryState state = { 0 }, decoded;
    state.area_count = 3;
    state.newspaper_count = 5;
    state.newspapers[0] = -1;
    state.newspapers[4] = 5000;
    state.variables[0] = INT32_MIN;
    state.variables[1999] = INT32_MAX;
    state.flags[99] = UINT32_C(0x80000001);
    state.story = 7;
    state.quests[999] = 6;
    state.generators[255] = 0x9f;
    state.rumors[250] = 0xa5;
    state.areas[2] = 1;
    state.game_time = (CoopStoryClock) { 31, 86399999 };
    state.animation_time = (CoopStoryClock) { 32, 1 };
    size_t size;
    uint8_t* bytes = coop_story_state_encode(&state, &size);
    CHECK(bytes && coop_story_state_decode(bytes, size, &decoded));
    CHECK(!memcmp(bytes, "ACS4", 4) && coop_object_read32(bytes + 4) == COOP_MAGIC_VIEW_HEADER
        && coop_object_read32(bytes + 8) == 3 && coop_object_read32(bytes + 12) == 5);
    CHECK(!memcmp(bytes + 36, "\x80\0\0\0", 4));
    CHECK(decoded.variables[0] == INT32_MIN && decoded.variables[1999] == INT32_MAX);
    CHECK(decoded.flags[99] == UINT32_C(0x80000001) && decoded.quests[999] == 6 && decoded.story == 7);
    CHECK(decoded.newspapers[0] == -1 && decoded.newspapers[4] == 5000 && decoded.newspaper_count == 5);
    CHECK(decoded.game_time.days == 31 && decoded.game_time.milliseconds == 86399999 && decoded.areas[2] == 1);
    memset(&decoded, 0x42, sizeof(decoded));
    CoopStoryState retained = decoded;
    CHECK(!coop_story_state_decode(bytes, size - 1, &decoded) && !memcmp(&retained, &decoded, sizeof(decoded)));
    state.quests[0] = 7;
    CHECK(!coop_story_state_encode(&state, &size));
    state.quests[0] = 0;
    state.generators[0] = 0x20;
    CHECK(!coop_story_state_encode(&state, &size));
    state.generators[0] = 0;
    state.areas[0] = 2;
    CHECK(!coop_story_state_encode(&state, &size));
    state.areas[0] = 0;
    state.game_time.milliseconds = 86400000;
    CHECK(!coop_story_state_encode(&state, &size));
    state.game_time.milliseconds = 86399999;
    state.game_time.days = UINT32_MAX;
    CHECK(!coop_story_state_encode(&state, &size));
    state.game_time.days = 31;
    state.newspaper_count = 26;
    CHECK(!coop_story_state_encode(&state, &size));
    state.newspaper_count = 5;
    state.story = -1;
    CHECK(!coop_story_state_encode(&state, &size));
    state.story = 7;
    uint32_t random = 20261020;
    for (unsigned i = 0; i < 5000; i++) {
        random = random * 1664525u + 1013904223u;
        size_t at = random % size;
        uint8_t saved = bytes[at];
        bytes[at] ^= (uint8_t)(random >> 24);
        if (coop_story_state_decode(bytes, size, &decoded)) {
            size_t encoded_size;
            uint8_t* encoded = coop_story_state_encode(&decoded, &encoded_size);
            CHECK(encoded && size == encoded_size && !memcmp(bytes, encoded, size));
            free(encoded);
        }
        bytes[at] = saved;
    }
    free(bytes);
    puts("PASS: portable campaign state, signed/flag boundary bytes, validated clocks/enums/counts and malformed corpus");
    return 0;
}
