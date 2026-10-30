#ifndef ARCMP_NOTES_H
#define ARCMP_NOTES_H
#include "coop/map_notes.h"
#include "coop/world_net.h"
enum { COOP_NOTE_REQUEST=72, COOP_NOTE_RESULT=73 };
typedef struct CoopNotes CoopNotes;
typedef int64_t (*CoopNoteActor)(void*,uint8_t,const uint8_t*);
CoopNotes* coop_notes_create(CoopSession*,CoopWorldNetwork*,uint32_t,uint64_t,CoopNoteActor,void*);
void coop_notes_destroy(CoopNotes*);
bool coop_notes_message(CoopNotes*,uint8_t,uint8_t,const uint8_t*,size_t);
bool coop_notes_submit(CoopNotes*,const CoopNoteRequest*);
bool coop_notes_poll(CoopNotes*,uint64_t,bool);
bool coop_notes_settled(const CoopNotes*);
/* Last receipt whose authoritative world update has also been applied. */
uint32_t coop_notes_last_result(const CoopNotes*, unsigned*);
#endif
