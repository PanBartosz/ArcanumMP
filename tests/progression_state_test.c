#include "coop/progression_state.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
int main(void)
{
    CoopProgressionRequest r = { .epoch = 1, .sequence = 2, .map = 3, .operation = COOP_PROGRESS_OPEN }, parsed;
    r.actor[15] = 1;
    uint8_t bytes[COOP_PROGRESSION_REQUEST_BYTES], changed[COOP_PROGRESSION_REQUEST_BYTES];
    CHECK(coop_progression_request_encode(&r, bytes));
    for (size_t size = 0; size < sizeof(bytes); size++) CHECK(!coop_progression_request_decode(bytes, size, &parsed));
    CHECK(coop_progression_request_decode(bytes, sizeof(bytes), &parsed));
    r.lease = 1; CHECK(!coop_progression_request_encode(&r, bytes));
    r.operation = COOP_PROGRESS_CLOSE; CHECK(coop_progression_request_encode(&r, bytes));
    r.direction = 1; CHECK(!coop_progression_request_encode(&r, bytes));
    r.operation = COOP_PROGRESS_EDIT; r.expected = 8; r.points = 5;
    for (unsigned kind = 0; kind <= COOP_PROGRESS_SCHEME; kind++) {
        r.kind = (CoopProgressionKind)kind;
        unsigned count = kind == COOP_PROGRESS_SCHEME ? 200 : kind == COOP_PROGRESS_STAT || kind == COOP_PROGRESS_DISCIPLINE ? 8 : kind == COOP_PROGRESS_SKILL || kind == COOP_PROGRESS_COLLEGE ? 16 : 1;
        for (unsigned index = 0; index < count; index++) {
            r.index = (uint8_t)index;
            for (r.direction = 1; r.direction <= 2; r.direction++) {
                if (kind == COOP_PROGRESS_SCHEME && r.direction == 2) { CHECK(!coop_progression_request_encode(&r, bytes)); continue; }
                CHECK(coop_progression_request_encode(&r, bytes) && coop_progression_request_decode(bytes, sizeof(bytes), &parsed));
                CHECK(parsed.kind == r.kind && parsed.index == index && parsed.direction == r.direction && parsed.points == 5 && parsed.expected == 8);
            }
        }
        r.index = (uint8_t)count; r.direction = 1; CHECK(!coop_progression_request_encode(&r, bytes));
    }
    r.kind = COOP_PROGRESS_STAT; r.index = 0; r.direction = 2;
    r.expected = UINT32_MAX; CHECK(!coop_progression_request_encode(&r, bytes)); r.expected = 8;
    r.points = UINT32_MAX; CHECK(!coop_progression_request_encode(&r, bytes)); r.points = 5;
    CHECK(coop_progression_request_encode(&r, bytes));
    uint32_t state = 0x501A7u;
    for (unsigned i = 0; i < 20000; i++) {
        state = state * 1664525u + 1013904223u;
        memcpy(changed, bytes, sizeof(changed)); changed[state % sizeof(changed)] ^= (uint8_t)(1u << ((state >> 16) & 7));
        if (coop_progression_request_decode(changed, sizeof(changed), &parsed)) {
            uint8_t canonical[COOP_PROGRESSION_REQUEST_BYTES]; CHECK(coop_progression_request_encode(&parsed, canonical));
            CHECK(!memcmp(changed, canonical, sizeof(canonical)));
        }
    }
    r = (CoopProgressionRequest) { .epoch=1, .sequence=2, .map=3, .operation=COOP_PROGRESS_OPTIONS, .direction=15, .expected=0x80 };
    r.actor[0]=1;
    CHECK(coop_progression_request_encode(&r,bytes));
    r.expected=0xc4;
    CHECK(coop_progression_request_encode(&r,bytes) && coop_progression_request_decode(bytes,sizeof(bytes),&parsed) && parsed.direction==15);
    r.expected=1; CHECK(!coop_progression_request_encode(&r,bytes)); r.expected=0;
    r.direction=31; r.expected=0xf0;
    CHECK(coop_progression_request_encode(&r,bytes) && coop_progression_request_decode(bytes,sizeof(bytes),&parsed) && parsed.direction==31 && parsed.expected==0xf0);
    r.direction=32; CHECK(!coop_progression_request_encode(&r,bytes)); r.direction=0; r.expected=0;
    CHECK(coop_progression_request_encode(&r,bytes));
    puts("progression schema PASS");
    return 0;
}
