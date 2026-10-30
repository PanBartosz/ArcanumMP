#include "coop/save_catalog.h"
#include "coop/bootstrap.h"
#include "coop/object_state.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
struct CoopSaveCatalog {
    CoopSession* session;
    CoopSaveRead read;
    void* context;
    bool host, pending;
    uint32_t serial, requested;
    uint64_t last_query;
    struct { bool pending; uint32_t serial, page; uint64_t sent; } requests[COOP_MAX_PLAYERS];
    CoopSavePage current;
};
static unsigned rows(unsigned page)
{
    unsigned left = COOP_SAVE_TOTAL - page * COOP_SAVE_PAGE_ROWS;
    return left < COOP_SAVE_PAGE_ROWS ? left : COOP_SAVE_PAGE_ROWS;
}
static bool entry_valid(const CoopSaveEntry* entry)
{
    if (entry->slot >= COOP_SAVE_TOTAL || entry->status < COOP_CHECKPOINT_OK || entry->status > COOP_CHECKPOINT_IO || !coop_save_label_valid(entry->info.description)) return false;
    bool present = entry->status == COOP_CHECKPOINT_OK || entry->status == COOP_CHECKPOINT_CONTENT || entry->status == COOP_CHECKPOINT_OWNER;
    if (!present) {
        static const uint8_t zero[64];
        return !entry->info.members && !entry->info.epoch && !entry->info.map && !entry->fingerprint && !entry->payload_size && !entry->modified
            && !memcmp(entry->info.owners, zero, 64) && !memcmp(entry->info.description, zero, 64);
    }
    if (!entry->info.epoch || entry->info.map > INT32_MAX || !entry->info.members || (entry->info.members & ~15u)
        || entry->payload_size < 16 || entry->payload_size > COOP_SNAPSHOT_LIMIT) return false;
    return true;
}
bool coop_save_page_encode(const CoopSavePage* page, uint8_t bytes[COOP_SAVE_PAGE_BYTES])
{
    if (!page || !bytes || !page->serial || page->page >= COOP_SAVE_PAGES || page->count != rows(page->page)) return false;
    memset(bytes, 0, COOP_SAVE_PAGE_BYTES);
    coop_object_write32(bytes, page->serial); coop_object_write32(bytes + 4, page->page); bytes[8] = (uint8_t)page->count;
    for (unsigned i = 0; i < page->count; i++) {
        const CoopSaveEntry* entry = &page->entries[i];
        if (entry->slot != page->page * COOP_SAVE_PAGE_ROWS + i || !entry_valid(entry)) return false;
        uint8_t* p = bytes + 12 + i * 92;
        p[0] = (uint8_t)entry->slot; p[1] = (uint8_t)entry->status; p[2] = entry->info.members;
        coop_object_write32(p + 4, entry->info.epoch); coop_object_write32(p + 8, entry->info.map);
        coop_object_write32(p + 12, entry->payload_size); coop_object_write32(p + 16, entry->fingerprint);
        coop_object_write64(p + 20, entry->modified); memcpy(p + 28, entry->info.description, 64);
    }
    return true;
}
bool coop_save_page_decode(const void* data, size_t size, CoopSavePage* result)
{
    if (!data || !result || size != COOP_SAVE_PAGE_BYTES) return false;
    const uint8_t* bytes = data;
    CoopSavePage page = { .serial = coop_object_read32(bytes), .page = coop_object_read32(bytes + 4), .count = bytes[8] };
    if (!page.serial || page.page >= COOP_SAVE_PAGES || page.count != rows(page.page) || bytes[9] || bytes[10] || bytes[11]) return false;
    for (unsigned i = 0; i < COOP_SAVE_PAGE_ROWS; i++) {
        const uint8_t* p = bytes + 12 + i * 92;
        if (i >= page.count) {
            for (unsigned n = 0; n < 92; n++) if (p[n]) return false;
            continue;
        }
        CoopSaveEntry* entry = &page.entries[i];
        entry->slot = p[0]; entry->status = (CoopCheckpointStatus)p[1]; entry->info.members = p[2];
        entry->info.epoch = coop_object_read32(p + 4); entry->info.map = coop_object_read32(p + 8);
        entry->payload_size = coop_object_read32(p + 12); entry->fingerprint = coop_object_read32(p + 16);
        entry->modified = coop_object_read64(p + 20); memcpy(entry->info.description, p + 28, 64);
        if (p[3] || entry->slot != page.page * COOP_SAVE_PAGE_ROWS + i || !entry_valid(entry)) return false;
    }
    *result = page;
    return true;
}
CoopSaveCatalog* coop_save_catalog_create(CoopSession* session, CoopSaveRead read, void* context)
{
    if (!session || !read) return NULL;
    CoopSaveCatalog* catalog = calloc(1, sizeof(*catalog));
    if (catalog) { catalog->session = session; catalog->read = read; catalog->context = context; catalog->host = coop_session_is_host(session); }
    return catalog;
}
void coop_save_catalog_destroy(CoopSaveCatalog* catalog) { free(catalog); }
bool coop_save_catalog_request(CoopSaveCatalog* catalog, unsigned page)
{
    if (!catalog || page >= COOP_SAVE_PAGES || catalog->serial == UINT32_MAX) return false;
    catalog->serial++; catalog->requested = page; catalog->pending = true; catalog->last_query = 0;
    return true;
}
void coop_save_catalog_cancel(CoopSaveCatalog* catalog)
{
    if (catalog) { catalog->pending = false; catalog->requests[0].pending = false; }
}
bool coop_save_catalog_message(CoopSaveCatalog* catalog, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    if (!catalog || !bytes) return false;
    if (catalog->host) {
        if (type != COOP_SAVE_QUERY || size != 8 || !sender || sender >= COOP_MAX_PLAYERS || !coop_session_players(catalog->session)[sender].connected) return false;
        uint32_t serial = coop_object_read32(bytes), page = coop_object_read32(bytes + 4);
        if (!serial || page >= COOP_SAVE_PAGES) return false;
        catalog->requests[sender].serial = serial; catalog->requests[sender].page = page; catalog->requests[sender].pending = true;
        return true;
    }
    CoopSavePage page;
    if (sender || type != COOP_SAVE_PAGE || !coop_save_page_decode(bytes, size, &page)) return false;
    if (page.serial != catalog->serial || page.page != catalog->requested) return true;
    catalog->current = page; catalog->pending = false;
    return true;
}
void coop_save_catalog_poll(CoopSaveCatalog* catalog, uint64_t now, bool serving)
{
    if (!catalog || coop_session_phase(catalog->session) != COOP_LOBBY) return;
    if (catalog->pending && (!catalog->last_query || now - catalog->last_query >= 750)) {
        if (catalog->host) {
            catalog->requests[0].serial = catalog->serial; catalog->requests[0].page = catalog->requested; catalog->requests[0].pending = true;
            catalog->last_query = now;
        } else {
            uint8_t bytes[8]; coop_object_write32(bytes, catalog->serial); coop_object_write32(bytes + 4, catalog->requested);
            if (coop_session_send(catalog->session, 0, COOP_SAVE_QUERY, bytes, sizeof(bytes))) catalog->last_query = now;
        }
    }
    if (!catalog->host || !serving) return;
    for (unsigned peer = 0; peer < COOP_MAX_PLAYERS; peer++) {
        if (!catalog->requests[peer].pending || now - catalog->requests[peer].sent < 250) continue;
        CoopSavePage page = { .serial = catalog->requests[peer].serial, .page = catalog->requests[peer].page };
        page.count = rows(page.page);
        for (unsigned i = 0; i < page.count; i++) catalog->read(catalog->context, page.page * COOP_SAVE_PAGE_ROWS + i, &page.entries[i]);
        uint8_t bytes[COOP_SAVE_PAGE_BYTES];
        if (!coop_save_page_encode(&page, bytes)) continue;
        if (!peer) { catalog->current = page; catalog->pending = false; }
        else if (!coop_session_players(catalog->session)[peer].connected || !coop_session_send(catalog->session, peer, COOP_SAVE_PAGE, bytes, sizeof(bytes))) continue;
        catalog->requests[peer].sent = now; catalog->requests[peer].pending = false;
    }
}
const CoopSavePage* coop_save_catalog_current(const CoopSaveCatalog* catalog) { return catalog && catalog->current.serial ? &catalog->current : NULL; }
bool coop_save_catalog_pending(const CoopSaveCatalog* catalog) { return catalog && catalog->pending; }
