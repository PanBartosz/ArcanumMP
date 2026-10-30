#include "coop/cartography.h"
#include "coop/object_state.h"
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
    CoopCartography maps = { 0 }, decoded = { 0 };
    uint8_t bits[COOP_CHART_TILES / 8] = { 1 };
    CHECK(coop_cartography_put(&maps, 21, 19600, bits));
    CHECK(coop_cartography_put(&maps, 3, 9, bits));
    CHECK(maps.count == 2 && maps.charts[0].id == 3);
    CHECK(!coop_cartography_put(&maps, 0, 1, bits));
    CHECK(!coop_cartography_put(&maps, 4096, 1, bits));
    CHECK(!coop_cartography_put(&maps, 10, COOP_CHART_TILES + 1, bits));
    bits[1] = 2;
    CHECK(!coop_cartography_put(&maps, 10, 9, bits));
    bits[1] = 0;
    size_t length;
    uint8_t* bytes = coop_cartography_encode(&maps, &length);
    CHECK(bytes);
    CHECK(coop_cartography_decode(&decoded, bytes, length));
    CHECK(decoded.count == 2 && coop_cartography_find(&decoded, 21)->tiles == 19600);
    const uint8_t* old = decoded.charts[0].bits;
    for (size_t i = 0; i < length; i++) {
        CHECK(!coop_cartography_decode(&decoded, bytes, i));
        CHECK(decoded.charts[0].bits == old);
    }
    uint32_t random = 33;
    for (unsigned i = 0; i < 20000; i++) {
        random = random * 1664525u + 1013904223u;
        size_t at = random % length;
        uint8_t saved = bytes[at];
        bytes[at] ^= (uint8_t)(random >> 24);
        if (coop_cartography_decode(&decoded, bytes, length)) {
            size_t n;
            uint8_t* copy = coop_cartography_encode(&decoded, &n);
            CHECK(copy && n == length && !memcmp(copy, bytes, n));
            free(copy);
        }
        bytes[at] = saved;
    }
    free(bytes);
    coop_cartography_clear(&decoded);
    coop_cartography_clear(&maps);
    memset(bits, 255, sizeof(bits));
    for (unsigned i = 1; i <= COOP_CHART_MAPS; i++)
        CHECK(coop_cartography_put(&maps, i, COOP_CHART_TILES, bits));
    CHECK(!coop_cartography_put(&maps, COOP_CHART_MAPS + 1, COOP_CHART_TILES, bits));
    bytes = coop_cartography_encode(&maps, &length);
    CHECK(bytes && length == COOP_CHART_LIMIT);
    CHECK(coop_cartography_decode(&decoded, bytes, length) && decoded.count == COOP_CHART_MAPS);
    coop_object_write32(bytes + 4, COOP_CHART_MAPS + 1);
    CHECK(!coop_cartography_decode(&decoded, bytes, length));
    free(bytes);
    memset(bits, 0, sizeof(bits));
    CHECK(coop_cartography_put(&maps, 21, 19600, bits) && !coop_cartography_find(&maps, 21));
    coop_cartography_clear(&maps);
    coop_cartography_clear(&decoded);
    puts("PASS: bounded canonical exploration, sparse maps, padding, atomic decode, full capacity and mutation corpus");
    return 0;
}
