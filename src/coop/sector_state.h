#ifndef ARCMP_SECTOR_STATE_H
#define ARCMP_SECTOR_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { COOP_SECTOR_TILES = 4096,
    COOP_SECTOR_BLOCKS = 128,
    COOP_SECTOR_ROOFS = 256,
    COOP_SECTOR_FIXED_BYTES = 17980,
    COOP_SECTOR_VISUAL_BYTES = 1052,
    COOP_SECTOR_SCRIPT_LIMIT = 4096,
    COOP_SECTOR_LIGHT_LIMIT = 1024,
    COOP_SECTOR_LIGHT_BYTES = 24 };
typedef struct CoopSectorScript {
    uint32_t tile, flags, counters, number;
} CoopSectorScript;
typedef struct CoopSectorLight {
    uint32_t tile, offset_x, offset_y, flags, art, color;
} CoopSectorLight;
typedef struct CoopSectorState {
    uint32_t x, y, townmap, aptitude, light, sound_flags, music, ambient;
    uint32_t script_flags, script_counters, script_number;
    bool blocked;
    uint32_t tiles[COOP_SECTOR_TILES], blocks[COOP_SECTOR_BLOCKS];
    uint32_t roofs[COOP_SECTOR_ROOFS];
    unsigned scripts, lights;
    CoopSectorScript tile_scripts[COOP_SECTOR_SCRIPT_LIMIT];
    CoopSectorLight static_lights[COOP_SECTOR_LIGHT_LIMIT];
} CoopSectorState;
bool coop_sector_valid(const CoopSectorState* state);
uint8_t* coop_sector_encode(const CoopSectorState* state, size_t* size);
bool coop_sector_decode(const void* data, size_t size, CoopSectorState* state);
/* Compact native save supplement: roof, static lights and regional sound only. */
uint8_t* coop_sector_visual_encode(const CoopSectorState* state, size_t* size);
bool coop_sector_visual_decode(const void* bytes, size_t size, CoopSectorState* out);
#endif
