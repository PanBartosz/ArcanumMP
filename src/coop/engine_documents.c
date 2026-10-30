#include "coop/engine_documents.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "ui/written_ui.h"
#include <stdlib.h>
#include <tig/tig.h>
static CoopDocuments documents;
static const char path[] = "Save\\Current\\arcmp-documents.bin";
const CoopDocuments* coop_engine_documents(void) { return &documents; }
void coop_engine_documents_reset(void)
{
    coop_documents_ui_close();
    documents = (CoopDocuments) { 0 };
}
bool coop_engine_documents_record(unsigned type, int number, bool variant)
{
    if (!coop_campaign_active() || type == 5) return true;
    if (!coop_authority_can_simulate() || type > 6 || number < 0) return false;
    CoopDocument d = { .type = (uint8_t)type, .number = (uint32_t)number, .variant = variant };
    if (!coop_document_valid(d) || !written_ui_coop_valid(d.type, (int)d.number)) return false;
    bool good = coop_documents_add(&documents, d);
    if (good) coop_documents_ui_refresh();
    return good;
}
bool coop_engine_documents_matches(const CoopDocuments* candidate)
{
    for (unsigned i = 0; i < candidate->count; i++)
        if (!written_ui_coop_valid(candidate->records[i].type, (int)candidate->records[i].number)) return false;
    return true;
}
void coop_engine_documents_install(const CoopDocuments* candidate)
{
    documents = *candidate;
    coop_documents_ui_refresh();
}
bool coop_engine_documents_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(path, NULL) || tig_file_remove(path) == 0;
    size_t size;
    uint8_t* bytes = coop_documents_encode(&documents, &size);
    if (!bytes) return false;
    TigFile* file = tig_file_fopen(path, "wb");
    bool good = file && tig_file_fwrite(bytes, 1, size, file) == size;
    if (file && tig_file_fclose(file)) good = false;
    free(bytes);
    return good;
}
bool coop_engine_documents_load(void)
{
    coop_engine_documents_reset();
    if (!coop_campaign_active() || !tig_file_exists(path, NULL)) return true;
    TigFile* file = tig_file_fopen(path, "rb");
    if (!file) return false;
    int size = tig_file_filelength(file);
    uint8_t* bytes = size >= 12 && size <= COOP_DOCUMENTS_LIMIT ? malloc((size_t)size) : NULL;
    bool good = bytes && tig_file_fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (tig_file_fclose(file)) good = false;
    good = good && coop_documents_decode(&documents, bytes, (size_t)size) && coop_engine_documents_matches(&documents);
    free(bytes);
    return good;
}
