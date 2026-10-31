#include "coop/bootstrap.h"
#include "coop/journal.h"
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
static CoopDialogueView fixture(void)
{
    CoopDialogueView v = { .conversation = 1, .revision = 1, .active = true, .chooser = 2, .speech = -1, .option_count = 1 };
    v.pc_guid[0] = 1;
    v.npc.bytes[0] = 2;
    v.npc.bytes[1] = 2;
    strcpy(v.chooser_name, "Traveller");
    strcpy(v.reply, "A native quest clue.\nRemember this after travelling.");
    strcpy(v.options[0], "Tell me more.");
    v.text_refs[0]=(CoopTextRef){.path="dlg/01324virgil.dlg",.key=1,.variant=1};
    return v;
}
int main(void)
{
    CoopJournal *j = coop_journal_create(), *decoded = coop_journal_create();
    CHECK(j && decoded);
    CoopDialogueView v = fixture(), out;
    char speaker[COOP_JOURNAL_SPEAKER];
    CHECK(!coop_journal_append(j, &v, "") && !coop_journal_append(j, &v, "bad\nname"));
    CHECK(coop_journal_append(j, &v, "Gilbert Bates"));
    /* Old journals retain their bytes and do not grow on load. */
    size_t initial_size;
    uint8_t* initial=coop_journal_encode(j,&initial_size);
    CHECK(initial);
    size_t metadata=(COOP_DIALOGUE_OPTIONS+2)*(COOP_TEXT_PATH+5);
    size_t page_at=12+4+strlen("Gilbert Bates")+1;
    size_t old_size=initial_size-metadata;
    uint8_t* old=malloc(old_size);CHECK(old);
    memcpy(old,initial,page_at+97);memcpy(old+page_at,"ACD1",4);
    memcpy(old+page_at+97,initial+page_at+97+metadata,initial_size-page_at-97-metadata);
    coop_object_write32(old+8,coop_object_read32(initial+8)-(uint32_t)metadata);
    coop_object_write32(old+12,coop_object_read32(initial+12)-(uint32_t)metadata);
    coop_object_write32(old+old_size-4,coop_snapshot_crc(old,old_size-4));
    CHECK(coop_journal_decode(decoded,old,old_size));
    size_t retained_size;uint8_t* retained=coop_journal_encode(decoded,&retained_size);
    CHECK(retained&&retained_size==old_size&&!memcmp(retained,old,old_size));
    free(initial);free(old);free(retained);
    v.revision++;
    v.active = false;
    v.option_count = 0;
    v.mode = COOP_DIALOGUE_FLOAT;
    strcpy(v.selected, "Gilbert Bates");
    CHECK(coop_journal_append(j, &v, "Gilbert Bates"));
    v.mode = 0;
    CHECK(coop_journal_append(j, &v, "Gilbert Bates"));
    size_t size;
    uint8_t* bytes = coop_journal_encode(j, &size);
    CHECK(bytes && coop_journal_decode(decoded, bytes, size) && coop_journal_count(decoded) == 3);
    CHECK(coop_journal_page(decoded, 1, &out, speaker) && out.mode == COOP_DIALOGUE_FLOAT && !strcmp(speaker, "Gilbert Bates"));
    CHECK(out.text_refs[0].key==1&&!strcmp(out.text_refs[0].path,v.text_refs[0].path));
    CHECK(!coop_journal_page(decoded, 3, &out, speaker));
    CHECK(!coop_journal_decode(decoded, bytes, size - 1) && coop_journal_count(decoded) == 3);
    uint32_t random = 41;
    for (unsigned i = 0; i < 10000; i++) {
        random = random * 1664525u + 1013904223u;
        size_t at = random % (size - 4);
        uint8_t original = bytes[at];
        bytes[at] ^= (uint8_t)(random >> 24);
        coop_object_write32(bytes + size - 4, coop_snapshot_crc(bytes, size - 4));
        if (coop_journal_decode(decoded, bytes, size)) {
            size_t n;
            uint8_t* canonical = coop_journal_encode(decoded, &n);
            CHECK(canonical && n == size && !memcmp(bytes, canonical, size));
            free(canonical);
        }
        bytes[at] = original;
    }
    free(bytes);
    for (unsigned i = 0; i <= COOP_JOURNAL_PAGES; i++) {
        v.revision = i + 10;
        CHECK(coop_journal_append(j, &v, "Gilbert Bates"));
    }
    CHECK(coop_journal_count(j) == COOP_JOURNAL_PAGES);
    CHECK(coop_journal_page(j, 0, &out, speaker) && out.revision == 11);
    memset(v.reply, 'R', sizeof(v.reply) - 1);
    memset(v.selected, 'S', sizeof(v.selected) - 1);
    v.active = true;
    v.option_count = 5;
    for (unsigned i = 0; i < 5; i++)
        memset(v.options[i], 'A' + i, sizeof(v.options[i]) - 1);
    for (unsigned i = 0; i < 1300; i++)
        CHECK(coop_journal_append(j, &v, "Gilbert Bates"));
    bytes = coop_journal_encode(j, &size);
    CHECK(bytes && size <= COOP_JOURNAL_BYTES && coop_journal_count(j) < COOP_JOURNAL_PAGES);
    CHECK(coop_journal_decode(decoded, bytes, size));
    free(bytes);
    coop_journal_destroy(j);
    coop_journal_destroy(decoded);
    puts("PASS: saved conversation journal, atomic malformed/fuzz decode, page and byte rollover, native text and floating hints");
    return 0;
}
