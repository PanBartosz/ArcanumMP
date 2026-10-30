#include "coop/character.h"
#include <string.h>
static void u16(uint8_t* p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static unsigned r16(const uint8_t* p) { return (unsigned)p[0] << 8 | p[1]; }
bool coop_character_valid(const CoopCharacter* c)
{
    if (!c || c->race >= 8 || c->gender >= 2 || c->background >= 256
        || c->portrait > INT32_MAX || c->hp > 200 || c->fatigue > 200 || c->unspent > 56) return false;
    if (c->trades > COOP_STARTING_TRADES) return false;
    for (unsigned i = 0; i < COOP_STARTING_TRADES; i++) {
        if (i < c->trades) {
            if (!c->purchases[i].prototype || !c->purchases[i].quantity || c->purchases[i].quantity == INT16_MIN) return false;
        } else if (c->purchases[i].prototype || c->purchases[i].quantity)
            return false;
    }
    if (!c->name[0] || !memchr(c->name, 0, sizeof(c->name))) return false;
    bool non_space = false;
    for (const unsigned char* p = (const unsigned char*)c->name; *p; p++) {
        if (*p < 32 || *p > 126) return false;
        if (*p != ' ') non_space = true;
    }
    if (!non_space) return false;
    for (unsigned i = 0; i < 8; i++) if (!c->stats[i] || c->stats[i] > 100 || c->disciplines[i] >= 8) return false;
    for (unsigned i = 0; i < 16; i++) if (c->skills[i] > 5 || c->colleges[i] > 5) return false;
    return true;
}
bool coop_character_encode(const CoopCharacter* c, uint8_t p[COOP_CHARACTER_BYTES])
{
    if (!p || !coop_character_valid(c)) return false;
    memset(p, 0, COOP_CHARACTER_BYTES);
    memcpy(p, "ACH2", 4);
    p[4] = c->race; p[5] = c->gender; u16(p + 6, c->background);
    p[8] = (uint8_t)(c->portrait >> 24); p[9] = (uint8_t)(c->portrait >> 16);
    p[10] = (uint8_t)(c->portrait >> 8); p[11] = (uint8_t)c->portrait;
    u16(p + 12, c->hp); u16(p + 14, c->fatigue); u16(p + 16, c->unspent);
    for (unsigned i = 0; i < 8; i++) u16(p + 18 + i * 2, c->stats[i]);
    memcpy(p + 34, c->skills, 16); memcpy(p + 50, c->colleges, 16); memcpy(p + 66, c->disciplines, 8);
    memcpy(p + 74, c->name, strlen(c->name));
    p[106] = c->trades;
    for (unsigned i = 0; i < c->trades; i++) {
        u16(p + 112 + i * 4, c->purchases[i].prototype);
        u16(p + 114 + i * 4, (uint16_t)c->purchases[i].quantity);
    }
    return true;
}
bool coop_character_decode(CoopCharacter* result, const uint8_t* p, size_t size)
{
    if (!result || !p) return false;
    bool legacy = size == COOP_CHARACTER_LEGACY_BYTES && !memcmp(p, "ACH1", 4);
    if (!legacy && (size != COOP_CHARACTER_BYTES || memcmp(p, "ACH2", 4))) return false;
    for (unsigned i = legacy ? 106 : 107; i < 112; i++)
        if (p[i]) return false;
    CoopCharacter c = { 0 };
    c.race = p[4]; c.gender = p[5]; c.background = (uint16_t)r16(p + 6);
    c.portrait = (uint32_t)p[8] << 24 | (uint32_t)p[9] << 16 | (uint32_t)p[10] << 8 | p[11];
    c.hp = (uint16_t)r16(p + 12); c.fatigue = (uint16_t)r16(p + 14); c.unspent = (uint16_t)r16(p + 16);
    for (unsigned i = 0; i < 8; i++) c.stats[i] = (uint16_t)r16(p + 18 + i * 2);
    memcpy(c.skills, p + 34, 16); memcpy(c.colleges, p + 50, 16); memcpy(c.disciplines, p + 66, 8);
    memcpy(c.name, p + 74, 32);
    if (!legacy) {
        c.trades = p[106];
        for (unsigned i = 0; i < COOP_STARTING_TRADES; i++) {
            c.purchases[i].prototype = (uint16_t)r16(p + 112 + i * 4);
            c.purchases[i].quantity = (int16_t)r16(p + 114 + i * 4);
        }
    }
    if (!coop_character_valid(&c)) return false;
    /* Fixed strings have one canonical representation. */
    for (size_t i = strlen(c.name) + 1; i < sizeof(c.name); i++) if (c.name[i]) return false;
    *result = c;
    return true;
}
