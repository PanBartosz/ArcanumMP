#ifndef ARCMP_JOURNAL_H
#define ARCMP_JOURNAL_H
#include "coop/dialogue.h"
/* A bounded campaign transcript. Oldest pages roll off at either limit. */
enum { COOP_JOURNAL_PAGES = 8192,
    COOP_JOURNAL_BYTES = 8 * 1024 * 1024,
    COOP_JOURNAL_SPEAKER = 128 };
typedef struct CoopJournal CoopJournal;
CoopJournal* coop_journal_create(void);
void coop_journal_destroy(CoopJournal* journal);
unsigned coop_journal_count(const CoopJournal* journal);
bool coop_journal_append(CoopJournal* journal, const CoopDialogueView* view, const char* speaker);
bool coop_journal_page(const CoopJournal* journal, unsigned index, CoopDialogueView* view, char speaker[COOP_JOURNAL_SPEAKER]);
uint8_t* coop_journal_encode(const CoopJournal* journal, size_t* size);
/* Decode is atomic: malformed input leaves the existing journal intact. */
bool coop_journal_decode(CoopJournal* journal, const void* bytes, size_t size);
#endif
