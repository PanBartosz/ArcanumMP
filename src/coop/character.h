#ifndef ARCMP_CHARACTER_H
#define ARCMP_CHARACTER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A creation recipe contains choices, never serialized engine objects, items,
 * effects, XP, money or training. The host must replay its native point rules. */
#define COOP_CHARACTER_LEGACY_BYTES 112
#define COOP_CHARACTER_BYTES 176
#define COOP_STARTING_TRADES 16
typedef struct CoopStartingTrade {
    uint16_t prototype;
    int16_t quantity;
} CoopStartingTrade;
typedef struct CoopCharacter {
    uint8_t race, gender;
    uint16_t background;
    uint32_t portrait;
    uint16_t hp, fatigue, unspent;
    uint16_t stats[8];
    uint8_t skills[16], colleges[16], disciplines[8];
    char name[32];
    uint8_t trades;
    CoopStartingTrade purchases[COOP_STARTING_TRADES];
} CoopCharacter;
bool coop_character_valid(const CoopCharacter* character);
bool coop_character_encode(const CoopCharacter* character, uint8_t bytes[COOP_CHARACTER_BYTES]);
bool coop_character_decode(CoopCharacter* character, const uint8_t* bytes, size_t size);
#endif
