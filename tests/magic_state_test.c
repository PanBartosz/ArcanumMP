#include "coop/magic_state.h"
#include <limits.h>
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
    CoopMagicView view = { 0 }, decoded;
    uint8_t bytes[COOP_MAGIC_VIEW_HEADER + COOP_MAGIC_RUN_LIMIT * COOP_MAGIC_ENTRY_BYTES];
    CHECK(coop_magic_view_size(&view) == COOP_MAGIC_VIEW_HEADER && coop_magic_view_encode(&view, bytes, COOP_MAGIC_VIEW_HEADER));
    CHECK(!memcmp(bytes, "ACM2\0\0\0\0", 8) && coop_magic_view_decode(bytes, COOP_MAGIC_VIEW_HEADER, &decoded));
    view.choices[2]=(CoopMagicChoice){.token=12,.map=3,.spell=79,.charge=1}; view.choices[2].actor[15]=4;
    CHECK(coop_magic_view_encode(&view,bytes,COOP_MAGIC_VIEW_HEADER) && coop_magic_view_decode(bytes,COOP_MAGIC_VIEW_HEADER,&decoded)
        && decoded.choices[2].token==12 && decoded.choices[2].charge==1);
    bytes[8+2*COOP_MAGIC_CHOICE_BYTES+31]=1; CHECK(!coop_magic_view_decode(bytes,COOP_MAGIC_VIEW_HEADER,&decoded));
    view.choices[1]=view.choices[2]; CHECK(!coop_magic_view_valid(&view)); view.choices[1]=(CoopMagicChoice){0};
    view.count = COOP_MAGIC_RUN_LIMIT;
    for (unsigned i = 0; i < view.count; i++) {
        view.entries[i].token = (UINT64_C(1) << 40) + i;
        view.entries[i].owner[15] = 1;
        view.entries[i].spell = 222;
        view.entries[i].target.bytes[0] = 2;
        view.entries[i].target.bytes[16] = 9;
    }
    size_t size = coop_magic_view_size(&view);
    CHECK(size == sizeof(bytes) && coop_magic_view_encode(&view, bytes, size));
    CHECK(bytes[COOP_MAGIC_VIEW_HEADER] == 0 && bytes[COOP_MAGIC_VIEW_HEADER + 2] == 1
        && bytes[COOP_MAGIC_VIEW_HEADER + 24] == 0 && bytes[COOP_MAGIC_VIEW_HEADER + 25] == 222);
    CHECK(coop_magic_view_decode(bytes, size, &decoded) && decoded.count == 512 && decoded.entries[511].token == view.entries[511].token);
    memset(&decoded, 0x42, sizeof(decoded));
    CoopMagicView retained = decoded;
    CHECK(!coop_magic_view_decode(bytes, size - 1, &decoded) && !memcmp(&retained, &decoded, sizeof(decoded)));
    view.entries[1].token = view.entries[0].token;
    CHECK(!coop_magic_view_valid(&view));
    view.entries[1].token++;
    view.entries[0].spell = 223;
    CHECK(!coop_magic_view_valid(&view));
    view.entries[0].spell = 222;
    view.count = 513;
    CHECK(!coop_magic_view_valid(&view));
    uint32_t random = 20261020;
    for (unsigned i = 0; i < 5000; i++) {
        random = random * 1664525u + 1013904223u;
        size_t at = random % size;
        uint8_t saved = bytes[at];
        bytes[at] ^= (uint8_t)(random >> 24);
        if (coop_magic_view_decode(bytes, size, &decoded)) {
            uint8_t encoded[sizeof(bytes)];
            CHECK(coop_magic_view_encode(&decoded, encoded, size) && !memcmp(bytes, encoded, size));
        }
        bytes[at] = saved;
    }
    CoopMagicRequest r = { .epoch = 1, .sequence = 2, .map = 3, .spell = 79, .flags = COOP_MAGIC_LOCATION, .x = 7, .y = 8, .generation = 9 };
    r.actor[15] = 5;
    r.source.bytes[0] = 2;
    r.source.bytes[16] = 5;
    uint8_t request[COOP_MAGIC_REQUEST_BYTES];
    CoopMagicRequest read;
    CHECK(coop_magic_request_encode(&r, request) && coop_magic_request_decode(request, sizeof(request), &read));
    CHECK(request[28] == 0 && request[30] == 79 && request[31] == 2 && coop_object_read32(request + 66) == 7 && coop_object_read32(request + 82) == 9);
    for (unsigned flags = 0; flags < 256; flags++) {
        request[31] = (uint8_t)flags;
        CHECK(coop_magic_request_decode(request, sizeof(request), &read) == (flags == 2 || flags == 3));
    }
    r.flags = 0;
    r.x = r.y = 0;
    r.target.bytes[0] = 2;
    r.target.bytes[16] = 6;
    CHECK(coop_magic_request_encode(&r, request));
    for (unsigned n = 0; n < sizeof(request); n++)
        CHECK(!coop_magic_request_decode(request, n, &read));
    r.source.bytes[0] = 1;
    CHECK(!coop_magic_request_encode(&r, request));
    r = (CoopMagicRequest) { .epoch = 1, .sequence = 3, .map = 3, .operation = COOP_MAGIC_CANCEL, .token = UINT64_C(0xfedcba9876543210), .generation = 9 };
    r.actor[0] = 1;
    CHECK(coop_magic_request_encode(&r, request) && coop_magic_request_decode(request, sizeof(request), &read));
    CHECK(read.token == r.token && request[74] == 0xfe && request[81] == 0x10);
    r.spell = 1;
    CHECK(!coop_magic_request_encode(&r, request));
    r.spell = 0;
    r.flags = 1;
    CHECK(!coop_magic_request_encode(&r, request));
    r.flags = 0;
    r.token = 0;
    CHECK(!coop_magic_request_encode(&r, request));
    r = (CoopMagicRequest) { .epoch = 1, .sequence = 4, .map = 3, .operation = COOP_MAGIC_USE_ITEM, .generation = 9 };
    r.actor[15] = 1;
    r.source.bytes[0] = r.target.bytes[0] = 2;
    r.source.bytes[16] = 1; r.target.bytes[16] = 2;
    CHECK(coop_magic_request_encode(&r, request) && coop_magic_request_decode(request, sizeof(request), &read));
    CHECK(read.operation == COOP_MAGIC_USE_ITEM && !read.spell && request[28] == 2);
    r.spell = 1; CHECK(!coop_magic_request_encode(&r, request)); r.spell = 0;
    r.token = 1; CHECK(!coop_magic_request_encode(&r, request)); r.token = 0;
    r.flags = 8; CHECK(!coop_magic_request_encode(&r, request));
    r.flags = COOP_MAGIC_OWN_SKILL; CHECK(coop_magic_request_encode(&r, request) && coop_magic_request_decode(request,sizeof(request),&read));
    r.flags = COOP_MAGIC_LOCATION; r.target = (CoopObjectId){0}; r.x = 9; r.y = 10;
    CHECK(coop_magic_request_encode(&r, request) && coop_magic_request_decode(request, sizeof(request), &read));
    request[28] = 3; CHECK(!coop_magic_request_decode(request, sizeof(request), &read));
    r.flags = 0; r.x = r.y = 0;
    CHECK(coop_magic_request_encode(&r, request) && coop_magic_request_decode(request, sizeof(request), &read));
    CHECK(read.operation == COOP_MAGIC_USE_ITEM && !read.target.bytes[0] && !read.flags);
    r.flags = COOP_MAGIC_FORCE; CHECK(!coop_magic_request_encode(&r, request)); r.flags = 0;
    r.x = 1; CHECK(!coop_magic_request_encode(&r, request)); r.x = 0;
    r.operation = COOP_MAGIC_CAST; CHECK(!coop_magic_request_encode(&r, request));
    r=(CoopMagicRequest){.epoch=1,.sequence=5,.map=3,.operation=COOP_MAGIC_DESTINATION,.spell=79,.token=12,.x=15};
    r.actor[15]=4; r.source.bytes[0]=2; r.source.bytes[16]=4;
    CHECK(coop_magic_request_encode(&r,request) && coop_magic_request_decode(request,sizeof(request),&read) && read.x==15 && read.token==12);
    r.x=0; CHECK(coop_magic_request_encode(&r,request));
    r.x=4096; CHECK(!coop_magic_request_encode(&r,request)); r.x=15;
    r.flags=COOP_MAGIC_FORCE; CHECK(!coop_magic_request_encode(&r,request));
    CoopWrittenView w = { .epoch = 1, .sequence = 2, .map = 3, .revision = 4, .type = 2, .variant = 1, .number = 123 }, parsed;
    w.reader[15] = 1;
    uint8_t book[COOP_WRITTEN_BYTES];
    CHECK(coop_written_encode(&w, book) && coop_written_decode(book, sizeof(book), &parsed) && parsed.number == 123);
    for (unsigned n = 0; n < sizeof(book); n++) CHECK(!coop_written_decode(book, n, &parsed));
    book[34] = 1; CHECK(!coop_written_decode(book, sizeof(book), &parsed)); book[34] = 0;
    for (unsigned type = 0; type < 256; type++) {
        book[32] = (uint8_t)type;
        CHECK(coop_written_decode(book, sizeof(book), &parsed) == (type == 2 || type == 5));
    }
    w.type = 5; CHECK(coop_written_encode(&w, book));
    w.revision = 0; CHECK(!coop_written_encode(&w, book)); w.revision = 4;
    w.number = INT32_MAX; CHECK(!coop_written_encode(&w, book)); w.number = 1;
    memset(w.reader, 0, sizeof(w.reader)); CHECK(!coop_written_encode(&w, book));
    puts("PASS: bounded maintained views, cast/cancel/item-use schemas, personal reading views and malformed records");
    return 0;
}
