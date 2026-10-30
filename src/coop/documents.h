#ifndef ARCMP_DOCUMENTS_H
#define ARCMP_DOCUMENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { COOP_DOCUMENTS_MAX = 2048,
    COOP_DOCUMENTS_LIMIT = 12 + 8 * COOP_DOCUMENTS_MAX };
typedef struct CoopDocument {
    uint32_t number;
    uint8_t type, variant;
} CoopDocument;
typedef struct CoopDocuments {
    uint32_t count;
    CoopDocument records[COOP_DOCUMENTS_MAX];
} CoopDocuments;
bool coop_document_valid(CoopDocument document);
bool coop_documents_add(CoopDocuments* documents, CoopDocument document);
uint8_t* coop_documents_encode(const CoopDocuments* documents, size_t* size);
bool coop_documents_decode(CoopDocuments* documents, const void* bytes, size_t size);
#endif
