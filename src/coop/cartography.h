#ifndef ARCMP_CARTOGRAPHY_H
#define ARCMP_CARTOGRAPHY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { COOP_CHART_MAPS = 256,
    COOP_CHART_TILES = 32768,
    COOP_CHART_LIMIT = 1050632 };
typedef struct CoopChart {
    uint32_t id, tiles;
    uint8_t* bits;
} CoopChart;
typedef struct CoopCartography {
    unsigned count;
    CoopChart* charts;
} CoopCartography;
void coop_cartography_clear(CoopCartography* maps);
const CoopChart* coop_cartography_find(const CoopCartography* maps, uint32_t id);
/* Zero maps are omitted; unknown maps are zero on replacement. */
bool coop_cartography_put(CoopCartography* maps, uint32_t id, uint32_t tiles, const uint8_t* bits);
uint8_t* coop_cartography_encode(const CoopCartography* maps, size_t* size);
/* Decode commits only after the complete canonical view passes validation. */
bool coop_cartography_decode(CoopCartography* maps, const void* bytes, size_t size);
#endif
