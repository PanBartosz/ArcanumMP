#include "coop/object_state.h"
#include "coop/sector_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                       \
    do {                                               \
        if (!(x)) {                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #x); \
            exit(1);                                   \
        }                                              \
    } while (0)
int main(void)
{
    CoopSectorState *s = calloc(1, sizeof(*s)), *copy = calloc(1, sizeof(*copy));
    CHECK(s && copy);
    s->x = 1200;
    s->y = 30;
    s->aptitude = (uint32_t)-9;
    s->blocked = true;
    s->scripts = 3;
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++)
        s->roofs[i] = UINT32_MAX;
    s->roofs[17] = 0xa0000040;
    s->lights = 2;
    s->static_lights[0] = (CoopSectorLight) { 22, (uint32_t)-45, 29, 1, 0x90000000, 0x123456 };
    s->static_lights[1] = (CoopSectorLight) { 4095, 0, 0, 8, 0x90000001, 0 };
    for (unsigned i = 0; i < 4096; i++)
        s->tiles[i] = 0x10000000u + i;
    for (unsigned i = 0; i < 128; i++)
        s->blocks[i] = 1u << (i % 32);
    for (unsigned i = 0; i < 3; i++)
        s->tile_scripts[i] = (CoopSectorScript) { i * 1024, i + 1, 0xfefdfcfbu, 999 };
    size_t n;
    uint8_t* bytes = coop_sector_encode(s, &n);
    CHECK(bytes && n == COOP_SECTOR_FIXED_BYTES + 48 + 2 * COOP_SECTOR_LIGHT_BYTES);
    CHECK(bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 30);
    CHECK(coop_sector_decode(bytes, n, copy) && !memcmp(s, copy, sizeof(*s)));
    copy->x = 99999;
    CHECK(!coop_sector_decode(bytes, n - 1, copy) && copy->x == 99999);
    coop_object_write32(bytes + 52, 2);
    CHECK(!coop_sector_decode(bytes, n, copy));
    coop_object_write32(bytes + 52, 1);
    coop_object_write32(bytes + COOP_SECTOR_FIXED_BYTES + 16, 0);
    CHECK(!coop_sector_decode(bytes, n, copy));
    free(bytes);
    unsigned original = s->static_lights[0].tile;
    s->static_lights[0].tile = 4096;
    CHECK(!coop_sector_valid(s));
    s->static_lights[0].tile = original;
    s->roofs[17] |= 0x1000;
    CHECK(!coop_sector_valid(s));
    s->roofs[17] &= ~0x1000u;
    uint32_t roof = s->roofs[17];
    s->roofs[17] = 0xa0000000u | (31u << 14);
    CHECK(!coop_sector_valid(s));
    s->roofs[17] = roof;
    s->static_lights[1].color = 1;
    CHECK(!coop_sector_valid(s));
    s->static_lights[1].color = 0;
    s->lights = COOP_SECTOR_LIGHT_LIMIT;
    for (unsigned i = 2; i < s->lights; i++)
        s->static_lights[i] = s->static_lights[0];
    s->scripts = 4096;
    for (unsigned i = 0; i < 4096; i++)
        s->tile_scripts[i] = (CoopSectorScript) { i, 0, i, 0 };
    bytes = coop_sector_encode(s, &n);
    CHECK(bytes && coop_sector_decode(bytes, n, copy));
    unsigned seed = 79, valid = 0;
    for (unsigned i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        size_t at = seed % n;
        uint8_t saved = bytes[at];
        bytes[at] ^= (uint8_t)(1u << (seed % 8));
        if (coop_sector_decode(bytes, n, copy)) {
            size_t m;
            uint8_t* canonical = coop_sector_encode(copy, &m);
            CHECK(canonical && m == n && !memcmp(canonical, bytes, n));
            free(canonical);
            valid++;
        }
        bytes[at] = saved;
    }
    CHECK(valid > 1000);
    free(bytes);
    bytes = coop_sector_visual_encode(s, &n);
    CHECK(bytes && n == COOP_SECTOR_VISUAL_BYTES + s->lights * 24);
    CHECK(coop_sector_visual_decode(bytes, n, copy) && copy->x == s->x && copy->y == s->y && copy->lights == s->lights
        && !memcmp(copy->roofs, s->roofs, sizeof(s->roofs)) && !memcmp(copy->static_lights, s->static_lights, s->lights * sizeof(*s->static_lights)));
    CHECK(!copy->scripts && !copy->tiles[1]);
    for (size_t length = 0; length < n; length += 71)
        CHECK(!coop_sector_visual_decode(bytes, length, copy));
    for (unsigned i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        size_t at = seed % n;
        uint8_t saved = bytes[at];
        bytes[at] ^= (uint8_t)(1u << (seed % 8));
        if (coop_sector_visual_decode(bytes, n, copy)) {
            size_t m;
            uint8_t* canonical = coop_sector_visual_encode(copy, &m);
            CHECK(canonical && m == n && !memcmp(canonical, bytes, n));
            free(canonical);
        }
        bytes[at] = saved;
    }
    free(bytes);
    free(s);
    free(copy);
    puts("PASS: sector roundtrip, bounds, canonicality, tombstones and 20,000 mutations");
    return 0;
}
