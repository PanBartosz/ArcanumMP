#include "coop/bootstrap.h"
#include "coop/documents.h"
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
    CoopDocuments* documents = calloc(1, sizeof(*documents));
    CoopDocuments* decoded = calloc(1, sizeof(*decoded));
    CHECK(documents && decoded);
    size_t size;
    uint8_t* bytes = coop_documents_encode(documents, &size);
    CHECK(bytes && size == 12 && coop_documents_decode(decoded, bytes, size));
    free(bytes);
    CHECK(coop_documents_add(documents, (CoopDocument) { .type = 2, .number = 1280, .variant = 1 }));
    CHECK(coop_documents_add(documents, (CoopDocument) { .type = 0, .number = 100 }));
    CHECK(coop_documents_add(documents, (CoopDocument) { .type = 0, .number = 100 }));
    CHECK(documents->count == 2 && documents->records[0].type == 0);
    CHECK(!coop_documents_add(documents, (CoopDocument) { .type = 5, .number = 1 }));
    CHECK(!coop_documents_add(documents, (CoopDocument) { .type = 0, .variant = 1 }));
    CHECK(!coop_documents_add(documents, (CoopDocument) { .type = 7 }));
    CHECK(!coop_documents_add(documents, (CoopDocument) { .number = INT32_MAX }));
    bytes = coop_documents_encode(documents, &size);
    CHECK(bytes && size == 28);
    CHECK(coop_documents_decode(decoded, bytes, size) && decoded->count == 2);
    for (size_t i = 0; i < size; i++) {
        CHECK(!coop_documents_decode(decoded, bytes, i));
        CHECK(decoded->count == 2 && decoded->records[0].number == 100);
    }
    uint32_t rng = 34;
    for (unsigned i = 0; i < 10000; i++) {
        uint8_t mutated[29];
        memcpy(mutated, bytes, size);
        rng = rng * 1664525u + 1013904223u;
        mutated[rng % (size - 4)] ^= (uint8_t)(rng >> 24);
        coop_object_write32(mutated + size - 4, coop_snapshot_crc(mutated, size - 4));
        if (coop_documents_decode(decoded, mutated, size)) {
            size_t n;
            uint8_t* canonical = coop_documents_encode(decoded, &n);
            CHECK(canonical && n == size && !memcmp(canonical, mutated, size));
            free(canonical);
        }
    }
    /* Reach structural rejection with a valid checksum. */
    bytes[10] = 1;
    coop_object_write32(bytes + size - 4, coop_snapshot_crc(bytes, size - 4));
    CHECK(!coop_documents_decode(decoded, bytes, size));
    bytes[10] = 0;
    memcpy(bytes + 16, bytes + 8, 8);
    coop_object_write32(bytes + size - 4, coop_snapshot_crc(bytes, size - 4));
    CHECK(!coop_documents_decode(decoded, bytes, size));
    free(bytes);
    *documents = (CoopDocuments) { 0 };
    for (unsigned i = 0; i < COOP_DOCUMENTS_MAX; i++)
        CHECK(coop_documents_add(documents, (CoopDocument) { .type = 1, .number = i }));
    CHECK(!coop_documents_add(documents, (CoopDocument) { .type = 1, .number = COOP_DOCUMENTS_MAX }));
    bytes = coop_documents_encode(documents, &size);
    CHECK(bytes && size == COOP_DOCUMENTS_LIMIT);
    CHECK(coop_documents_decode(decoded, bytes, size) && decoded->count == COOP_DOCUMENTS_MAX);
    free(bytes);
    free(decoded);
    free(documents);
    return 0;
}
