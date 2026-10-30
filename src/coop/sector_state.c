#include "coop/sector_state.h"
#include "coop/object_state.h"
#include <stdlib.h>
#include <string.h>
bool coop_sector_valid(const CoopSectorState* s)
{
    if (!s || s->x >= 0x4000000 || s->y >= 0x4000000 || s->scripts > COOP_SECTOR_SCRIPT_LIMIT || s->lights > COOP_SECTOR_LIGHT_LIMIT) return false;
    for (unsigned i = 0; i < s->scripts; i++)
        if (s->tile_scripts[i].tile >= COOP_SECTOR_TILES || (i && s->tile_scripts[i - 1].tile >= s->tile_scripts[i].tile)) return false;
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++) {
        uint32_t roof = s->roofs[i];
        unsigned piece = ((roof >> 14) & 31) + ((roof & 1) ? 9 : 0);
        if (roof != UINT32_MAX && ((roof >> 28) != 10 || (roof & 0x3000) || piece > 12)) return false;
    }
    for (unsigned i = 0; i < s->lights; i++) {
        const CoopSectorLight* l = &s->static_lights[i];
        if (l->tile >= COOP_SECTOR_TILES || (l->art >> 28) != 9 || (l->flags & ~0x3bu)
            || (l->flags & 0x18) == 0x18 || l->color > 0xffffff
            || ((l->flags & 0x18) && l->color)
            || (int32_t)l->offset_x < -1048576 || (int32_t)l->offset_x > 1048576
            || (int32_t)l->offset_y < -1048576 || (int32_t)l->offset_y > 1048576) return false;
    }
    return true;
}
uint8_t* coop_sector_encode(const CoopSectorState* s, size_t* size)
{
    *size = 0;
    if (!coop_sector_valid(s)) return NULL;
    size_t length = COOP_SECTOR_FIXED_BYTES + s->scripts * 16 + s->lights * COOP_SECTOR_LIGHT_BYTES;
    uint8_t* p = malloc(length);
    if (!p) return NULL;
    memcpy(p, "ASC2", 4);
    uint32_t header[] = { s->x, s->y, s->townmap, s->aptitude, s->light, s->sound_flags, s->music, s->ambient, s->script_flags, s->script_counters, s->script_number, s->scripts, s->blocked };
    for (unsigned i = 0; i < 13; i++)
        coop_object_write32(p + 4 + i * 4, header[i]);
    coop_object_write32(p + 56, s->lights);
    size_t at = 60;
    for (unsigned i = 0; i < COOP_SECTOR_TILES; i++, at += 4)
        coop_object_write32(p + at, s->tiles[i]);
    for (unsigned i = 0; i < COOP_SECTOR_BLOCKS; i++, at += 4)
        coop_object_write32(p + at, s->blocks[i]);
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++, at += 4)
        coop_object_write32(p + at, s->roofs[i]);
    for (unsigned i = 0; i < s->scripts; i++, at += 16) {
        const CoopSectorScript* t = &s->tile_scripts[i];
        coop_object_write32(p + at, t->tile);
        coop_object_write32(p + at + 4, t->flags);
        coop_object_write32(p + at + 8, t->counters);
        coop_object_write32(p + at + 12, t->number);
    }
    for (unsigned i = 0; i < s->lights; i++, at += COOP_SECTOR_LIGHT_BYTES) {
        const CoopSectorLight* l = &s->static_lights[i];
        const uint32_t fields[] = { l->tile, l->offset_x, l->offset_y, l->flags, l->art, l->color };
        for (unsigned j = 0; j < 6; j++)
            coop_object_write32(p + at + j * 4, fields[j]);
    }
    *size = length;
    return p;
}
bool coop_sector_decode(const void* data, size_t size, CoopSectorState* out)
{
    const uint8_t* p = data;
    if (!p || !out || size < COOP_SECTOR_FIXED_BYTES || memcmp(p, "ASC2", 4)) return false;
    unsigned count = coop_object_read32(p + 48);
    unsigned lights = coop_object_read32(p + 56);
    if (count > COOP_SECTOR_SCRIPT_LIMIT || lights > COOP_SECTOR_LIGHT_LIMIT
        || size != COOP_SECTOR_FIXED_BYTES + count * 16 + lights * COOP_SECTOR_LIGHT_BYTES || coop_object_read32(p + 52) > 1) return false;
    CoopSectorState* s = calloc(1, sizeof(*s));
    if (!s) return false;
    uint32_t* header[] = { &s->x, &s->y, &s->townmap, &s->aptitude, &s->light, &s->sound_flags, &s->music, &s->ambient, &s->script_flags, &s->script_counters, &s->script_number };
    for (unsigned i = 0; i < 11; i++)
        *header[i] = coop_object_read32(p + 4 + i * 4);
    s->scripts = count;
    s->lights = lights;
    s->blocked = coop_object_read32(p + 52) != 0;
    size_t at = 60;
    for (unsigned i = 0; i < COOP_SECTOR_TILES; i++, at += 4)
        s->tiles[i] = coop_object_read32(p + at);
    for (unsigned i = 0; i < COOP_SECTOR_BLOCKS; i++, at += 4)
        s->blocks[i] = coop_object_read32(p + at);
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++, at += 4)
        s->roofs[i] = coop_object_read32(p + at);
    for (unsigned i = 0; i < count; i++, at += 16)
        s->tile_scripts[i] = (CoopSectorScript) { coop_object_read32(p + at), coop_object_read32(p + at + 4), coop_object_read32(p + at + 8), coop_object_read32(p + at + 12) };
    for (unsigned i = 0; i < lights; i++, at += COOP_SECTOR_LIGHT_BYTES)
        s->static_lights[i] = (CoopSectorLight) { coop_object_read32(p + at), coop_object_read32(p + at + 4), coop_object_read32(p + at + 8), coop_object_read32(p + at + 12), coop_object_read32(p + at + 16), coop_object_read32(p + at + 20) };
    bool good = coop_sector_valid(s);
    if (good) *out = *s;
    free(s);
    return good;
}

uint8_t* coop_sector_visual_encode(const CoopSectorState* s, size_t* size)
{
    if (!size) return NULL;
    *size = 0;
    if (!coop_sector_valid(s)) return NULL;
    size_t length = COOP_SECTOR_VISUAL_BYTES + s->lights * COOP_SECTOR_LIGHT_BYTES;
    uint8_t* p = malloc(length);
    if (!p) return NULL;
    memcpy(p, "ACV1", 4);
    const uint32_t header[] = { s->x, s->y, s->sound_flags, s->music, s->ambient, s->lights };
    for (unsigned i = 0; i < 6; i++)
        coop_object_write32(p + 4 + i * 4, header[i]);
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++)
        coop_object_write32(p + 28 + i * 4, s->roofs[i]);
    for (unsigned i = 0; i < s->lights; i++) {
        const CoopSectorLight* l = &s->static_lights[i];
        const uint32_t fields[] = { l->tile, l->offset_x, l->offset_y, l->flags, l->art, l->color };
        for (unsigned j = 0; j < 6; j++)
            coop_object_write32(p + COOP_SECTOR_VISUAL_BYTES + i * 24 + j * 4, fields[j]);
    }
    *size = length;
    return p;
}
bool coop_sector_visual_decode(const void* bytes, size_t size, CoopSectorState* out)
{
    const uint8_t* p = bytes;
    if (!p || !out || size < COOP_SECTOR_VISUAL_BYTES || memcmp(p, "ACV1", 4)) return false;
    unsigned count = coop_object_read32(p + 24);
    if (count > COOP_SECTOR_LIGHT_LIMIT || size != COOP_SECTOR_VISUAL_BYTES + count * 24) return false;
    CoopSectorState* s = calloc(1, sizeof(*s));
    if (!s) return false;
    s->x = coop_object_read32(p + 4);
    s->y = coop_object_read32(p + 8);
    s->sound_flags = coop_object_read32(p + 12);
    s->music = coop_object_read32(p + 16);
    s->ambient = coop_object_read32(p + 20);
    s->lights = count;
    for (unsigned i = 0; i < COOP_SECTOR_ROOFS; i++)
        s->roofs[i] = coop_object_read32(p + 28 + i * 4);
    for (unsigned i = 0; i < count; i++) {
        const uint8_t* q = p + COOP_SECTOR_VISUAL_BYTES + i * 24;
        s->static_lights[i] = (CoopSectorLight) { coop_object_read32(q), coop_object_read32(q + 4), coop_object_read32(q + 8), coop_object_read32(q + 12), coop_object_read32(q + 16), coop_object_read32(q + 20) };
    }
    bool good = coop_sector_valid(s);
    if (good) *out = *s;
    free(s);
    return good;
}
