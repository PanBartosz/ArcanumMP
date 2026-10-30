#ifndef ARCMP_ENGINE_DOCUMENTS_H
#define ARCMP_ENGINE_DOCUMENTS_H
#include "coop/documents.h"
const CoopDocuments* coop_engine_documents(void);
void coop_engine_documents_reset(void);
bool coop_engine_documents_record(unsigned type, int number, bool variant);
bool coop_engine_documents_save(void);
bool coop_engine_documents_load(void);
bool coop_engine_documents_matches(const CoopDocuments* documents);
void coop_engine_documents_install(const CoopDocuments* documents);
bool coop_documents_ui_open(void);
bool coop_documents_ui_active(void);
void coop_documents_ui_close(void);
void coop_documents_ui_refresh(void);
#endif
