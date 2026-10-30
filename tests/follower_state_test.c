#include "coop/follower_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
int main(void)
{
    CoopFollowerRequest r = { .epoch = 1, .sequence = 1, .map = 1, .generation = 2, .actor = { 3 }, .follower = { .bytes = { 2, 9 } } }, decoded;
    uint8_t bytes[COOP_FOLLOWER_REQUEST_BYTES];
    for (unsigned order = 0; order <= COOP_FOLLOW_INVENTORY; order++) {
        r.order = (CoopFollowerOrder)order; memset(&r.target, 0, sizeof(r.target));
        if (order == COOP_FOLLOW_ATTACK) { r.target.bytes[0] = 2; r.target.bytes[1] = 4; }
        CHECK(coop_follower_encode(&r, bytes) && coop_follower_decode(bytes, sizeof(bytes), &decoded));
        CHECK(bytes[70] == order && bytes[72] == 0 && bytes[75] == 2);
        uint32_t random = 42;
        for (unsigned i = 0; i < 10000; i++) {
            random = random * 1664525u + 1013904223u;
            unsigned at = random % sizeof(bytes); uint8_t old = bytes[at]; bytes[at] ^= (uint8_t)(random >> 24);
            if (coop_follower_decode(bytes, sizeof(bytes), &decoded)) {
                uint8_t again[sizeof(bytes)]; CHECK(coop_follower_encode(&decoded, again) && !memcmp(bytes, again, sizeof(bytes)));
            }
            bytes[at] = old;
        }
    }
    r.order = COOP_FOLLOW_INVENTORY;
    r.target.bytes[0] = 2;
    CHECK(!coop_follower_encode(&r, bytes));
    memset(&r.target, 0, sizeof(r.target));
    r.force = true;
    CHECK(!coop_follower_encode(&r, bytes));
    r.force = false;
    r.order = COOP_FOLLOW_INVENTORY + 1;
    CHECK(!coop_follower_encode(&r, bytes));
    r.order = COOP_FOLLOW_WAIT; r.force = true; CHECK(!coop_follower_encode(&r, bytes)); r.force = false;
    r.x = 1; CHECK(!coop_follower_encode(&r, bytes)); r.x = 0; r.generation = 0;
    CHECK(coop_follower_encode(&r, bytes) && coop_follower_decode(bytes, sizeof(bytes), &decoded));
    r.generation = 1; r.follower.bytes[0] = 3; CHECK(!coop_follower_encode(&r, bytes));
    r = (CoopFollowerRequest){ .epoch = 2, .sequence = 3, .map = 4, .generation = 5, .actor = { 6 }, .order = COOP_FOLLOW_BROADCAST };
    memcpy(r.command, "# wait", 7); r.x = 100; r.y = 200; r.target.bytes[0] = 2; r.target.bytes[1] = 7;
    CHECK(coop_follower_encode(&r, bytes) && coop_follower_decode(bytes, sizeof(bytes), &decoded));
    CHECK(!strcmp(decoded.command, "# wait") && decoded.x == 100 && decoded.y == 200 && decoded.target.bytes[1] == 7);
    CHECK(!coop_follower_decode(bytes, 76, &decoded));
    bytes[76 + 20] = 'x'; CHECK(!coop_follower_decode(bytes, sizeof(bytes), &decoded)); bytes[76 + 20] = 0;
    r.command[1] = '\n'; CHECK(!coop_follower_encode(&r, bytes)); r.command[1] = ' ';
    r.follower.bytes[0] = 2; r.follower.bytes[1] = 9; CHECK(!coop_follower_encode(&r, bytes)); memset(&r.follower, 0, sizeof(r.follower));
    r.force = true; CHECK(!coop_follower_encode(&r, bytes)); r.force = false;
    memset(r.command, 'x', sizeof(r.command)); CHECK(!coop_follower_encode(&r, bytes));
    puts("PASS: nine canonical follower orders, 90000 mutations and bounded owner-scoped broadcasts");
    return 0;
}
