#include "coop/skills.h"
#include "coop/campaign.h"
#include "coop/engine_skills.h"
#include "game/map.h"
#include "ui/intgame.h"
#include "ui/skill_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RESULT_BYTES = 16, FEEDBACK_QUEUE = 64, STAGED = 1, EXECUTING, CAPTURING, REPLY, SENT };
typedef struct SkillPeer {
    uint8_t bytes[COOP_SKILL_REQUEST_BYTES];
    CoopSkillRequest request;
    unsigned phase, status, count;
    uint32_t revision;
    uint64_t window, capture;
} SkillPeer;
typedef struct FeedbackEvent {
    uint8_t bytes[COOP_SKILL_FEEDBACK_BYTES];
    uint64_t capture, created;
    bool sent;
} FeedbackEvent;
typedef struct FeedbackPeer {
    FeedbackEvent queue[FEEDBACK_QUEUE];
    unsigned head, count;
    uint32_t sequence, acknowledged;
    uint8_t last[COOP_SKILL_FEEDBACK_BYTES];
} FeedbackPeer;
struct CoopSkills {
    CoopSession* session;
    CoopWorldNetwork* world;
    CoopItems* items;
    uint32_t epoch, map;
    bool host, pending, received, failed;
    CoopSkillActor actor;
    void* context;
    SkillPeer peers[COOP_MAX_PLAYERS];
    FeedbackPeer feedback[COOP_MAX_PLAYERS];
    uint64_t now, submitted_at, feedback_at;
    uint32_t submitted, last_result, waiting_revision;
    unsigned last_status, executed, rejected;
    uint8_t feedback_pending[COOP_SKILL_FEEDBACK_BYTES], feedback_applied[COOP_SKILL_FEEDBACK_BYTES];
    bool feedback_waiting, feedback_ack;
};
static bool feedback_valid(CoopSkills* s, const uint8_t* p, size_t size)
{
    CoopSkillFeedback f;
    int member = coop_campaign_slot_for_owner(coop_session_players(s->session)[coop_session_local_slot(s->session)].identity);
    const CoopCampaignMember* pc = member < 0 ? NULL : coop_campaign_member((unsigned)member);
    return coop_skill_feedback_decode(p, size, &f) && f.epoch == s->epoch && f.map == s->map
        && pc && !memcmp(pc->object_id.d.g.data, f.actor, 16);
}
static bool present_feedback(CoopSkills* s, const uint8_t* p)
{
    if (!feedback_valid(s, p, COOP_SKILL_FEEDBACK_BYTES)) return false;
    return skill_ui_coop_feedback((int)((unsigned)p[32] << 8 | p[33]), p[34] != 0, p[35]);
}
static bool feedback_message(CoopSkills* s, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    uint32_t sequence = coop_object_read32(p + 4);
    if (s->host) {
        if (type != COOP_SKILL_FEEDBACK_ACK || sender >= COOP_MAX_PLAYERS || !sender || size != 8) return false;
        FeedbackPeer* peer = &s->feedback[sender];
        if (sequence && sequence == peer->acknowledged) return true;
        if (!peer->count || !peer->queue[peer->head].sent || sequence != coop_object_read32(peer->queue[peer->head].bytes + 4)) return false;
        peer->acknowledged = sequence;
        memcpy(peer->last, peer->queue[peer->head].bytes, sizeof(peer->last));
        peer->head = (peer->head + 1) % FEEDBACK_QUEUE; peer->count--;
        return true;
    }
    if (sender || type != COOP_SKILL_FEEDBACK || !feedback_valid(s, p, size)) return false;
    uint32_t applied = coop_object_read32(s->feedback_applied + 4);
    if (sequence == applied) {
        if (memcmp(p, s->feedback_applied, size)) return false;
        s->feedback_ack = true;
        return true;
    }
    if (s->feedback_waiting) return !memcmp(p, s->feedback_pending, size);
    if (applied == UINT32_MAX || sequence != applied + 1) return false;
    memcpy(s->feedback_pending, p, size);
    s->feedback_waiting = true; s->feedback_at = s->now;
    return true;
}
CoopSkills* coop_skills_create(CoopSession* session, CoopWorldNetwork* world, CoopItems* items, uint32_t epoch, uint64_t now, CoopSkillActor actor, void* context)
{
    if (!session || !world || !epoch || !actor) return NULL;
    CoopSkills* s = calloc(1, sizeof(*s));
    if (s) {
        s->session = session; s->world = world; s->items = items; s->epoch = epoch; s->now = now;
        s->actor = actor; s->context = context; s->host = coop_session_is_host(session); s->map = (uint32_t)map_current_map();
    }
    return s;
}
void coop_skills_destroy(CoopSkills* s) { free(s); }
bool coop_skills_message(CoopSkills* s, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (!s || !p || size < 8 || coop_object_read32(p) != s->epoch) return false;
    if (type == COOP_SKILL_FEEDBACK || type == COOP_SKILL_FEEDBACK_ACK) return feedback_message(s, sender, type, p, size);
    if (s->host) {
        CoopSkillRequest r;
        if (type != COOP_SKILL_REQUEST || sender >= COOP_MAX_PLAYERS || !coop_session_players(s->session)[sender].connected
            || !coop_skill_request_decode(p, size, &r)) return false;
        SkillPeer* peer = &s->peers[sender];
        if (r.sequence == peer->request.sequence) {
            if (memcmp(peer->bytes, p, size)) return false;
            if (peer->phase == SENT) peer->phase = REPLY;
            return true;
        }
        if (peer->request.sequence == UINT32_MAX || r.sequence != peer->request.sequence + 1
            || (peer->phase && peer->phase != SENT)) return false;
        if (s->now - peer->window >= 1000) { peer->window = s->now; peer->count = 0; }
        if (++peer->count > 30) return false;
        peer->request = r; memcpy(peer->bytes, p, size); peer->phase = STAGED;
        return true;
    }
    uint32_t sequence = coop_object_read32(p + 4);
    if (sender || type != COOP_SKILL_RESULT || size != RESULT_BYTES || !sequence || sequence != s->submitted) return false;
    unsigned status = coop_object_read32(p + 8); uint32_t revision = coop_object_read32(p + 12);
    if (status > COOP_SKILL_REFUSED || !revision) return false;
    if (s->received) return status == s->last_status && revision == s->waiting_revision;
    s->last_status = status; s->waiting_revision = revision; s->received = true;
    return true;
}
bool coop_skills_feedback(CoopSkills* s, uint8_t recipient, unsigned message, bool red, unsigned catalogue)
{
    if (!s || !s->host || recipient >= COOP_MAX_PLAYERS) return false;
    FeedbackPeer* peer = &s->feedback[recipient];
    int member = coop_campaign_slot_for_owner(coop_session_players(s->session)[recipient].identity);
    const CoopCampaignMember* pc = member < 0 ? NULL : coop_campaign_member((unsigned)member);
    if (!pc || !coop_session_players(s->session)[recipient].connected || peer->count == FEEDBACK_QUEUE || peer->sequence == UINT32_MAX || message > UINT16_MAX || catalogue > 6) {
        s->failed = true; return false;
    }
    CoopSkillFeedback f = { .epoch = s->epoch, .sequence = peer->sequence + 1, .map = (uint32_t)map_current_map(), .revision = 1, .message = (uint16_t)message, .red = red, .catalogue = (uint8_t)catalogue };
    memcpy(f.actor, pc->object_id.d.g.data, 16);
    FeedbackEvent* event = &peer->queue[(peer->head + peer->count) % FEEDBACK_QUEUE];
    memset(event, 0, sizeof(*event));
    if (!coop_skill_feedback_encode(&f, event->bytes) || f.map != s->map) { s->failed = true; return false; }
    peer->count++; peer->sequence++;
    event->capture = coop_world_net_request_capture(s->world); event->created = s->now;
    return true;
}
static bool feedback_poll(CoopSkills* s)
{
    if (s->host) {
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            FeedbackPeer* peer = &s->feedback[i];
            if (!peer->count) continue;
            FeedbackEvent* event = &peer->queue[peer->head];
            if (s->now - event->created > 30000) return false;
            if (event->sent || coop_world_net_captures(s->world) < event->capture) continue;
            coop_object_write32(event->bytes + 12, coop_world_net_revision(s->world));
            if (!i) {
                if (!present_feedback(s, event->bytes)) return false;
                peer->acknowledged = coop_object_read32(event->bytes + 4);
                memcpy(peer->last, event->bytes, sizeof(peer->last));
                peer->head = (peer->head + 1) % FEEDBACK_QUEUE; peer->count--;
            } else event->sent = coop_session_send(s->session, (uint8_t)i, COOP_SKILL_FEEDBACK, event->bytes, sizeof(event->bytes));
        }
        return true;
    }
    if (s->feedback_waiting) {
        if (s->now - s->feedback_at > 30000) return false;
        if (coop_world_net_applied_revision(s->world) >= coop_object_read32(s->feedback_pending + 12)) {
            if (!present_feedback(s, s->feedback_pending)) return false;
            memcpy(s->feedback_applied, s->feedback_pending, sizeof(s->feedback_applied));
            s->feedback_waiting = false; s->feedback_ack = true;
        }
    }
    if (s->feedback_ack && coop_session_send(s->session, 0, COOP_SKILL_FEEDBACK_ACK, s->feedback_applied, 8)) s->feedback_ack = false;
    return true;
}
static void present_result(CoopSkills* s)
{
    if (!s->pending || !s->received || coop_world_net_applied_revision(s->world) < s->waiting_revision) return;
    s->last_result = s->submitted; s->pending = false;
    static const char* errors[] = { "", "That skill is unavailable now.", "That character or item belongs to another player.",
        "That is not a valid skill target.", "That target is too far away.", "It is another character's turn.",
        "The turn changed. Try again.", "Finish your current action first.", "That skill cannot be used now." };
    if (s->last_status) {
        UiMessage message = { .type = UI_MSG_TYPE_EXCLAMATION, .str = (char*)errors[s->last_status] };
        intgame_message_window_display_msg(&message);
    }
}
bool coop_skills_poll(CoopSkills* s, uint64_t now, bool accepting)
{
    if (coop_world_net_retired(s->world)) return true;
    if (now > s->now) s->now = now;
    if (s->failed || (s->pending && s->now - s->submitted_at > 30000)) return false;
    if (s->host) for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        SkillPeer* peer = &s->peers[i];
        if (peer->phase == STAGED) {
            peer->phase = EXECUTING;
            int64_t actor = accepting ? s->actor(s->context, (uint8_t)i, peer->bytes) : 0;
            peer->status = !accepting ? COOP_SKILL_UNAVAILABLE : actor ? (unsigned)coop_engine_skills_execute(actor, &peer->request, s->items, (uint8_t)i) : COOP_SKILL_OWNERSHIP;
            if (coop_world_net_retired(s->world)) return true;
            if (peer->status) s->rejected++; else s->executed++;
            peer->capture = coop_world_net_request_capture(s->world); peer->phase = CAPTURING;
            fprintf(stderr, "ARCMP skill action sender=%u sequence=%u action=%u status=%u\n", i, peer->request.sequence, peer->request.action, peer->status);
        }
        if (peer->phase == CAPTURING && coop_world_net_captures(s->world) >= peer->capture) {
            peer->revision = coop_world_net_revision(s->world); peer->phase = REPLY;
        }
        if (peer->phase == REPLY) {
            uint8_t bytes[RESULT_BYTES];
            coop_object_write32(bytes, s->epoch); coop_object_write32(bytes + 4, peer->request.sequence);
            coop_object_write32(bytes + 8, peer->status); coop_object_write32(bytes + 12, peer->revision);
            if (!i) {
                s->last_status = peer->status; s->waiting_revision = peer->revision; s->received = true; peer->phase = SENT;
            } else if (coop_session_send(s->session, (uint8_t)i, COOP_SKILL_RESULT, bytes, sizeof(bytes))) peer->phase = SENT;
        }
    }
    present_result(s);
    return feedback_poll(s);
}
bool coop_skills_submit(CoopSkills* s, const CoopSkillRequest* request)
{
    if (!s || !request || s->pending || s->failed || s->submitted == UINT32_MAX) return false;
    CoopSkillRequest r = *request; r.epoch = s->epoch; r.sequence = s->submitted + 1;
    uint8_t bytes[COOP_SKILL_REQUEST_BYTES];
    if (!coop_skill_request_encode(&r, bytes)) return false;
    uint8_t slot = coop_session_local_slot(s->session);
    if (!(s->host ? coop_skills_message(s, slot, COOP_SKILL_REQUEST, bytes, sizeof(bytes)) : coop_session_send(s->session, 0, COOP_SKILL_REQUEST, bytes, sizeof(bytes)))) return false;
    s->submitted++; s->pending = true; s->received = false; s->submitted_at = s->now;
    return true;
}
bool coop_skills_feedback_last(const CoopSkills* s, uint8_t recipient, uint8_t out[COOP_SKILL_FEEDBACK_BYTES])
{
    if (!s || !out || recipient >= COOP_MAX_PLAYERS || (!s->host && recipient != coop_session_local_slot(s->session))) return false;
    const uint8_t* bytes = s->host ? s->feedback[recipient].last : s->feedback_applied;
    if (!coop_object_read32(bytes + 4)) return false;
    memcpy(out, bytes, COOP_SKILL_FEEDBACK_BYTES);
    return true;
}
bool coop_skills_busy(const CoopSkills* s) { return s->pending; }
bool coop_skills_settled(const CoopSkills* s)
{
    if (s->pending || s->failed || s->feedback_waiting || s->feedback_ack) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if ((s->peers[i].phase && s->peers[i].phase != SENT) || s->feedback[i].count) return false;
    return true;
}
uint32_t coop_skills_last_result(const CoopSkills* s, unsigned* status) { *status = s->last_status; return s->last_result; }
unsigned coop_skills_executed(const CoopSkills* s) { return s->executed; }
unsigned coop_skills_rejected(const CoopSkills* s) { return s->rejected; }
