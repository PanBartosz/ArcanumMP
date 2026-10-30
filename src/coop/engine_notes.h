#ifndef ARCMP_ENGINE_NOTES_H
#define ARCMP_ENGINE_NOTES_H
#include "coop/map_notes.h"
const CoopMapNotes* coop_engine_notes(void);
void coop_engine_notes_reset(void);
bool coop_engine_notes_save(void);
bool coop_engine_notes_load(void);
bool coop_engine_notes_matches(const CoopMapNotes* notes);
void coop_engine_notes_install(const CoopMapNotes* notes);
CoopNoteStatus coop_engine_note_apply(int64_t actor, const CoopNoteRequest* request);
/* Migration is enabled only inside the native checkpoint loader. */
bool coop_engine_note_import(uint32_t context, int x, int y, unsigned type, const char* text);
bool coop_engine_note_mark(uint32_t context, int x, int y, const char* text);
#endif
