#include "coop/map_notes.h"
#include "coop/object_state.h"
#include "coop/bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while (0)
int main(void)
{
    CoopMapNotes* notes=calloc(1,sizeof(*notes)), *loaded=calloc(1,sizeof(*loaded)); CHECK(notes && loaded);
    CoopNoteRequest a={.epoch=1,.sequence=1,.map=3,.operation=COOP_NOTE_ADD,.note={.context=7,.x=123,.y=456,.type=2}};
    a.actor[0]=1;strcpy(a.note.text,"Meet at the chest.");
    uint8_t request[COOP_NOTE_REQUEST_BYTES];CoopNoteRequest parsed;
    CHECK(coop_note_request_encode(&a,request) && coop_note_request_decode(&parsed,request,sizeof(request)));
    CHECK(coop_map_notes_apply(notes,&parsed)==COOP_NOTE_OK && notes->count==1 && notes->records[0].id==1);
    CoopNoteRequest edit=a;edit.operation=COOP_NOTE_EDIT;edit.note.id=1;edit.expected=1;memset(edit.note.text,0,sizeof(edit.note.text));strcpy(edit.note.text,"Door is locked.");
    CoopNoteRequest concurrent=edit;strcpy(concurrent.note.text,"Newer draft from a second player.");
    CHECK(coop_map_notes_apply(notes,&edit)==COOP_NOTE_OK && notes->records[0].revision==2);
    CHECK(coop_map_notes_apply(notes,&concurrent)==COOP_NOTE_CHANGED && !strcmp(notes->records[0].text,"Door is locked."));
    CoopNoteRequest remove={.epoch=1,.sequence=3,.map=3,.operation=COOP_NOTE_REMOVE,.expected=1,.note={.id=1,.context=7}};remove.actor[0]=1;
    CHECK(coop_map_notes_apply(notes,&remove)==COOP_NOTE_CHANGED && notes->count==1);
    remove.expected=2;CHECK(coop_map_notes_apply(notes,&remove)==COOP_NOTE_OK && notes->count==0);
    CHECK(coop_map_notes_apply(notes,&a)==COOP_NOTE_OK && notes->records[0].id==2);
    CHECK(coop_map_notes_apply(notes,&remove)==COOP_NOTE_CHANGED && notes->count==1);
    CoopMapNote marker={.context=8,.x=44,.y=55};strcpy(marker.text,"Original quest marker");
    CHECK(coop_map_notes_mark(notes,&marker)==COOP_NOTE_OK && notes->count==2);
    unsigned revision=notes->revision;CHECK(coop_map_notes_mark(notes,&marker)==COOP_NOTE_OK && notes->count==2 && notes->revision==revision);
    remove.note.id=3;remove.note.context=8;remove.expected=notes->records[1].revision;
    CHECK(coop_map_notes_apply(notes,&remove)==COOP_NOTE_CHANGED && notes->count==2);
    size_t size;uint8_t* bytes=coop_map_notes_encode(notes,&size);CHECK(bytes && coop_map_notes_decode(loaded,bytes,size));
    CHECK(loaded->serial==3 && loaded->count==2 && !strcmp(loaded->records[0].text,a.note.text));
    bytes[24]^=1;CHECK(!coop_map_notes_decode(loaded,bytes,size) && loaded->count==2);bytes[24]^=1;
    bytes[16+21]=1;coop_object_write32(bytes+size-4,coop_snapshot_crc(bytes,size-4));
    CHECK(!coop_map_notes_decode(loaded,bytes,size) && loaded->serial==3);free(bytes);
    CHECK(coop_note_request_encode(&a,request));request[303]=1;CHECK(!coop_note_request_decode(&parsed,request,sizeof(request)));
    a.note.text[100]='X';CHECK(!coop_note_request_encode(&a,request));a.note.text[100]=0;
    a.note.type=3;CHECK(!coop_note_request_encode(&a,request));a.note.type=0;
    memset(notes,0,sizeof(*notes));
    for (unsigned i=0;i<COOP_NOTES_PER_MAP;i++) CHECK(coop_map_notes_apply(notes,&a)==COOP_NOTE_OK);
    CHECK(coop_map_notes_apply(notes,&a)==COOP_NOTE_FULL && notes->count==COOP_NOTES_PER_MAP);
    a.note.context=8;CHECK(coop_map_notes_apply(notes,&a)==COOP_NOTE_OK);
    notes->serial=COOP_NOTE_ID_LIMIT;CHECK(coop_map_notes_apply(notes,&a)==COOP_NOTE_FULL);
    free(notes);free(loaded);puts("PASS: shared note conflicts, immutable quest markers, non-reused IDs, bounded storage and atomic CRC decode");return 0;
}
