#include "coop/map_notes.h"
#include "coop/identity.h"
#include "coop/object_state.h"
#include "coop/bootstrap.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static bool text_valid(const char text[COOP_NOTE_TEXT], bool empty)
{
    bool end = false, visible = false;
    for (unsigned i = 0; i < COOP_NOTE_TEXT; i++) {
        unsigned char c = (unsigned char)text[i];
        if (!c) end = true;
        else if (end || c < 32 || c == 127) return false;
        else if (c != ' ') visible = true;
    }
    return end && (empty || visible);
}
bool coop_map_note_valid(const CoopMapNote* n)
{
    return n && n->id && n->id <= COOP_NOTE_ID_LIMIT && n->revision && n->context <= INT32_MAX
        && n->x < 0x1000000 && n->y < 0x1000000 && n->type < 4 && text_valid(n->text, false);
}
const CoopMapNote* coop_map_note_find(const CoopMapNotes* s, uint32_t id)
{
    if (!s || !id) return NULL;
    for (unsigned i = 0; i < s->count; i++) if (s->records[i].id == id) return &s->records[i];
    return NULL;
}
static bool valid(const CoopMapNotes* s)
{
    if (!s || s->count > COOP_NOTES_MAX || s->serial > COOP_NOTE_ID_LIMIT || (!s->revision && (s->serial || s->count))) return false;
    uint32_t previous = 0;
    for (unsigned i = 0; i < s->count; i++) {
        const CoopMapNote* n = &s->records[i];
        if (!coop_map_note_valid(n) || n->id <= previous || n->id > s->serial || n->revision > s->revision) return false;
        unsigned count = 1;
        for (unsigned j = 0; j < i; j++) count += s->records[j].context == n->context;
        if (count > COOP_NOTES_PER_MAP) return false;
        previous = n->id;
    }
    return true;
}
static bool request_valid(const CoopNoteRequest* r)
{
    if (!r || !r->epoch || !r->sequence || r->map > INT32_MAX || !coop_identity_valid(r->actor)
        || r->operation > COOP_NOTE_REMOVE || r->operation < COOP_NOTE_ADD || r->note.revision
        || r->note.context > INT32_MAX || r->note.id > COOP_NOTE_ID_LIMIT) return false;
    if (r->operation == COOP_NOTE_REMOVE) {
        static const char zero[COOP_NOTE_TEXT];
        return r->note.id && r->expected && !r->note.x && !r->note.y && !r->note.type && !memcmp(r->note.text, zero, sizeof(zero));
    }
    CoopMapNote n = r->note; n.id = 1; n.revision = 1;
    return r->note.type < 3 && coop_map_note_valid(&n)
        && (r->operation == COOP_NOTE_ADD ? !r->note.id && !r->expected : r->note.id && r->expected);
}
CoopNoteStatus coop_map_notes_apply(CoopMapNotes* s, const CoopNoteRequest* r)
{
    if (!s || !request_valid(r)) return COOP_NOTE_LOCATION;
    if (s->revision == UINT32_MAX) return COOP_NOTE_FULL;
    CoopMapNote* n = NULL;
    if (r->operation == COOP_NOTE_ADD) {
        if (s->count >= COOP_NOTES_MAX || s->serial == COOP_NOTE_ID_LIMIT) return COOP_NOTE_FULL;
        unsigned count = 0;
        for (unsigned i = 0; i < s->count; i++) count += s->records[i].context == r->note.context;
        if (count >= COOP_NOTES_PER_MAP) return COOP_NOTE_FULL;
        n = &s->records[s->count++]; *n = r->note; n->id = ++s->serial; n->revision = ++s->revision;
        return COOP_NOTE_OK;
    }
    for (unsigned i = 0; i < s->count; i++) if (s->records[i].id == r->note.id) { n = &s->records[i]; break; }
    if (!n || n->revision != r->expected || n->context != r->note.context || n->type == 3) return COOP_NOTE_CHANGED;
    if (r->operation == COOP_NOTE_REMOVE) {
        unsigned index = (unsigned)(n - s->records);
        memmove(n, n + 1, (s->count - index - 1) * sizeof(*n));
        memset(&s->records[--s->count], 0, sizeof(*n)); s->revision++;
    } else { *n = r->note; n->revision = ++s->revision; }
    return COOP_NOTE_OK;
}
CoopNoteStatus coop_map_notes_mark(CoopMapNotes* s, const CoopMapNote* note)
{
    if (!s || !note) return COOP_NOTE_LOCATION;
    CoopMapNote candidate = *note; candidate.id = 1; candidate.revision = 1; candidate.type = 3;
    if (!coop_map_note_valid(&candidate)) return COOP_NOTE_LOCATION;
    unsigned count = 0;
    for (unsigned i = 0; i < s->count; i++) if (s->records[i].context == candidate.context) {
        count++;
        const CoopMapNote* old = &s->records[i];
        if (old->type == 3 && old->x == candidate.x && old->y == candidate.y && !strcmp(old->text, candidate.text)) return COOP_NOTE_OK;
    }
    if (count >= COOP_NOTES_PER_MAP || s->count == COOP_NOTES_MAX || s->serial == COOP_NOTE_ID_LIMIT || s->revision == UINT32_MAX) return COOP_NOTE_FULL;
    candidate.id = ++s->serial; candidate.revision = ++s->revision; s->records[s->count++] = candidate;
    return COOP_NOTE_OK;
}
static void write_note(uint8_t* p, const CoopMapNote* n)
{
    memset(p, 0, COOP_NOTE_RECORD_BYTES);
    coop_object_write32(p,n->id); coop_object_write32(p+4,n->revision); coop_object_write32(p+8,n->context);
    coop_object_write32(p+12,n->x); coop_object_write32(p+16,n->y); p[20]=n->type; memcpy(p+24,n->text,COOP_NOTE_TEXT);
}
static bool read_note(CoopMapNote* n, const uint8_t* p)
{
    if (p[21] || p[22] || p[23] || p[274] || p[275]) return false;
    *n=(CoopMapNote){.id=coop_object_read32(p),.revision=coop_object_read32(p+4),.context=coop_object_read32(p+8),
        .x=coop_object_read32(p+12),.y=coop_object_read32(p+16),.type=p[20]};
    memcpy(n->text,p+24,COOP_NOTE_TEXT); return true;
}
uint8_t* coop_map_notes_encode(const CoopMapNotes* s, size_t* size)
{
    if (!size || !valid(s)) return NULL;
    size_t length = 20 + s->count * COOP_NOTE_RECORD_BYTES;
    uint8_t* p = malloc(length); if (!p) return NULL;
    memcpy(p,"ACN1",4); coop_object_write32(p+4,s->serial); coop_object_write32(p+8,s->revision); coop_object_write32(p+12,s->count);
    for (unsigned i=0;i<s->count;i++) write_note(p+16+i*COOP_NOTE_RECORD_BYTES,&s->records[i]);
    coop_object_write32(p+length-4,coop_snapshot_crc(p,length-4)); *size=length; return p;
}
bool coop_map_notes_decode(CoopMapNotes* out, const void* bytes, size_t size)
{
    const uint8_t* p=bytes;
    if (!out || !p || size<20 || size>COOP_NOTES_LIMIT || memcmp(p,"ACN1",4)
        || coop_object_read32(p+size-4)!=coop_snapshot_crc(p,size-4)) return false;
    uint32_t count=coop_object_read32(p+12);
    if (count>COOP_NOTES_MAX || size!=20+count*COOP_NOTE_RECORD_BYTES) return false;
    CoopMapNotes* s=calloc(1,sizeof(*s)); if (!s) return false;
    s->count=count; s->serial=coop_object_read32(p+4); s->revision=coop_object_read32(p+8);
    bool good=true;
    for (unsigned i=0;good && i<count;i++) good=read_note(&s->records[i],p+16+i*COOP_NOTE_RECORD_BYTES);
    good=good && valid(s); if (good) *out=*s; free(s); return good;
}
bool coop_note_request_encode(const CoopNoteRequest* r, uint8_t p[COOP_NOTE_REQUEST_BYTES])
{
    if (!p || !request_valid(r)) return false;
    memset(p,0,COOP_NOTE_REQUEST_BYTES);
    coop_object_write32(p,r->epoch); coop_object_write32(p+4,r->sequence); coop_object_write32(p+8,r->map); memcpy(p+12,r->actor,16);
    p[28]=(uint8_t)r->operation; p[29]=r->note.type;
    coop_object_write32(p+32,r->note.id); coop_object_write32(p+36,r->expected); coop_object_write32(p+40,r->note.context);
    coop_object_write32(p+44,r->note.x); coop_object_write32(p+48,r->note.y); memcpy(p+52,r->note.text,COOP_NOTE_TEXT); return true;
}
bool coop_note_request_decode(CoopNoteRequest* out, const void* bytes, size_t size)
{
    const uint8_t* p=bytes;
    if (!out || !p || size!=COOP_NOTE_REQUEST_BYTES || p[30] || p[31] || p[302] || p[303]) return false;
    CoopNoteRequest r={.epoch=coop_object_read32(p),.sequence=coop_object_read32(p+4),.map=coop_object_read32(p+8),.operation=(CoopNoteOperation)p[28],
        .expected=coop_object_read32(p+36),.note={.id=coop_object_read32(p+32),.context=coop_object_read32(p+40),.x=coop_object_read32(p+44),.y=coop_object_read32(p+48),.type=p[29]}};
    memcpy(r.actor,p+12,16); memcpy(r.note.text,p+52,COOP_NOTE_TEXT);
    if (!request_valid(&r)) return false;
    *out=r; return true;
}
