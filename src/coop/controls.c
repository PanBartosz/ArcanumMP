#include "coop/controls.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
typedef struct ControlPeer {
    uint8_t bytes[COOP_CONTROL_REQUEST_BYTES];
    CoopControlRequest request;
    bool staged;
    uint32_t acknowledged;
    unsigned count;
    uint64_t window, sent;
} ControlPeer;
struct CoopControls {
    CoopSession* session;
    CoopControlValidate validate; CoopControlBegin begin; void* context;
    CoopControlView view;
    CoopControlRequest active;
    ControlPeer peers[COOP_MAX_PLAYERS];
    uint8_t received_bytes[COOP_CONTROL_VIEW_BYTES];
    uint32_t submitted, last_result, serial;
    unsigned status, executed;
    uint64_t now, pending_at, deadline, ack_sent, armed_at;
    bool host, pending, received, armed, started, cancelled, dirty;
};
static bool changed(CoopControls* s)
{
    if (s->view.revision == UINT32_MAX) return false;
    s->view.revision++; s->dirty = true;
    return true;
}
CoopControls* coop_controls_create(CoopSession* session, uint32_t epoch, uint32_t map, uint64_t now, CoopControlValidate validate, CoopControlBegin begin, void* context)
{
    if (!session || !epoch || map > INT32_MAX || !validate || !begin) return NULL;
    CoopControls* s = calloc(1, sizeof(*s));
    if (s) {
        s->session = session; s->host = coop_session_is_host(session); s->validate = validate; s->begin = begin; s->context = context; s->now = now;
        s->view = (CoopControlView) { .epoch = epoch, .revision = 1, .map = map, .participants = 1, .proposer = COOP_NO_SLOT };
        s->dirty = true;
    }
    return s;
}
void coop_controls_destroy(CoopControls* s) { free(s); }
bool coop_controls_restore_pauses(CoopControls* s,uint8_t pauses)
{
    if (!s || !s->host || s->view.phase != COOP_CONTROL_IDLE) return false;
    uint8_t participants=coop_session_reserved(s->session);
    if (!participants || (pauses & ~participants)) return false;
    s->view.participants=participants; s->view.paused=pauses;
    return changed(s);
}
bool coop_controls_restore_interrupted(CoopControls* s,const CoopControlView* previous)
{
    if (!s || !s->host || !previous || s->view.phase != COOP_CONTROL_IDLE
        || (previous->action != COOP_CONTROL_REST && previous->action != COOP_CONTROL_SAVE
            && previous->action != COOP_CONTROL_LOAD && previous->action != COOP_CONTROL_TRAVEL)) return false;
    CoopControlView restored=s->view;
    restored.route_count=previous->route_count; memcpy(restored.route,previous->route,sizeof(restored.route));
    restored.action=previous->action; restored.value=previous->value;
    restored.checkpoint_fingerprint=previous->checkpoint_fingerprint; restored.checkpoint_present=previous->checkpoint_present;
    memcpy(restored.description,previous->description,sizeof(restored.description));
    restored.completed=previous->completed; restored.notice=COOP_CONTROL_INTERRUPTED;
    uint8_t bytes[COOP_CONTROL_VIEW_BYTES];
    if (!coop_control_view_encode(&restored,bytes)) return false;
    s->view=restored; return changed(s);
}
static void idle(CoopControls* s, unsigned notice)
{
    s->view.phase = COOP_CONTROL_IDLE; s->view.notice = (uint8_t)notice;
    s->view.proposal = s->view.agreed = s->view.refused = s->view.remaining_ms = 0;
    s->armed = s->started = s->cancelled = false;
}
bool coop_controls_message(CoopControls* s, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    if (!s || !bytes) return false;
    if (s->host) {
        if (sender >= COOP_MAX_PLAYERS || !coop_session_players(s->session)[sender].connected) return false;
        if (type == COOP_CONTROL_ACK) {
            if (size != 8) return false;
            uint32_t epoch = coop_object_read32(bytes);
            if (epoch < s->view.epoch) return epoch != 0;
            if (epoch != s->view.epoch) return false;
            uint32_t revision = coop_object_read32(bytes + 4);
            if (!revision || revision > s->view.revision) return false;
            if (revision > s->peers[sender].acknowledged) s->peers[sender].acknowledged = revision;
            return true;
        }
        CoopControlRequest r;
        if (type != COOP_CONTROL_REQUEST || !coop_control_request_decode(bytes, size, &r)) return false;
        ControlPeer* peer = &s->peers[sender];
        if (r.epoch != s->view.epoch) {
            if (r.epoch < s->view.epoch && r.sequence == peer->request.sequence && !memcmp(bytes, peer->bytes, size)) { peer->acknowledged = 0; peer->sent = 0; return true; }
            return false;
        }
        if (r.sequence == peer->request.sequence) {
            if (memcmp(bytes, peer->bytes, size)) return false;
            peer->acknowledged = 0; peer->sent = 0; return true;
        }
        if (peer->staged || s->view.sequences[sender] == UINT32_MAX || r.sequence != s->view.sequences[sender] + 1) return false;
        if (s->now - peer->window >= 1000) { peer->window = s->now; peer->count = 0; }
        if (++peer->count > 30) return false;
        peer->request = r; memcpy(peer->bytes, bytes, size); peer->staged = true;
        return true;
    }
    CoopControlView v;
    if (sender || type != COOP_CONTROL_VIEW || !coop_control_view_decode(bytes, size, &v)) return false;
    if (v.epoch < s->view.epoch || (v.epoch == s->view.epoch && v.revision < s->view.revision)) return true;
    if (v.epoch > s->view.epoch && (s->view.epoch == UINT32_MAX || v.epoch != s->view.epoch + 1)) return false;
    if (s->received && v.epoch == s->view.epoch && v.revision == s->view.revision && memcmp(bytes, s->received_bytes, size)) return false;
    s->view = v; memcpy(s->received_bytes, bytes, size); s->received = true; s->ack_sent = 0;
    uint8_t local = coop_session_local_slot(s->session);
    if (local >= COOP_MAX_PLAYERS) return false;
    if (s->pending && s->view.sequences[local] == s->submitted) { s->pending = false; s->last_result = s->submitted; s->status = s->view.statuses[local]; }
    return true;
}
static unsigned execute(CoopControls* s, unsigned sender, const CoopControlRequest* r, bool ready)
{
    if (!ready) return COOP_CONTROL_UNAVAILABLE;
    unsigned status = s->validate(s->context, (uint8_t)sender, r);
    if (status) return status;
    CoopControlView* v = &s->view;
    if (r->action == COOP_CONTROL_PAUSE || r->action == COOP_CONTROL_RESUME_ALL) {
        if (v->phase != COOP_CONTROL_IDLE && v->phase != COOP_CONTROL_VOTING) return COOP_CONTROL_BUSY;
        if (r->action == COOP_CONTROL_RESUME_ALL) {
            if (sender) return COOP_CONTROL_OWNER;
            v->paused = 0;
        } else if (r->value) v->paused |= (uint8_t)(1u << sender);
        else v->paused &= (uint8_t)~(1u << sender);
        return COOP_CONTROL_OK;
    }
    if (r->action == COOP_CONTROL_VOTE) {
        if (v->phase != COOP_CONTROL_VOTING || r->proposal != v->proposal) return COOP_CONTROL_UNAVAILABLE;
        if (r->value) { v->agreed |= (uint8_t)(1u << sender); return COOP_CONTROL_OK; }
        idle(s, COOP_CONTROL_DECLINED); return COOP_CONTROL_OK;
    }
    if (r->action == COOP_CONTROL_CANCEL) {
        if (!v->proposal || r->proposal != v->proposal) return COOP_CONTROL_UNAVAILABLE;
        if (v->phase == COOP_CONTROL_VOTING || (v->phase == COOP_CONTROL_DRAINING && !s->started)) idle(s, COOP_CONTROL_INTERRUPTED);
        else if (v->phase == COOP_CONTROL_RESTING || v->phase == COOP_CONTROL_JOURNEY) s->cancelled = true;
        else return COOP_CONTROL_BUSY;
        return COOP_CONTROL_OK;
    }
    if (v->phase != COOP_CONTROL_IDLE || s->serial == UINT32_MAX) return COOP_CONTROL_BUSY;
    v->route_count = r->route_count; memcpy(v->route, r->route, sizeof(v->route));
    s->active = *r; v->action = r->action; v->value = r->value; v->completed = 0;
    v->checkpoint_fingerprint=r->checkpoint_fingerprint; v->checkpoint_present=r->checkpoint_present;
    memcpy(v->description,r->description,sizeof(v->description));
    v->proposal = ++s->serial; v->proposer = (uint8_t)sender; v->notice = COOP_CONTROL_OK;
    v->agreed = (uint8_t)(1u << sender); v->refused = 0; s->cancelled = s->started = false;
    if (r->action == COOP_CONTROL_SAVE) {
        v->agreed = v->participants; v->phase = COOP_CONTROL_DRAINING; v->remaining_ms = 0;
        s->armed = true; s->armed_at = s->now;
    } else { v->phase = COOP_CONTROL_VOTING; v->remaining_ms = 30000; s->deadline = s->now + 30000; }
    return COOP_CONTROL_OK;
}
bool coop_controls_poll(CoopControls* s, uint64_t now, bool ready)
{
    if (!s) return false;
    if (now > s->now) s->now = now;
    if (s->pending && now - s->pending_at > 30000) return false;
    if (!s->host) {
        if (s->received && now - s->ack_sent >= 200) {
            uint8_t bytes[8]; coop_object_write32(bytes, s->view.epoch); coop_object_write32(bytes + 4, s->view.revision);
            if (coop_session_send(s->session, 0, COOP_CONTROL_ACK, bytes, sizeof(bytes))) s->ack_sent = now;
        }
        return true;
    }
    uint8_t participants = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (coop_session_players(s->session)[i].connected) participants |= (uint8_t)(1u << i);
    if (!participants || !(participants & 1)) return false;
    if (participants != s->view.participants) {
        if (s->view.phase != COOP_CONTROL_IDLE) return false;
        s->view.participants = participants; s->view.paused &= participants;
        if (!changed(s)) return false;
    }
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (s->peers[i].staged) {
        ControlPeer* p = &s->peers[i]; p->staged = false;
        /* Claim admission before a validation or native callback can reenter. */
        s->view.sequences[i] = p->request.sequence;
        unsigned status = execute(s, i, &p->request, ready);
        s->view.statuses[i] = (uint8_t)status;
        if (!i && s->pending && s->submitted == p->request.sequence) { s->pending = false; s->last_result = s->submitted; s->status = status; }
        if (!changed(s)) return false;
    }
    if (s->view.phase == COOP_CONTROL_VOTING) {
        if (now >= s->deadline) { idle(s, COOP_CONTROL_EXPIRED); if (!changed(s)) return false; }
        else if (s->view.agreed == participants) {
            unsigned status = s->validate(s->context, s->view.proposer, &s->active);
            if (status) idle(s, status);
            else { s->view.phase = COOP_CONTROL_DRAINING; s->view.remaining_ms = 0; s->armed = true; s->armed_at = now; }
            if (!changed(s)) return false;
        } else {
            uint32_t remaining = (uint32_t)(s->deadline - now);
            if (remaining / 1000 != s->view.remaining_ms / 1000) { s->view.remaining_ms = remaining; if (!changed(s)) return false; }
        }
    }
    if (s->armed && !s->started) {
        bool all = true;
        for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++) if ((participants & (1u << i)) && s->peers[i].acknowledged != s->view.revision) all = false;
        if (now - s->armed_at > 30000) return false;
        if (all) {
            s->started = true; s->armed = false;
            unsigned status = s->begin(s->context, s->view.proposer, &s->active);
            if (status) { idle(s, status); if (!changed(s)) return false; }
            else s->executed++;
        }
    }
    uint8_t bytes[COOP_CONTROL_VIEW_BYTES];
    if (!coop_control_view_encode(&s->view, bytes)) return false;
    for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++) if ((participants & (1u << i)) && (s->dirty || (s->peers[i].acknowledged != s->view.revision && now - s->peers[i].sent >= 200))) {
        if (coop_session_send(s->session, (uint8_t)i, COOP_CONTROL_VIEW, bytes, sizeof(bytes))) s->peers[i].sent = now;
    }
    s->dirty = false;
    return true;
}
bool coop_controls_submit(CoopControls* s, const CoopControlRequest* request)
{
    if (!s || !request || s->pending || s->submitted == UINT32_MAX) return false;
    CoopControlRequest r = *request; r.epoch = s->view.epoch; r.sequence = s->submitted + 1;
    uint8_t bytes[COOP_CONTROL_REQUEST_BYTES]; if (!coop_control_request_encode(&r, bytes)) return false;
    uint8_t sender = coop_session_local_slot(s->session);
    if (!(s->host ? coop_controls_message(s, sender, COOP_CONTROL_REQUEST, bytes, sizeof(bytes)) : coop_session_send(s->session, 0, COOP_CONTROL_REQUEST, bytes, sizeof(bytes)))) return false;
    s->submitted++; s->pending = true; s->pending_at = s->now;
    return true;
}
const CoopControlView* coop_controls_current(const CoopControls* s) { return s ? &s->view : NULL; }
bool coop_controls_pending(const CoopControls* s) { return s && s->pending; }
bool coop_controls_progress(CoopControls* s, CoopControlPhase phase, unsigned hours)
{
    if (!s || !s->host || !s->started || s->view.phase == COOP_CONTROL_IDLE || phase < COOP_CONTROL_DRAINING || phase > COOP_CONTROL_JOURNEY
        || hours > (phase == COOP_CONTROL_JOURNEY ? 1000000u : 168u)) return false;
    if (s->view.phase == phase && s->view.completed == hours) return true;
    s->view.phase = phase; s->view.completed = hours;
    return changed(s);
}
bool coop_controls_complete(CoopControls* s, unsigned status, uint32_t epoch, uint32_t map)
{
    if (!s || !s->host || status > COOP_CONTROL_CHANGED || epoch < s->view.epoch || (epoch != s->view.epoch && (s->view.epoch == UINT32_MAX || epoch != s->view.epoch + 1)) || !epoch || map > INT32_MAX) return false;
    s->view.epoch = epoch; s->view.map = map; idle(s, status);
    return changed(s);
}
bool coop_controls_context(CoopControls* s, uint32_t epoch, uint32_t map)
{
    if (!s || !epoch || map > INT32_MAX) return false;
    if (!s->host) return true;
    if (s->view.epoch == epoch && s->view.map == map) return true;
    if (s->view.phase != COOP_CONTROL_IDLE) return false;
    s->view.epoch = epoch; s->view.map = map; return changed(s);
}
bool coop_controls_cancelled(const CoopControls* s) { return s && s->cancelled; }
uint32_t coop_controls_last_result(const CoopControls* s, unsigned* status) { if (status) *status = s ? s->status : COOP_CONTROL_UNAVAILABLE; return s ? s->last_result : 0; }
unsigned coop_controls_executed(const CoopControls* s) { return s ? s->executed : 0; }
