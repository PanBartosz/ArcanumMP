#include "coop/engine_journal.h"
#include "coop/campaign.h"
#include "coop/dialogue_ui.h"
#include <stdlib.h>
#include <tig/tig.h>
static CoopJournal* journal;
static const char path[] = "Save\\Current\\arcmp-conversations.bin";
CoopJournal* coop_engine_journal(void)
{
    if (!journal) journal = coop_journal_create();
    return journal;
}
void coop_engine_journal_reset(void)
{
    coop_dialogue_ui_reset();
    coop_journal_destroy(journal);
    journal = NULL;
}
bool coop_engine_journal_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(path, NULL) || tig_file_remove(path) == 0;
    size_t size;
    uint8_t* bytes = coop_journal_encode(coop_engine_journal(), &size);
    if (!bytes) return false;
    TigFile* file = tig_file_fopen(path, "wb");
    bool good = file && tig_file_fwrite(bytes, 1, size, file) == size;
    if (file && tig_file_fclose(file)) good = false;
    free(bytes);
    return good;
}
bool coop_engine_journal_load(void)
{
    coop_engine_journal_reset();
    if (!coop_campaign_active() || !tig_file_exists(path, NULL)) return true;
    TigFile* file = tig_file_fopen(path, "rb");
    if (!file) return false;
    int length = tig_file_filelength(file);
    uint8_t* bytes = length >= 16 && length <= COOP_JOURNAL_BYTES ? malloc((size_t)length) : NULL;
    bool good = bytes && tig_file_fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (tig_file_fclose(file)) good = false;
    good = good && coop_journal_decode(coop_engine_journal(), bytes, (size_t)length);
    free(bytes);
    return good;
}
