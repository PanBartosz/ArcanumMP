#include "coop/notes.h"
#include "coop/engine_notes.h"
#include "coop/object_state.h"
#include "game/map.h"
#include "ui/intgame.h"
#include "ui/wmap_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { STAGED=1,CAPTURING,REPLY,SENT,RESULT_BYTES=16 };
typedef struct NotePeer {
    CoopNoteRequest request; uint8_t bytes[COOP_NOTE_REQUEST_BYTES];
    uint32_t revision; unsigned phase,status,count; uint64_t window,capture;
} NotePeer;
struct CoopNotes {
    CoopSession* session; CoopWorldNetwork* world; CoopNoteActor actor; void* context;
    uint32_t epoch,map,submitted,status,revision,last_result,last_status; uint64_t now,submitted_at;
    bool host,pending,received; NotePeer peers[COOP_MAX_PLAYERS];
};
CoopNotes* coop_notes_create(CoopSession* session,CoopWorldNetwork* world,uint32_t epoch,uint64_t now,CoopNoteActor actor,void* context)
{
    if (!session || !world || !epoch || !actor) return NULL;
    CoopNotes* s=calloc(1,sizeof(*s));
    if (s) { s->session=session;s->world=world;s->epoch=epoch;s->now=now;s->actor=actor;s->context=context;s->map=(uint32_t)map_current_map();s->host=coop_session_is_host(session); }
    return s;
}
void coop_notes_destroy(CoopNotes* s) { free(s); }
bool coop_notes_message(CoopNotes* s,uint8_t sender,uint8_t type,const uint8_t* p,size_t size)
{
    if (!s || !p || size<8 || coop_object_read32(p)!=s->epoch) return false;
    if (s->host) {
        CoopNoteRequest r;
        if (sender>=COOP_MAX_PLAYERS || !coop_session_players(s->session)[sender].connected || type!=COOP_NOTE_REQUEST
            || !coop_note_request_decode(&r,p,size)) return false;
        NotePeer* peer=&s->peers[sender];
        if (r.sequence==peer->request.sequence) {
            if (memcmp(p,peer->bytes,size)) return false;
            if (peer->phase==SENT) peer->phase=REPLY;
            return true;
        }
        if (peer->request.sequence==UINT32_MAX || r.sequence!=peer->request.sequence+1 || (peer->phase && peer->phase!=SENT)) return false;
        if (s->now-peer->window>=1000) { peer->window=s->now;peer->count=0; }
        if (++peer->count>30) return false;
        peer->request=r;memcpy(peer->bytes,p,size);peer->phase=STAGED;return true;
    }
    if (sender || type!=COOP_NOTE_RESULT || size!=RESULT_BYTES || !s->submitted || coop_object_read32(p+4)!=s->submitted) return false;
    unsigned status=coop_object_read32(p+8); uint32_t revision=coop_object_read32(p+12);
    if (status>COOP_NOTE_FULL || !revision) return false;
    if (s->received) return status==s->status && revision==s->revision;
    s->status=status;s->revision=revision;s->received=true;return true;
}
bool coop_notes_submit(CoopNotes* s,const CoopNoteRequest* request)
{
    if (!s || !request || s->pending || s->submitted==UINT32_MAX) return false;
    CoopNoteRequest r=*request;r.epoch=s->epoch;r.sequence=s->submitted+1;
    uint8_t bytes[COOP_NOTE_REQUEST_BYTES];if (!coop_note_request_encode(&r,bytes)) return false;
    uint8_t slot=coop_session_local_slot(s->session);
    if (!(s->host?coop_notes_message(s,slot,COOP_NOTE_REQUEST,bytes,sizeof(bytes)):coop_session_send(s->session,0,COOP_NOTE_REQUEST,bytes,sizeof(bytes)))) return false;
    s->submitted++;s->pending=true;s->received=false;s->submitted_at=s->now;return true;
}
bool coop_notes_poll(CoopNotes* s,uint64_t now,bool accepting)
{
    if (coop_world_net_retired(s->world)) return true;
    if (!s) return false;
    if (now>s->now) s->now=now;
    if (s->pending && now-s->submitted_at>30000) return false;
    if (s->host) for (unsigned i=0;i<COOP_MAX_PLAYERS;i++) {
        NotePeer* peer=&s->peers[i];
        if (peer->phase==STAGED) {
            int64_t actor=s->actor(s->context,(uint8_t)i,peer->bytes);
            peer->status=!accepting?COOP_NOTE_UNAVAILABLE:!actor || peer->request.map!=s->map?COOP_NOTE_OWNER:coop_engine_note_apply(actor,&peer->request);
            if (coop_world_net_retired(s->world)) return true;
            if (!peer->status) { peer->capture=coop_world_net_request_capture(s->world);peer->phase=CAPTURING; }
            else { peer->revision=coop_world_net_revision(s->world);peer->phase=REPLY; }
            fprintf(stderr,"ARCMP map note sender=%u sequence=%u operation=%u id=%u context=%u status=%u\n",i,peer->request.sequence,peer->request.operation,peer->request.note.id,peer->request.note.context,peer->status);
        }
        if (peer->phase==CAPTURING && coop_world_net_captures(s->world)>=peer->capture) { peer->revision=coop_world_net_revision(s->world);peer->phase=REPLY; }
        if (peer->phase==REPLY) {
            uint8_t bytes[RESULT_BYTES];coop_object_write32(bytes,s->epoch);coop_object_write32(bytes+4,peer->request.sequence);coop_object_write32(bytes+8,peer->status);coop_object_write32(bytes+12,peer->revision);
            if (!i) { s->status=peer->status;s->revision=peer->revision;s->received=true;peer->phase=SENT; }
            else if (coop_session_send(s->session,(uint8_t)i,COOP_NOTE_RESULT,bytes,sizeof(bytes))) peer->phase=SENT;
        }
    }
    if (s->pending && s->received && coop_world_net_applied_revision(s->world)>=s->revision) {
        s->pending=false;
        s->last_result=s->submitted;s->last_status=s->status;
        bool explained=wmap_ui_coop_note_result(s->status);
        static char* feedback[]={"Party map note updated.","Map notes cannot change during the current party action.","That character belongs to another player.",
            "Another player changed this note. Open it again before editing.","Choose a position on the current map.","The party map-note limit has been reached. Remove an old note first."};
        if (!explained) { UiMessage message={.type=s->status?UI_MSG_TYPE_EXCLAMATION:UI_MSG_TYPE_FEEDBACK,.str=feedback[s->status]};intgame_message_window_display_msg(&message); }
    }
    return true;
}
bool coop_notes_settled(const CoopNotes* s)
{
    if (!s || s->pending) return false;
    for (unsigned i=0;i<COOP_MAX_PLAYERS;i++) if (s->peers[i].phase && s->peers[i].phase!=SENT) return false;
    return true;
}
uint32_t coop_notes_last_result(const CoopNotes* s,unsigned* status)
{
    if (status) *status=s?s->last_status:COOP_NOTE_UNAVAILABLE;
    return s?s->last_result:0;
}
