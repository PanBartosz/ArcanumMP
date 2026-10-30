#include "coop/reconnect.h"
#include "coop/object_state.h"
#include <stdlib.h>
#include <string.h>
struct CoopReconnect {
    CoopSession* session;
    CoopReconnectView view;
    uint32_t acknowledged[COOP_MAX_PLAYERS];
    uint64_t now, deadline, sent;
    bool active, ready, dirty;
};
static uint8_t connected(const CoopReconnect* s)
{
    uint8_t mask = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (coop_session_players(s->session)[i].connected) mask |= (uint8_t)(1u << i);
    return mask;
}
static bool valid(const CoopReconnectView* v)
{
    return v && v->epoch && v->revision && v->participants > 1 && v->participants <= 15 && (v->participants & 1)
        && (v->connected & 1) && !(v->connected & ~v->participants) && v->remaining_ms && v->remaining_ms <= COOP_RECONNECT_GRACE
        && (v->phase == COOP_RECONNECT_WAIT || (v->phase == COOP_RECONNECT_SYNC && v->connected == v->participants));
}
bool coop_reconnect_encode(const CoopReconnectView* v, uint8_t out[COOP_RECONNECT_BYTES])
{
    if (!out || !valid(v)) return false;
    coop_object_write32(out,v->epoch); coop_object_write32(out+4,v->revision);
    out[8]=v->participants; out[9]=v->connected; out[10]=v->phase; out[11]=0;
    coop_object_write32(out+12,v->remaining_ms); return true;
}
bool coop_reconnect_decode(const void* bytes, size_t size, CoopReconnectView* out)
{
    if (!bytes || !out || size != COOP_RECONNECT_BYTES) return false;
    const uint8_t* p = bytes;
    CoopReconnectView v = {.epoch=coop_object_read32(p),.revision=coop_object_read32(p+4),.participants=p[8],.connected=p[9],.phase=p[10],.remaining_ms=coop_object_read32(p+12)};
    if (p[11] || !valid(&v)) return false;
    *out=v; return true;
}
CoopReconnect* coop_reconnect_create(CoopSession* session)
{
    if (!session) return NULL;
    CoopReconnect* s=calloc(1,sizeof(*s)); if (s) s->session=session; return s;
}
void coop_reconnect_destroy(CoopReconnect* s) { free(s); }
bool coop_reconnect_begin(CoopReconnect* s,uint32_t epoch,uint8_t participants,uint64_t now)
{
    if (!s || !coop_session_is_host(s->session) || epoch <= s->view.epoch) return false;
    CoopReconnectView v={.epoch=epoch,.revision=1,.participants=participants,.connected=connected(s),.phase=COOP_RECONNECT_WAIT,.remaining_ms=COOP_RECONNECT_GRACE};
    if (!valid(&v)) return false;
    s->view=v; s->now=now; s->deadline=now+COOP_RECONNECT_GRACE; s->sent=0; s->active=s->dirty=true; s->ready=false;
    memset(s->acknowledged,0,sizeof(s->acknowledged)); return true;
}
bool coop_reconnect_message(CoopReconnect* s,uint8_t sender,uint8_t type,const uint8_t* bytes,size_t size)
{
    if (!s || !bytes) return false;
    if (coop_session_is_host(s->session)) {
        if (type != COOP_RECONNECT_ACK || size != 8 || sender >= COOP_MAX_PLAYERS || !coop_session_players(s->session)[sender].connected) return false;
        uint32_t epoch=coop_object_read32(bytes),revision=coop_object_read32(bytes+4);
        if (epoch < s->view.epoch) return true;
        if (epoch != s->view.epoch || !revision || revision > s->view.revision) return false;
        if (revision > s->acknowledged[sender]) s->acknowledged[sender]=revision;
        return true;
    }
    CoopReconnectView v;
    if (sender || type != COOP_RECONNECT_VIEW || !coop_reconnect_decode(bytes,size,&v)) return false;
    if (v.epoch < s->view.epoch || (v.epoch == s->view.epoch && v.revision < s->view.revision)) return true;
    if (v.epoch == s->view.epoch && v.revision == s->view.revision) {
        uint8_t prior[COOP_RECONNECT_BYTES]; return coop_reconnect_encode(&s->view,prior) && !memcmp(prior,bytes,size);
    }
    if (!s->active && v.epoch <= s->view.epoch) return true;
    if (v.epoch > s->view.epoch) {
        if (v.phase != COOP_RECONNECT_WAIT) return false;
        s->deadline=0;
    } else if (v.participants != s->view.participants || (s->view.phase == COOP_RECONNECT_SYNC && v.phase != COOP_RECONNECT_SYNC && v.connected == s->view.connected)) return false;
    s->view=v; s->active=true; s->sent=0; return true;
}
static bool changed(CoopReconnect* s)
{
    if (s->view.revision == UINT32_MAX) return false;
    s->view.revision++; s->dirty=true; s->ready=false; return true;
}
bool coop_reconnect_poll(CoopReconnect* s,uint64_t now,bool prepared)
{
    if (!s) return false;
    if (now > s->now) s->now=now;
    if (!s->active) return true;
    if (!s->deadline) s->deadline=s->now+s->view.remaining_ms;
    if (s->now >= s->deadline) return false;
    if (coop_session_phase(s->session) != COOP_LOBBY) return true;
    if (coop_session_is_host(s->session)) {
        uint8_t mask=connected(s);
        if (mask != s->view.connected) {
            s->view.connected=mask; s->view.phase=COOP_RECONNECT_WAIT;
            if (!changed(s)) return false;
        }
        if (prepared) s->acknowledged[0]=s->view.revision;
        bool all=s->view.connected==s->view.participants;
        for (unsigned i=0;i<COOP_MAX_PLAYERS;i++) if (s->view.participants & (1u<<i)) all &= s->acknowledged[i]==s->view.revision;
        if (all && s->view.phase==COOP_RECONNECT_WAIT) {
            s->view.phase=COOP_RECONNECT_SYNC; if (!changed(s)) return false;
        } else if (all && s->view.phase==COOP_RECONNECT_SYNC) s->ready=true;
        uint32_t remaining=(uint32_t)(s->deadline-s->now);
        if (s->view.phase==COOP_RECONNECT_WAIT && remaining/1000 != s->view.remaining_ms/1000) {
            s->view.remaining_ms=remaining; if (!changed(s)) return false;
        }
        if (s->dirty || s->now-s->sent>=100) {
            uint8_t bytes[COOP_RECONNECT_BYTES]; if (!coop_reconnect_encode(&s->view,bytes)) return false;
            for (unsigned i=1;i<COOP_MAX_PLAYERS;i++) if (s->view.connected & (1u<<i)) coop_session_send(s->session,(uint8_t)i,COOP_RECONNECT_VIEW,bytes,sizeof(bytes));
            s->dirty=false; s->sent=s->now;
        }
    } else if (prepared && (!s->sent || s->now-s->sent>=100)) {
        uint8_t bytes[8]; coop_object_write32(bytes,s->view.epoch); coop_object_write32(bytes+4,s->view.revision);
        if (coop_session_send(s->session,0,COOP_RECONNECT_ACK,bytes,sizeof(bytes))) s->sent=s->now;
    }
    return true;
}
const CoopReconnectView* coop_reconnect_current(const CoopReconnect* s) { return s ? &s->view : NULL; }
bool coop_reconnect_active(const CoopReconnect* s) { return s && s->active; }
bool coop_reconnect_ready(const CoopReconnect* s) { return s && s->active && s->ready; }
void coop_reconnect_end(CoopReconnect* s) { if (s) { s->active=s->ready=false; } }
