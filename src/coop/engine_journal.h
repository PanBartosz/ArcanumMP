#ifndef ARCMP_ENGINE_JOURNAL_H
#define ARCMP_ENGINE_JOURNAL_H
#include "coop/journal.h"
CoopJournal* coop_engine_journal(void);
void coop_engine_journal_reset(void);
bool coop_engine_journal_save(void);
bool coop_engine_journal_load(void);
#endif
