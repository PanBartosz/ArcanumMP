#include "coop/journal.h"
#include "coop/bootstrap.h"
#include <stdlib.h>
#include <string.h>
typedef struct JournalPage {
    uint8_t* bytes;
    size_t size;
} JournalPage;
struct CoopJournal {
    JournalPage pages[COOP_JOURNAL_PAGES];
    unsigned count;
    size_t bytes;
};
CoopJournal* coop_journal_create(void) { return calloc(1, sizeof(CoopJournal)); }
void coop_journal_destroy(CoopJournal* j)
{
    if (!j) return;
    for (unsigned i = 0; i < j->count; i++)
        free(j->pages[i].bytes);
    free(j);
}
unsigned coop_journal_count(const CoopJournal* j) { return j ? j->count : 0; }
static bool speaker_valid(const char* s)
{
    if (!s || !*s) return false;
    for (unsigned i = 0; i < COOP_JOURNAL_SPEAKER; i++) {
        if (!s[i]) return true;
        if ((unsigned char)s[i] < 32) return false;
    }
    return false;
}
bool coop_journal_append(CoopJournal* j, const CoopDialogueView* v, const char* speaker)
{
    if (!j || !speaker_valid(speaker)) return false;
    size_t length;
    uint8_t* page = coop_dialogue_view_encode(v, &length);
    if (!page) return false;
    size_t name = strlen(speaker) + 1, size = 4 + name + length;
    uint8_t* bytes = malloc(size);
    if (!bytes) {
        free(page);
        return false;
    }
    coop_object_write32(bytes, (uint32_t)size);
    memcpy(bytes + 4, speaker, name);
    memcpy(bytes + 4 + name, page, length);
    free(page);
    while (j->count && (j->count == COOP_JOURNAL_PAGES || j->bytes + size + 16 > COOP_JOURNAL_BYTES)) {
        j->bytes -= j->pages[0].size;
        free(j->pages[0].bytes);
        memmove(j->pages, j->pages + 1, --j->count * sizeof(*j->pages));
    }
    j->pages[j->count++] = (JournalPage) { bytes, size };
    j->bytes += size;
    return true;
}
bool coop_journal_page(const CoopJournal* j, unsigned i, CoopDialogueView* view, char speaker[COOP_JOURNAL_SPEAKER])
{
    if (!j || i >= j->count || !view || !speaker) return false;
    const JournalPage* p = &j->pages[i];
    const char* name = (const char*)p->bytes + 4;
    size_t offset = 4 + strlen(name) + 1;
    if (!coop_dialogue_view_decode(p->bytes + offset, p->size - offset, view)) return false;
    memset(speaker, 0, COOP_JOURNAL_SPEAKER);
    memcpy(speaker, name, offset - 4);
    return true;
}
uint8_t* coop_journal_encode(const CoopJournal* j, size_t* size)
{
    if (!j || !size) return NULL;
    *size = j->bytes + 16;
    uint8_t* bytes = malloc(*size);
    if (!bytes) return NULL;
    memcpy(bytes, "ACJ1", 4);
    coop_object_write32(bytes + 4, j->count);
    coop_object_write32(bytes + 8, (uint32_t)j->bytes);
    size_t offset = 12;
    for (unsigned i = 0; i < j->count; i++) {
        memcpy(bytes + offset, j->pages[i].bytes, j->pages[i].size);
        offset += j->pages[i].size;
    }
    coop_object_write32(bytes + offset, coop_snapshot_crc(bytes, offset));
    return bytes;
}
bool coop_journal_decode(CoopJournal* j, const void* data, size_t size)
{
    const uint8_t* bytes = data;
    if (!j || !bytes || size < 16 || size > COOP_JOURNAL_BYTES || memcmp(bytes, "ACJ1", 4)
        || coop_object_read32(bytes + 4) > COOP_JOURNAL_PAGES || coop_object_read32(bytes + 8) != size - 16
        || coop_object_read32(bytes + size - 4) != coop_snapshot_crc(bytes, size - 4)) return false;
    CoopJournal* next = coop_journal_create();
    if (!next) return false;
    unsigned count = coop_object_read32(bytes + 4);
    size_t offset = 12;
    bool good = true;
    for (unsigned i = 0; good && i < count; i++) {
        if (size - 4 - offset < 4) {
            good = false;
            break;
        }
        size_t length = coop_object_read32(bytes + offset);
        if (length < 107 || length > 4 + COOP_JOURNAL_SPEAKER + COOP_DIALOGUE_LIMIT || length > size - 4 - offset) {
            good = false;
            break;
        }
        const char* speaker = (const char*)bytes + offset + 4;
        const char* end = memchr(speaker, 0, length - 4 < COOP_JOURNAL_SPEAKER ? length - 4 : COOP_JOURNAL_SPEAKER);
        CoopDialogueView view;
        size_t prefix = end ? 5 + (size_t)(end - speaker) : length;
        good = end && speaker_valid(speaker) && prefix < length
            && coop_dialogue_view_decode(bytes + offset + prefix, length - prefix, &view)
            && coop_journal_append(next, &view, speaker);
        offset += length;
    }
    good &= offset == size - 4 && next->count == count;
    if (good) {
        CoopJournal swap = *j;
        *j = *next;
        *next = swap;
    }
    coop_journal_destroy(next);
    return good;
}
