#include "coop/service_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
int main(void)
{
    CoopServiceRequest r = { .epoch = 1, .sequence = 2, .map = 3, .conversation = 4, .page = 5, .quantity = 1 };
    memset(r.actor, 6, 16); r.item.bytes[0] = 2; memset(r.item.bytes + 1, 7, 16);
    uint8_t bytes[COOP_SERVICE_REQUEST_BYTES]; CoopServiceRequest decoded;
    CHECK(coop_service_request_encode(&r, bytes));
    CHECK(bytes[3] == 1 && bytes[7] == 2 && bytes[31] == 4 && bytes[35] == 5 && bytes[36] == 0 && bytes[37] == 2 && bytes[57] == 0);
    CHECK(coop_service_request_decode(bytes, sizeof(bytes), &decoded));
    CHECK(decoded.page == 5 && decoded.operation == COOP_SERVICE_QUOTE && !decoded.quote);
    for (size_t n = 0; n < sizeof(bytes); n++) CHECK(!coop_service_request_decode(bytes, n, &decoded));
    r.operation = COOP_SERVICE_BUY; CHECK(!coop_service_request_encode(&r, bytes));
    r.quote = 9; CHECK(coop_service_request_encode(&r, bytes));
    CHECK(coop_service_request_decode(bytes, sizeof(bytes), &decoded) && decoded.quote == 9);
    r.operation = COOP_SERVICE_QUOTE; CHECK(!coop_service_request_encode(&r, bytes));
    r.quote = 0; r.page = 0; CHECK(!coop_service_request_encode(&r, bytes)); r.page = 5;
    r.item.bytes[0] = 0; CHECK(!coop_service_request_encode(&r, bytes)); r.item.bytes[0] = 2;
    r.map = UINT32_MAX; CHECK(!coop_service_request_encode(&r, bytes)); r.map = 3;
    r.quantity = 0; CHECK(!coop_service_request_encode(&r, bytes)); r.quantity = 1000001;
    CHECK(!coop_service_request_encode(&r, bytes)); r.quantity = 7;
    CHECK(coop_service_request_encode(&r, bytes) && coop_service_request_decode(bytes, sizeof(bytes), &decoded) && decoded.quantity == 7);
    r.operation = COOP_SERVICE_CRAFT; r.recipe = 2000;
    CHECK(!coop_service_request_encode(&r, bytes)); r.quantity = 1;
    CHECK(coop_service_request_encode(&r, bytes) && coop_service_request_decode(bytes, sizeof(bytes), &decoded) && decoded.recipe == 2000);
    r.conversation = r.page = 0;
    CHECK(coop_service_request_encode(&r, bytes)); r.page = 1; CHECK(!coop_service_request_encode(&r, bytes)); r.page = 0;
    r.quote = 1; CHECK(!coop_service_request_encode(&r, bytes)); r.quote = 0;
    r.recipe = 0; CHECK(!coop_service_request_encode(&r, bytes)); r.recipe = UINT32_MAX;
    CHECK(!coop_service_request_encode(&r, bytes)); r.recipe = 0;
    r.operation = COOP_SERVICE_QUOTE; r.conversation = 4; r.page = 5;
    CHECK(coop_service_request_encode(&r, bytes));
    for (unsigned i = 0; i < 20000; i++) {
        uint8_t mutated[sizeof(bytes)], canonical[sizeof(bytes)]; memcpy(mutated, bytes, sizeof(bytes));
        for (unsigned j = 0; j < 1 + i % 8; j++) mutated[(i * 7 + j * 13) % sizeof(bytes)] ^= (uint8_t)(i + j + 1);
        if (coop_service_request_decode(mutated, sizeof(mutated), &decoded)) {
            CHECK(coop_service_request_encode(&decoded, canonical)); CHECK(!memcmp(canonical, mutated, sizeof(mutated)));
        }
    }
    puts("PASS: canonical service quotes/buys/crafting, quantity/recipe bounds and 20,000 request mutations");
    return 0;
}
