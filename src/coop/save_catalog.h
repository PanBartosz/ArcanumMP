#ifndef ARCMP_SAVE_CATALOG_H
#define ARCMP_SAVE_CATALOG_H
#include "coop/save_slots.h"
enum { COOP_SAVE_QUERY = 70, COOP_SAVE_PAGE = 71, COOP_SAVE_PAGE_BYTES = 380 };
typedef struct CoopSavePage {
    uint32_t serial, page;
    unsigned count;
    CoopSaveEntry entries[COOP_SAVE_PAGE_ROWS];
} CoopSavePage;
typedef struct CoopSaveCatalog CoopSaveCatalog;
typedef void (*CoopSaveRead)(void* context, unsigned slot, CoopSaveEntry* entry);
CoopSaveCatalog* coop_save_catalog_create(CoopSession* session, CoopSaveRead read, void* context);
void coop_save_catalog_destroy(CoopSaveCatalog* catalog);
bool coop_save_catalog_request(CoopSaveCatalog* catalog, unsigned page);
void coop_save_catalog_cancel(CoopSaveCatalog* catalog);
bool coop_save_catalog_message(CoopSaveCatalog* catalog, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
void coop_save_catalog_poll(CoopSaveCatalog* catalog, uint64_t now, bool serving);
const CoopSavePage* coop_save_catalog_current(const CoopSaveCatalog* catalog);
bool coop_save_catalog_pending(const CoopSaveCatalog* catalog);
bool coop_save_page_encode(const CoopSavePage* page, uint8_t bytes[COOP_SAVE_PAGE_BYTES]);
bool coop_save_page_decode(const void* bytes, size_t size, CoopSavePage* page);
#endif
