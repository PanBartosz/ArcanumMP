#include "coop/character.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(void)
{
    CoopCharacter c = { .race = 2, .gender = 1, .background = 71, .portrait = 0x01020304, .hp = 3, .fatigue = 2, .unspent = 1, .name = "Custom traveller" };
    for (unsigned i = 0; i < 8; i++) c.stats[i] = (uint16_t)(8 + i);
    c.skills[15] = 2; c.colleges[15] = 1; c.disciplines[7] = 1;
    uint8_t bytes[COOP_CHARACTER_BYTES], next[COOP_CHARACTER_BYTES];
    CHECK(coop_character_encode(&c, bytes));
    CoopCharacter bad = c; strcpy(bad.name, "   "); CHECK(!coop_character_valid(&bad));
    CHECK(!memcmp(bytes, "ACH2\2\1\0\107\1\2\3\4", 12));
    CHECK(bytes[12] == 0 && bytes[13] == 3 && bytes[33] == 15 && bytes[49] == 2 && bytes[65] == 1 && bytes[73] == 1);
    CoopCharacter decoded;
    CHECK(coop_character_decode(&decoded, bytes, sizeof(bytes)));
    CHECK(coop_character_encode(&decoded, next) && !memcmp(bytes, next, sizeof(bytes)));
    for (size_t n = 0; n < sizeof(bytes); n++) CHECK(!coop_character_decode(&decoded, bytes, n));
    CHECK(!coop_character_decode(&decoded, bytes, sizeof(bytes) + 1));
    for (unsigned i = 106; i < sizeof(bytes); i++) { bytes[i] = 1; CHECK(!coop_character_decode(&decoded, bytes, sizeof(bytes))); bytes[i] = 0; }
    bytes[105] = 1; CHECK(!coop_character_decode(&decoded, bytes, sizeof(bytes))); bytes[105] = 0;
    uint8_t legacy[COOP_CHARACTER_LEGACY_BYTES];
    memcpy(legacy, bytes, sizeof(legacy));
    memcpy(legacy, "ACH1", 4);
    CHECK(coop_character_decode(&decoded, legacy, sizeof(legacy)) && decoded.trades == 0);
    c.trades = 2;
    c.purchases[0] = (CoopStartingTrade) { 6000, 1 };
    c.purchases[1] = (CoopStartingTrade) { 7000, -10 };
    CHECK(coop_character_encode(&c, bytes) && bytes[106] == 2 && bytes[112] == 23 && bytes[113] == 112);
    CHECK(coop_character_decode(&decoded, bytes, sizeof(bytes)) && decoded.purchases[1].quantity == -10);
    bad = c;
    bad.purchases[1].quantity = INT16_MIN;
    CHECK(!coop_character_valid(&bad));
    uint32_t rng = 716239;
    for (unsigned i = 0; i < 10000; i++) {
        memcpy(next, bytes, sizeof(next));
        rng = rng * 1664525u + 1013904223u; unsigned offset = rng % sizeof(next);
        rng = rng * 1664525u + 1013904223u; next[offset] ^= (uint8_t)(1u << (rng % 8));
        CoopCharacter before = decoded;
        if (coop_character_decode(&decoded, next, sizeof(next))) {
            uint8_t canonical[COOP_CHARACTER_BYTES]; CHECK(coop_character_encode(&decoded, canonical));
            CHECK(!memcmp(next, canonical, sizeof(next)));
        } else CHECK(!memcmp(&before, &decoded, sizeof(before)));
    }
    puts("character recipe: canonical bytes, truncation, bounds and 10000 mutations passed");
    return 0;
}
