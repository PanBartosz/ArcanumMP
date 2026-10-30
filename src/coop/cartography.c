#include "coop/cartography.h"
#include "coop/object_state.h"
#include <stdlib.h>
#include <string.h>
static size_t bit_bytes(uint32_t tiles) { return (tiles + 7u) / 8u; }
static bool valid(uint32_t id, uint32_t tiles, const uint8_t* bits)
{
    return id && id <= 4095 && tiles && tiles <= COOP_CHART_TILES && bits
        && (!(tiles & 7) || !(bits[bit_bytes(tiles) - 1] & (uint8_t)(0xffu << (tiles & 7))));
}
static bool nonzero(const uint8_t* bits, size_t size)
{
    for (size_t i = 0; i < size; i++)
        if (bits[i]) return true;
    return false;
}
void coop_cartography_clear(CoopCartography* maps)
{
    if (!maps) return;
    for (unsigned i = 0; i < maps->count; i++)
        free(maps->charts[i].bits);
    free(maps->charts);
    *maps = (CoopCartography) { 0 };
}
const CoopChart* coop_cartography_find(const CoopCartography* maps, uint32_t id)
{
    if (maps)
        for (unsigned i = 0; i < maps->count; i++)
            if (maps->charts[i].id == id) return &maps->charts[i];
    return NULL;
}
bool coop_cartography_put(CoopCartography* maps, uint32_t id, uint32_t tiles, const uint8_t* bits)
{
    if (!maps || !valid(id, tiles, bits)) return false;
    unsigned at = 0;
    while (at < maps->count && maps->charts[at].id < id)
        at++;
    bool existing = at < maps->count && maps->charts[at].id == id;
    size_t size = bit_bytes(tiles);
    if (!nonzero(bits, size)) {
        if (existing) {
            free(maps->charts[at].bits);
            memmove(maps->charts + at, maps->charts + at + 1, (maps->count - at - 1) * sizeof(*maps->charts));
            maps->count--;
        }
        return true;
    }
    if (!existing && maps->count == COOP_CHART_MAPS) return false;
    uint8_t* copy = malloc(size);
    if (!copy) return false;
    memcpy(copy, bits, size);
    if (existing)
        free(maps->charts[at].bits);
    else {
        CoopChart* charts = realloc(maps->charts, (maps->count + 1) * sizeof(*charts));
        if (!charts) {
            free(copy);
            return false;
        }
        maps->charts = charts;
        memmove(charts + at + 1, charts + at, (maps->count - at) * sizeof(*charts));
        maps->count++;
    }
    maps->charts[at] = (CoopChart) { id, tiles, copy };
    return true;
}
uint8_t* coop_cartography_encode(const CoopCartography* maps, size_t* size)
{
    if (!maps || !size || maps->count > COOP_CHART_MAPS || (maps->count && !maps->charts)) return NULL;
    size_t length = 8;
    for (unsigned i = 0; i < maps->count; i++) {
        const CoopChart* chart = &maps->charts[i];
        if (!valid(chart->id, chart->tiles, chart->bits) || (i && chart->id <= maps->charts[i - 1].id) || !nonzero(chart->bits, bit_bytes(chart->tiles))) return NULL;
        length += 8 + bit_bytes(chart->tiles);
    }
    if (length > COOP_CHART_LIMIT) return NULL;
    uint8_t* bytes = calloc(1, length);
    if (!bytes) return NULL;
    memcpy(bytes, "ACT1", 4);
    coop_object_write32(bytes + 4, maps->count);
    size_t at = 8;
    for (unsigned i = 0; i < maps->count; i++) {
        const CoopChart* chart = &maps->charts[i];
        coop_object_write32(bytes + at, chart->id);
        coop_object_write32(bytes + at + 4, chart->tiles);
        memcpy(bytes + at + 8, chart->bits, bit_bytes(chart->tiles));
        at += 8 + bit_bytes(chart->tiles);
    }
    *size = length;
    return bytes;
}
bool coop_cartography_decode(CoopCartography* maps, const void* data, size_t size)
{
    const uint8_t* bytes = data;
    if (!maps || !bytes || size < 8 || size > COOP_CHART_LIMIT || memcmp(bytes, "ACT1", 4)) return false;
    uint32_t count = coop_object_read32(bytes + 4), previous = 0;
    if (count > COOP_CHART_MAPS) return false;
    CoopCartography candidate = { 0 };
    size_t at = 8;
    bool good = true;
    for (unsigned i = 0; good && i < count; i++) {
        if (size - at < 8) {
            good = false;
            break;
        }
        uint32_t id = coop_object_read32(bytes + at), tiles = coop_object_read32(bytes + at + 4);
        at += 8;
        if (tiles > COOP_CHART_TILES || bit_bytes(tiles) > size - at || id <= previous || !valid(id, tiles, bytes + at) || !nonzero(bytes + at, bit_bytes(tiles))) {
            good = false;
            break;
        }
        good = coop_cartography_put(&candidate, id, tiles, bytes + at);
        previous = id;
        at += bit_bytes(tiles);
    }
    good &= at == size;
    if (good) {
        coop_cartography_clear(maps);
        *maps = candidate;
    } else
        coop_cartography_clear(&candidate);
    return good;
}
