#include "coop/control_state.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    CoopControlRequest r = { .epoch = 7, .sequence = 8, .map = 1, .actor = { 1 }, .action = COOP_CONTROL_REST, .value = 7 }, decoded;
    uint8_t bytes[COOP_CONTROL_REQUEST_BYTES];
    CHECK(coop_control_request_encode(&r, bytes) && coop_control_request_decode(bytes, sizeof(bytes), &decoded));
    CHECK(decoded.value == 7 && decoded.epoch == 7 && bytes[28] == COOP_CONTROL_REST);
    for (size_t n = 0; n < sizeof(bytes); n++) CHECK(!coop_control_request_decode(bytes, n, &decoded));
    r.value = 8; CHECK(!coop_control_request_encode(&r, bytes)); r.value = 0;
    r.action = COOP_CONTROL_VOTE; CHECK(!coop_control_request_encode(&r, bytes)); r.proposal = 2;
    CHECK(coop_control_request_encode(&r, bytes)); r.bed.bytes[0] = 2; r.bed.bytes[1] = 3;
    CHECK(!coop_control_request_encode(&r, bytes)); r.bed = (CoopObjectId) { 0 };
    r.action = COOP_CONTROL_SAVE; CHECK(!coop_control_request_encode(&r, bytes)); r.proposal = 0;
    CHECK(coop_control_request_encode(&r, bytes));
    r.action = COOP_CONTROL_TRAVEL; r.value = UINT32_MAX;
    CHECK(coop_control_request_encode(&r, bytes) && coop_control_request_decode(bytes, sizeof(bytes), &decoded) && decoded.value == UINT32_MAX);
    r.route_count = COOP_JOURNEY_WAYPOINTS;
    for (unsigned i = 0; i < r.route_count; i++) r.route[i] = (500u + i) << 16 | (700u + i);
    r.value = r.route[r.route_count - 1];
    CHECK(coop_control_request_encode(&r, bytes) && coop_control_request_decode(bytes, sizeof(bytes), &decoded));
    CHECK(decoded.route_count == COOP_JOURNEY_WAYPOINTS && !memcmp(r.route, decoded.route, sizeof(r.route)));
    bytes[123] = COOP_JOURNEY_WAYPOINTS + 1; CHECK(!coop_control_request_decode(bytes, sizeof(bytes), &decoded));
    r.route[1] = r.route[0]; CHECK(!coop_control_request_encode(&r, bytes)); r.route[1]++;
    r.value++; CHECK(!coop_control_request_encode(&r, bytes)); r.value--;
    r.action = COOP_CONTROL_REST; CHECK(!coop_control_request_encode(&r, bytes));
    r.route_count = 0; memset(r.route, 0, sizeof(r.route));
    r.action = COOP_CONTROL_SAVE; r.value = 0; CHECK(coop_control_request_encode(&r, bytes));
    CoopControlView v = { .epoch = 7, .revision = 9, .participants = 15, .proposer = COOP_NO_SLOT, .paused = 6 }, view;
    uint8_t state[COOP_CONTROL_VIEW_BYTES];
    CHECK(coop_control_view_encode(&v, state) && coop_control_view_decode(state, sizeof(state), &view));
    CHECK(view.paused == 6 && view.proposer == COOP_NO_SLOT);
    v.phase = COOP_CONTROL_VOTING; v.action = COOP_CONTROL_REST; v.proposal = 3; v.value = 7; v.proposer = 2; v.agreed = 4; v.remaining_ms = 30000;
    CHECK(coop_control_view_encode(&v, state)); v.refused = 4; CHECK(!coop_control_view_encode(&v, state)); v.refused = 0;
    v.phase = COOP_CONTROL_RESTING; CHECK(!coop_control_view_encode(&v, state)); v.agreed = 15; v.remaining_ms = 0;
    v.completed = 168; CHECK(coop_control_view_encode(&v, state)); v.completed = 169; CHECK(!coop_control_view_encode(&v, state)); v.completed = 12;
    CHECK(coop_control_view_encode(&v, state));
    v.phase = COOP_CONTROL_JOURNEY; v.action = COOP_CONTROL_TRAVEL; v.value = UINT32_MAX; v.completed = 1000000;
    CHECK(coop_control_view_encode(&v, state)); v.completed++; CHECK(!coop_control_view_encode(&v, state));
    v.completed = 0; v.route_count = 2; v.route[0] = 123; v.route[1] = v.value;
    CHECK(coop_control_view_encode(&v, state) && coop_control_view_decode(state, sizeof(state), &view));
    CHECK(view.route_count == 2 && view.route[0] == 123 && view.route[1] == UINT32_MAX);
    v.route[2] = 1; CHECK(!coop_control_view_encode(&v, state)); v.route[2] = 0;
    v.route_count = 0; memset(v.route, 0, sizeof(v.route));
    v.phase = COOP_CONTROL_RESTING; v.action = COOP_CONTROL_REST; v.value = 7; v.completed = 12; CHECK(coop_control_view_encode(&v, state));
    for (unsigned i = 0; i < 20000; i++) {
        uint8_t changed[COOP_CONTROL_VIEW_BYTES], canonical[COOP_CONTROL_VIEW_BYTES];
        memcpy(changed, state, sizeof(state)); changed[(i * 7) % sizeof(state)] ^= (uint8_t)(i + 1);
        if (coop_control_view_decode(changed, sizeof(changed), &view)) CHECK(coop_control_view_encode(&view, canonical) && !memcmp(changed, canonical, sizeof(changed)));
        memcpy(changed, bytes, sizeof(bytes)); changed[(i * 13) % sizeof(bytes)] ^= (uint8_t)(i + 1);
        if (coop_control_request_decode(changed, sizeof(bytes), &decoded)) CHECK(coop_control_request_encode(&decoded, canonical) && !memcmp(changed, canonical, sizeof(bytes)));
    }
    puts("PASS: canonical party controls, votes/pause masks, bounds and 40,000 wire mutations");
    return 0;
}
