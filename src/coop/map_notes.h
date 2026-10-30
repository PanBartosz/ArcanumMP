#ifndef ARCMP_MAP_NOTES_H
#define ARCMP_MAP_NOTES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { COOP_NOTES_MAX = 1024, COOP_NOTES_PER_MAP = 199, COOP_NOTE_TEXT = 250,
    COOP_NOTE_RECORD_BYTES = 276, COOP_NOTES_LIMIT = 20 + COOP_NOTES_MAX * COOP_NOTE_RECORD_BYTES,
    COOP_NOTE_REQUEST_BYTES = 304, COOP_NOTE_ID_LIMIT = 0x0fffffff };
typedef struct CoopMapNote { uint32_t id, revision, context, x, y; uint8_t type; char text[COOP_NOTE_TEXT]; } CoopMapNote;
typedef struct CoopMapNotes { uint32_t serial, revision, count; CoopMapNote records[COOP_NOTES_MAX]; } CoopMapNotes;
typedef enum CoopNoteOperation { COOP_NOTE_ADD, COOP_NOTE_EDIT, COOP_NOTE_REMOVE } CoopNoteOperation;
typedef enum CoopNoteStatus { COOP_NOTE_OK, COOP_NOTE_UNAVAILABLE, COOP_NOTE_OWNER, COOP_NOTE_CHANGED, COOP_NOTE_LOCATION, COOP_NOTE_FULL } CoopNoteStatus;
typedef struct CoopNoteRequest {
    uint32_t epoch, sequence, map; uint8_t actor[16];
    CoopNoteOperation operation; uint32_t expected; CoopMapNote note;
} CoopNoteRequest;
bool coop_map_note_valid(const CoopMapNote* note);
const CoopMapNote* coop_map_note_find(const CoopMapNotes* notes, uint32_t id);
CoopNoteStatus coop_map_notes_apply(CoopMapNotes* notes, const CoopNoteRequest* request);
CoopNoteStatus coop_map_notes_mark(CoopMapNotes* notes, const CoopMapNote* note);
uint8_t* coop_map_notes_encode(const CoopMapNotes* notes, size_t* size);
bool coop_map_notes_decode(CoopMapNotes* notes, const void* bytes, size_t size);
bool coop_note_request_encode(const CoopNoteRequest* request, uint8_t bytes[COOP_NOTE_REQUEST_BYTES]);
bool coop_note_request_decode(CoopNoteRequest* request, const void* bytes, size_t size);
#endif
