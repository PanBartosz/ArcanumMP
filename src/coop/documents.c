#include "coop/documents.h"
#include "coop/bootstrap.h"
#include "coop/object_state.h"
#include <stdlib.h>
#include <string.h>
bool coop_document_valid(CoopDocument d)
{
    return d.type <= 6 && d.type != 5 && d.number <= INT32_MAX - 10000u && d.variant <= (d.type == 2 ? 1 : 0);
}
static int compare(CoopDocument a, CoopDocument b)
{
    if (a.type != b.type) return a.type < b.type ? -1 : 1;
    if (a.variant != b.variant) return a.variant < b.variant ? -1 : 1;
    return a.number == b.number ? 0 : a.number < b.number ? -1
                                                          : 1;
}
bool coop_documents_add(CoopDocuments* documents, CoopDocument d)
{
    if (!documents || documents->count > COOP_DOCUMENTS_MAX || !coop_document_valid(d)) return false;
    unsigned at = 0;
    while (at < documents->count && compare(documents->records[at], d) < 0)
        at++;
    if (at < documents->count && !compare(documents->records[at], d)) return true;
    if (documents->count == COOP_DOCUMENTS_MAX) return false;
    memmove(documents->records + at + 1, documents->records + at, (documents->count - at) * sizeof(d));
    documents->records[at] = d;
    documents->count++;
    return true;
}
uint8_t* coop_documents_encode(const CoopDocuments* documents, size_t* size)
{
    if (!documents || !size || documents->count > COOP_DOCUMENTS_MAX) return NULL;
    size_t length = 12 + 8 * documents->count;
    uint8_t* bytes = calloc(1, length);
    if (!bytes) return NULL;
    memcpy(bytes, "ACD1", 4);
    coop_object_write32(bytes + 4, documents->count);
    for (unsigned i = 0; i < documents->count; i++) {
        CoopDocument d = documents->records[i];
        if (!coop_document_valid(d) || (i && compare(documents->records[i - 1], d) >= 0)) {
            free(bytes);
            return NULL;
        }
        bytes[8 + 8 * i] = d.type;
        bytes[9 + 8 * i] = d.variant;
        coop_object_write32(bytes + 12 + 8 * i, d.number);
    }
    coop_object_write32(bytes + length - 4, coop_snapshot_crc(bytes, length - 4));
    *size = length;
    return bytes;
}
bool coop_documents_decode(CoopDocuments* documents, const void* data, size_t size)
{
    const uint8_t* bytes = data;
    if (!documents || !bytes || size < 12 || size > COOP_DOCUMENTS_LIMIT || memcmp(bytes, "ACD1", 4)) return false;
    unsigned count = coop_object_read32(bytes + 4);
    if (count > COOP_DOCUMENTS_MAX || size != 12 + 8 * count || coop_object_read32(bytes + size - 4) != coop_snapshot_crc(bytes, size - 4)) return false;
    CoopDocuments* next = calloc(1, sizeof(*next));
    if (!next) return false;
    bool good = true;
    for (unsigned i = 0; good && i < count; i++) {
        CoopDocument d = { .type = bytes[8 + 8 * i], .variant = bytes[9 + 8 * i], .number = coop_object_read32(bytes + 12 + 8 * i) };
        good = !bytes[10 + 8 * i] && !bytes[11 + 8 * i] && coop_document_valid(d) && (!i || compare(next->records[i - 1], d) < 0);
        next->records[i] = d;
    }
    if (good) {
        next->count = count;
        *documents = *next;
    }
    free(next);
    return good;
}
