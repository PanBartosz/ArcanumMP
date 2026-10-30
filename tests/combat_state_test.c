#include "coop/combat_state.h"
#include <limits.h>
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
    CoopCombatState s = { .turn_based = true, .active = true, .round = 7, .generation = 0x12345678, .points = 11 };
    s.actor.bytes[0] = 2;
    s.actor.bytes[1] = 1;
    uint8_t bytes[COOP_COMBAT_STATE_BYTES];
    CHECK(coop_combat_state_encode(&s, bytes));
    CHECK(!memcmp(bytes, "ACB2\0\0\0\3\0\0\0\13\0\0\0\7\x12\x34\x56\x78", 20));
    CoopCombatState decoded, retained;
    CHECK(coop_combat_state_decode(bytes, sizeof(bytes), &decoded));
    CHECK(decoded.active && decoded.turn_based && decoded.points == 11 && decoded.round == 7 && decoded.generation == s.generation);
    CoopCombatState ordered = s;
    ordered.order_count = 2;
    ordered.order[0] = s.actor;
    ordered.order[1] = s.actor; ordered.order[1].bytes[1] = 9;
    CHECK(coop_combat_state_encode(&ordered, bytes) && coop_combat_state_decode(bytes, sizeof(bytes), &decoded)
        && decoded.order_count == 2 && !memcmp(decoded.order, ordered.order, sizeof(ordered.order)));
    ordered.order[1] = ordered.order[0]; CHECK(!coop_combat_state_encode(&ordered, bytes));
    ordered.order[1].bytes[1] = 9; ordered.order_count = 1; CHECK(!coop_combat_state_encode(&ordered, bytes));
    CHECK(coop_combat_state_encode(&s, bytes));
    memset(&decoded, 0x42, sizeof(decoded));
    retained = decoded;
    for (unsigned n = 0; n < sizeof(bytes); n++)
        CHECK(!coop_combat_state_decode(bytes, n, &decoded) && !memcmp(&decoded, &retained, sizeof(decoded)));
    s.active = false;
    CHECK(!coop_combat_state_encode(&s, bytes));
    s.actor = (CoopObjectId) { 0 };
    s.points = s.round = 0;
    CHECK(coop_combat_state_encode(&s, bytes));
    s.active = true;
    CHECK(!coop_combat_state_encode(&s, bytes));
    s.active = false;
    s.points = UINT32_MAX;
    CHECK(!coop_combat_state_encode(&s, bytes));
    s.points = 0;
    s.generation = UINT32_MAX;
    CHECK(!coop_combat_state_encode(&s, bytes));
    s.generation = 4;
    CoopCombatRequest r = { .epoch = 3, .sequence = 5, .map = 1, .operation = COOP_COMBAT_ATTACK, .value = 1, .generation = 4 };
    r.actor[0] = 1;
    r.target.bytes[0] = 2;
    r.target.bytes[1] = 2;
    uint8_t request[COOP_COMBAT_REQUEST_BYTES];
    CoopCombatRequest read, saved;
    CHECK(coop_combat_request_encode(&r, request) && coop_combat_request_decode(request, sizeof(request), &read));
    CHECK(!memcmp(request, "\0\0\0\3\0\0\0\5\0\0\0\1", 12));
    CHECK(read.operation == COOP_COMBAT_ATTACK && read.value == 1 && read.generation == 4);
    for (unsigned value = 0; value <= UINT8_MAX; value++) {
        r.value = (uint8_t)value;
        bool valid = value <= COOP_ATTACK_VALUE_MASK;
        CHECK(coop_combat_request_encode(&r, request) == valid);
        if (valid) {
            CHECK(coop_combat_request_decode(request, sizeof(request), &read));
            CHECK(read.value == value);
        } else {
            request[29] = (uint8_t)value;
            CHECK(!coop_combat_request_decode(request, sizeof(request), &read));
        }
    }
    r.value = 1;
    CHECK(coop_combat_request_encode(&r, request));
    memset(&read, 0x42, sizeof(read));
    saved = read;
    for (unsigned n = 0; n < sizeof(request); n++)
        CHECK(!coop_combat_request_decode(request, n, &read) && !memcmp(&saved, &read, sizeof(read)));
    r.operation = COOP_COMBAT_END_TURN;
    CHECK(!coop_combat_request_encode(&r, request));
    r.value = 0;
    r.target = (CoopObjectId) { 0 };
    CHECK(coop_combat_request_encode(&r, request));
    r.operation = COOP_COMBAT_POLICY;
    r.value = 1;
    CHECK(coop_combat_request_encode(&r, request));
    r.operation = COOP_COMBAT_IMMORTAL;
    for (unsigned value = 0; value < 16; value++) {
        r.value = (uint8_t)value;
        CHECK(coop_combat_request_encode(&r, request) == (value < 8));
        if (value < 8) CHECK(coop_combat_request_decode(request, sizeof(request), &read) && read.value == value);
    }
    for (unsigned operation = COOP_COMBAT_STANCE; operation <= COOP_COMBAT_POLICY; operation++) {
        r.operation = (CoopCombatOperation)operation;
        for (unsigned value = 2; value <= COOP_ATTACK_VALUE_MASK; value++) {
            r.value = (uint8_t)value;
            CHECK(coop_combat_request_encode(&r, request) == (operation == COOP_COMBAT_POLICY && value == 2));
        }
    }
    uint32_t random = 20261020;
    for (unsigned i = 0; i < 10000; i++) {
        random = random * 1664525u + 1013904223u;
        size_t at = random % sizeof(bytes);
        uint8_t old = bytes[at];
        bytes[at] ^= (uint8_t)(random >> 24);
        if (coop_combat_state_decode(bytes, sizeof(bytes), &decoded)) {
            uint8_t canonical[sizeof(bytes)];
            CHECK(coop_combat_state_encode(&decoded, canonical) && !memcmp(canonical, bytes, sizeof(bytes)));
        }
        bytes[at] = old;
        at = random % sizeof(request);
        old = request[at];
        request[at] ^= (uint8_t)(random >> 16);
        if (coop_combat_request_decode(request, sizeof(request), &read)) {
            uint8_t canonical[sizeof(request)];
            CHECK(coop_combat_request_encode(&read, canonical) && !memcmp(canonical, request, sizeof(request)));
        }
        request[at] = old;
    }
    puts("PASS: combat state/intent byte order, canonical records, turn/enum/identity bounds and malformed corpus");
    return 0;
}
