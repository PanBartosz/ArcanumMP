#include "coop/progression.h"
#include "coop/engine_progression.h"
#include "coop/engine_options.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/player.h"
#include "game/stat.h"
#include "ui/charedit_ui.h"
#include "ui/intgame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STAGED = 1, EXECUTING, CAPTURING, REPLY, SENT, RESULT_BYTES = 20 };
typedef struct ProgressionPeer {
    uint8_t bytes[COOP_PROGRESSION_REQUEST_BYTES];
    CoopProgressionRequest request;
    CoopProgressionBaseline baseline;
    unsigned phase, status, count;
    uint32_t lease, serial, revision, result_lease;
    uint64_t window, capture;
} ProgressionPeer;
struct CoopProgression {
    CoopSession* session;
    CoopWorldNetwork* world;
    CoopProgressionActor actor;
    void* context;
    ProgressionPeer peers[COOP_MAX_PLAYERS];
    uint32_t epoch, map, submitted, last_result, lease, status, revision, received_lease;
    uint64_t now, submitted_at;
    bool host, pending, received, want_editor, close_requested;
    CoopProgressionOperation submitted_operation;
    CoopProgressionKind submitted_kind;
    uint8_t option_changes, option_values;
    bool submitted_silent;
    unsigned executed, rejected;
};
CoopProgression* coop_progression_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, uint64_t now, CoopProgressionActor actor, void* context)
{
    if (!session || !world || !epoch || !actor) return NULL;
    CoopProgression* s = calloc(1, sizeof(*s));
    if (s) {
        s->session = session; s->world = world; s->epoch = epoch; s->now = now; s->actor = actor; s->context = context;
        s->map = (uint32_t)map_current_map(); s->host = coop_session_is_host(session);
    }
    return s;
}
void coop_progression_destroy(CoopProgression* s) { free(s); }
bool coop_progression_message(CoopProgression* s, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (!s || !p || size < 8 || coop_object_read32(p) != s->epoch) return false;
    if (s->host) {
        CoopProgressionRequest r;
        if (sender >= COOP_MAX_PLAYERS || !coop_session_players(s->session)[sender].connected
            || type != COOP_PROGRESSION_REQUEST || !coop_progression_request_decode(p, size, &r)) return false;
        ProgressionPeer* peer = &s->peers[sender];
        if (r.sequence == peer->request.sequence) {
            if (memcmp(p, peer->bytes, size)) return false;
            if (peer->phase == SENT) peer->phase = REPLY;
            return true;
        }
        if (peer->request.sequence == UINT32_MAX || r.sequence != peer->request.sequence + 1 || (peer->phase && peer->phase != SENT)) return false;
        if (s->now - peer->window >= 1000) { peer->window = s->now; peer->count = 0; }
        if (++peer->count > 30) return false;
        peer->request = r; memcpy(peer->bytes, p, size); peer->phase = STAGED;
        return true;
    }
    if (sender || type != COOP_PROGRESSION_RESULT || size != RESULT_BYTES || !s->submitted || coop_object_read32(p + 4) != s->submitted) return false;
    uint32_t status = coop_object_read32(p + 8), revision = coop_object_read32(p + 12), lease = coop_object_read32(p + 16);
    if (status > COOP_PROGRESS_BASELINE || !revision) return false;
    if (s->received) return status == s->status && revision == s->revision && lease == s->received_lease;
    s->received = true; s->status = status; s->revision = revision; s->received_lease = lease;
    return true;
}
static unsigned execute(CoopProgression* s, ProgressionPeer* peer, unsigned sender, bool accepting)
{
    const CoopProgressionRequest* r = &peer->request;
    int64_t actor = s->actor(s->context, (uint8_t)sender, peer->bytes);
    if (!actor || r->map != s->map) return COOP_PROGRESS_OWNERSHIP;
    if (r->operation == COOP_PROGRESS_CLOSE) {
        if (r->lease != peer->lease) return COOP_PROGRESS_LEASE;
        peer->lease = 0; return COOP_PROGRESS_OK;
    }
    if (!accepting) return COOP_PROGRESS_UNAVAILABLE;
    if (r->operation == COOP_PROGRESS_FATE) return coop_engine_progression_fate(actor, r);
    if (r->operation == COOP_PROGRESS_OPTIONS)
        return coop_engine_options_apply(actor, r->expected, r->direction) ? COOP_PROGRESS_OK : COOP_PROGRESS_CHANGED;
    if (r->operation == COOP_PROGRESS_OPEN) {
        if (peer->lease || peer->serial == UINT32_MAX) return COOP_PROGRESS_LEASE;
        if (!coop_engine_progression_open(actor, &peer->baseline)) return COOP_PROGRESS_UNAVAILABLE;
        peer->lease = ++peer->serial;
        return COOP_PROGRESS_OK;
    }
    if (!peer->lease || r->lease != peer->lease) return COOP_PROGRESS_LEASE;
    return coop_engine_progression_edit(actor, &peer->baseline, r);
}
bool coop_progression_submit(CoopProgression* s, const CoopProgressionRequest* request)
{
    if (!s || !request || s->pending || s->submitted == UINT32_MAX) return false;
    CoopProgressionRequest r = *request; r.epoch = s->epoch; r.sequence = s->submitted + 1;
    uint8_t bytes[COOP_PROGRESSION_REQUEST_BYTES];
    if (!coop_progression_request_encode(&r, bytes)) return false;
    uint8_t slot = coop_session_local_slot(s->session);
    if (!(s->host ? coop_progression_message(s, slot, COOP_PROGRESSION_REQUEST, bytes, sizeof(bytes)) : coop_session_send(s->session, 0, COOP_PROGRESSION_REQUEST, bytes, sizeof(bytes)))) return false;
    s->submitted++; s->submitted_operation = r.operation; s->submitted_kind = r.kind; s->submitted_at = s->now; s->pending = true; s->received = false; s->submitted_silent = false;
    return true;
}
static bool local_request(CoopProgression* s, CoopProgressionRequest* r)
{
    int64_t actor = player_get_local_pc_obj();
    if (!actor || map_current_map() != (int)s->map) return false;
    r->map = s->map; memcpy(r->actor, obj_get_id(actor).d.g.data, 16);
    return coop_progression_submit(s, r);
}
bool coop_progression_defaults(CoopProgression* s, unsigned mask)
{
    if (!s || mask > 31 || coop_engine_options_record(player_get_local_pc_obj())) return false;
    CoopProgressionRequest r = { .operation = COOP_PROGRESS_OPTIONS, .direction = (uint8_t)mask };
    if (!local_request(s, &r)) return false;
    /* A starting conversation can become active after a replica submits its
     * automatic defaults. Let the normal initializer retry after that barrier;
     * this is not a refused user gesture and must not produce a false alert. */
    s->submitted_silent = true;
    return true;
}
void coop_progression_editor(CoopProgression* s, bool open)
{
    if (!s) return;
    s->want_editor = open;
    if (!open) s->close_requested = true;
}
bool coop_progression_change(CoopProgression* s, CoopProgressionKind kind, unsigned index, bool increase)
{
    if (!s || s->pending || s->close_requested || !s->want_editor || !s->lease || !coop_progression_selection_valid(kind, index)) return false;
    int64_t actor = player_get_local_pc_obj();
    int value = coop_engine_progression_value(actor, kind, index), points = stat_level_get(actor, STAT_UNSPENT_POINTS);
    if (value < 0 || points < 0) return false;
    CoopProgressionRequest r = { .operation = COOP_PROGRESS_EDIT, .kind = kind, .index = (uint8_t)index,
        .direction = increase ? 1 : 2, .lease = s->lease, .expected = (uint32_t)value, .points = (uint32_t)points };
    return local_request(s, &r);
}
bool coop_progression_option(CoopProgression* s, unsigned option, bool value)
{
    if (!s || option >= COOP_OPTION_COUNT || coop_world_net_retired(s->world)) return false;
    unsigned bit = 1u << option;
    s->option_changes |= bit;
    if (value) s->option_values |= bit; else s->option_values &= ~bit;
    return true;
}
bool coop_progression_poll(CoopProgression* s, uint64_t now, bool accepting)
{
    if (coop_world_net_retired(s->world)) return true;
    if (now > s->now) s->now = now;
    if (s->pending && s->now - s->submitted_at > 30000) return false;
    if (s->host) for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        ProgressionPeer* peer = &s->peers[i];
        if (peer->phase == STAGED) {
            peer->phase = EXECUTING; peer->status = execute(s, peer, i, accepting);
            if (coop_world_net_retired(s->world)) return true;
            if (peer->status) s->rejected++; else s->executed++;
            peer->result_lease = peer->lease;
            peer->capture = coop_world_net_request_capture(s->world); peer->phase = CAPTURING;
            fprintf(stderr, "ARCMP progression sender=%u sequence=%u operation=%u kind=%u index=%u direction=%u status=%u lease=%u\n",
                i, peer->request.sequence, peer->request.operation, peer->request.kind, peer->request.index, peer->request.direction, peer->status, peer->lease);
        }
        if (peer->phase == CAPTURING && coop_world_net_captures(s->world) >= peer->capture) {
            peer->revision = coop_world_net_revision(s->world); peer->phase = REPLY;
        }
        if (peer->phase == REPLY) {
            uint8_t bytes[RESULT_BYTES]; coop_object_write32(bytes, s->epoch); coop_object_write32(bytes + 4, peer->request.sequence);
            coop_object_write32(bytes + 8, peer->status); coop_object_write32(bytes + 12, peer->revision); coop_object_write32(bytes + 16, peer->result_lease);
            if (!i) {
                s->received = true; s->status = peer->status; s->revision = peer->revision; s->received_lease = peer->result_lease; peer->phase = SENT;
            } else if (coop_session_send(s->session, (uint8_t)i, COOP_PROGRESSION_RESULT, bytes, sizeof(bytes))) peer->phase = SENT;
        }
    }
    if (s->pending && s->received && coop_world_net_applied_revision(s->world) >= s->revision) {
        s->pending = false; s->last_result = s->submitted; s->lease = s->received_lease;
        if (s->submitted_operation == COOP_PROGRESS_OPEN && s->status) s->want_editor = false;
        charedit_refresh();
        if (!s->status && s->submitted_operation == COOP_PROGRESS_EDIT) {
            intgame_message_window_display_feedback(s->submitted_kind == COOP_PROGRESS_SCHEME ? "Auto-level scheme updated." : "Character updated.");
        } else if (s->status && !s->submitted_silent) {
            static char* errors[] = { "", "Character development is unavailable now.", "That character belongs to another player.",
                "Reopen the character sheet to make further changes.", "Your character changed. Try again.", "You haven't enough character points.",
                "That ability has reached its maximum value.", "That change does not meet the requirements.", "You cannot reduce an accepted ability." };
            char* text = errors[s->status];
            if (s->submitted_operation == COOP_PROGRESS_FATE) {
                text = s->status == COOP_PROGRESS_POINTS ? "You have no unspent fate points."
                    : s->status == COOP_PROGRESS_CHANGED ? "Your fate selection changed. Choose again using the current state."
                    : "Your character cannot change fate right now.";
            }
            if (s->submitted_operation == COOP_PROGRESS_OPTIONS) text = s->status == COOP_PROGRESS_CHANGED
                ? "Your character options changed. Select the option again."
                : "Character options cannot change during the current party action.";
            UiMessage message = { .type = UI_MSG_TYPE_EXCLAMATION, .str = text }; intgame_message_window_display_msg(&message);
        }
    }
    if (accepting && !s->pending) {
        if (s->close_requested && !s->lease) s->close_requested = false;
        if ((s->close_requested && s->lease) || (s->want_editor != (s->lease != 0))) {
            bool close = s->close_requested || !s->want_editor;
            CoopProgressionRequest r = { .operation = close ? COOP_PROGRESS_CLOSE : COOP_PROGRESS_OPEN, .lease = s->lease };
            if (!local_request(s, &r)) return false;
        } else if (s->option_changes) {
            unsigned record = coop_engine_options_record(player_get_local_pc_obj());
            unsigned mask = record ? record & 31u : coop_engine_options_defaults();
            mask = (mask & ~s->option_changes) | (s->option_values & s->option_changes);
            CoopProgressionRequest r = { .operation = COOP_PROGRESS_OPTIONS, .direction = (uint8_t)mask, .expected = record };
            if (!local_request(s, &r)) return false;
            s->option_changes = 0;
        }
    }
    if (!accepting && s->option_changes) {
        s->option_changes = 0;
        intgame_message_window_display_feedback("Character options could not change before the party transition. Select them again when the party is ready.");
    }
    return true;
}
bool coop_progression_settled(const CoopProgression* s)
{
    if (s->pending || s->option_changes) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (s->peers[i].phase && s->peers[i].phase != SENT) return false;
    return true;
}
bool coop_progression_busy(const CoopProgression* s) { return s->pending; }
bool coop_progression_editing(const CoopProgression* s)
{
    if (!s || s->want_editor || s->lease || s->close_requested || s->pending || s->option_changes) return true;
    if (s->host) for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (s->peers[i].lease) return true;
    return false;
}
uint32_t coop_progression_lease(const CoopProgression* s) { return s->lease; }
uint32_t coop_progression_last_result(const CoopProgression* s, unsigned* status) { *status = s->status; return s->last_result; }
unsigned coop_progression_executed(const CoopProgression* s) { return s->executed; }
unsigned coop_progression_rejected(const CoopProgression* s) { return s->rejected; }
