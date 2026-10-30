#include "coop/magic.h"
#include "coop/engine_magic.h"
#include "coop/engine_object.h"
#include "coop/campaign.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "ui/intgame.h"
#include "ui/written_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { RESULT_BYTES = 16,
    STAGED = 1,
    EXECUTING,
    CAPTURING,
    REPLY,
    SENT };
typedef struct MagicPeer {
    uint8_t bytes[COOP_MAGIC_REQUEST_BYTES];
    CoopMagicRequest request;
    unsigned phase;
    unsigned status;
    unsigned count;
    uint64_t window;
    uint64_t capture;
    uint32_t revision;
    uint64_t use_started;
    CoopUseTicket use;
} MagicPeer;
enum { WRITTEN_BYTES = COOP_WRITTEN_BYTES, WRITTEN_QUEUE = 16 };
typedef struct WrittenEvent {
    uint8_t bytes[WRITTEN_BYTES];
    uint64_t capture, created;
    bool sent;
} WrittenEvent;
typedef struct WrittenPeer {
    WrittenEvent queue[WRITTEN_QUEUE];
    unsigned head, count;
    uint32_t sequence, acknowledged;
    uint8_t last[WRITTEN_BYTES];
} WrittenPeer;
struct CoopMagic {
    CoopSession* session;
    CoopWorldNetwork* world;
    uint32_t epoch;
    bool host;
    CoopMagicActor actor;
    void* context;
    MagicPeer peers[COOP_MAX_PLAYERS];
    uint64_t now;
    uint64_t submitted_at;
    uint32_t submitted;
    uint32_t last_result;
    uint32_t waiting_revision;
    unsigned last_status;
    CoopMagicOperation last_operation;
    bool pending;
    bool retired;
    bool received;
    unsigned executed;
    unsigned rejected;
    WrittenPeer written[COOP_MAX_PLAYERS];
    uint8_t written_pending[WRITTEN_BYTES], written_applied[WRITTEN_BYTES];
    uint64_t written_at;
    bool written_waiting, written_ack, written_failed;
};
static bool written_valid(CoopMagic* b, const uint8_t* p, size_t size)
{
    CoopWrittenView view;
    if (!coop_written_decode(p, size, &view) || view.epoch != b->epoch || view.map != (uint32_t)map_current_map()) return false;
    int member = coop_campaign_slot_for_object(player_get_local_pc_obj());
    const CoopCampaignMember* pc = member < 0 ? NULL : coop_campaign_member((unsigned)member);
    return pc && !memcmp(p + 16, pc->object_id.d.g.data, 16);
}
static bool written_message(CoopMagic* b, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (b->host) {
        if (type != COOP_WRITTEN_ACK || size != 8 || !sender || sender >= COOP_MAX_PLAYERS) return false;
        WrittenPeer* w = &b->written[sender];
        uint32_t seq = coop_object_read32(p + 4);
        if (seq && seq == w->acknowledged) return true;
        if (!w->count || !w->queue[w->head].sent || seq != coop_object_read32(w->queue[w->head].bytes + 4)) return false;
        w->acknowledged = seq;
        memcpy(w->last, w->queue[w->head].bytes, WRITTEN_BYTES);
        w->head = (w->head + 1) % WRITTEN_QUEUE;
        w->count--;
        return true;
    }
    if (sender || type != COOP_WRITTEN_VIEW || !written_valid(b, p, size)) return false;
    uint32_t seq = coop_object_read32(p + 4), applied = coop_object_read32(b->written_applied + 4);
    if (seq == applied) {
        if (memcmp(p, b->written_applied, size)) return false;
        b->written_ack = true;
        return true;
    }
    if (b->written_waiting) return !memcmp(p, b->written_pending, size);
    if (applied == UINT32_MAX || seq != applied + 1) return false;
    memcpy(b->written_pending, p, size);
    b->written_waiting = true;
    b->written_at = b->now;
    return true;
}
bool coop_magic_written(CoopMagic* b, uint8_t recipient, unsigned type, int number, bool vendigroth)
{
    if (!b || !b->host || recipient >= COOP_MAX_PLAYERS) return false;
    WrittenPeer* w = &b->written[recipient];
    int member = coop_campaign_slot_for_owner(coop_session_players(b->session)[recipient].identity);
    const CoopCampaignMember* pc = member < 0 ? NULL : coop_campaign_member((unsigned)member);
    if (!pc || !coop_session_players(b->session)[recipient].connected || w->count == WRITTEN_QUEUE || w->sequence == UINT32_MAX
        || type >= WRITTEN_TYPE_COUNT || number < 0 || number > INT32_MAX - 10000
        || (vendigroth && type != WRITTEN_TYPE_NEWSPAPER && type != WRITTEN_TYPE_SCHEMATIC)) { b->written_failed = true; return false; }
    WrittenEvent* e = &w->queue[(w->head + w->count++) % WRITTEN_QUEUE];
    memset(e, 0, sizeof(*e));
    coop_object_write32(e->bytes, b->epoch);
    coop_object_write32(e->bytes + 4, ++w->sequence);
    coop_object_write32(e->bytes + 8, (uint32_t)map_current_map());
    memcpy(e->bytes + 16, pc->object_id.d.g.data, 16);
    e->bytes[32] = (uint8_t)type;
    e->bytes[33] = vendigroth;
    coop_object_write32(e->bytes + 36, (uint32_t)number);
    e->capture = coop_world_net_request_capture(b->world);
    e->created = b->now;
    return true;
}
bool coop_magic_written_last(const CoopMagic* b, uint8_t recipient, uint8_t out[COOP_WRITTEN_BYTES])
{
    if (!b || !out || recipient >= COOP_MAX_PLAYERS) return false;
    const uint8_t* p = b->host ? b->written[recipient].last : b->written_applied;
    if ((!b->host && recipient != coop_session_local_slot(b->session)) || !coop_object_read32(p + 4)) return false;
    memcpy(out, p, COOP_WRITTEN_BYTES);
    return true;
}
static bool written_poll(CoopMagic* b)
{
    if (b->written_failed) return false;
    if (b->host) {
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            WrittenPeer* w = &b->written[i];
            if (!w->count) continue;
            WrittenEvent* e = &w->queue[w->head];
            if (b->now - e->created > 30000) return false;
            if (e->sent || coop_world_net_captures(b->world) < e->capture) continue;
            coop_object_write32(e->bytes + 12, coop_world_net_revision(b->world));
            if (!i) {
                if (!written_valid(b, e->bytes, sizeof(e->bytes)) || !written_ui_coop_present(e->bytes[32], (int)coop_object_read32(e->bytes + 36), e->bytes[33])) return false;
                w->acknowledged = coop_object_read32(e->bytes + 4);
                memcpy(w->last, e->bytes, WRITTEN_BYTES);
                w->head = (w->head + 1) % WRITTEN_QUEUE;
                w->count--;
            } else e->sent = coop_session_send(b->session, (uint8_t)i, COOP_WRITTEN_VIEW, e->bytes, sizeof(e->bytes));
        }
        return true;
    }
    if (b->written_waiting) {
        if (b->now - b->written_at > 30000) return false;
        const uint8_t* p = b->written_pending;
        if (coop_world_net_applied_revision(b->world) >= coop_object_read32(p + 12)) {
            if (!written_ui_coop_present(p[32], (int)coop_object_read32(p + 36), p[33])) return false;
            memcpy(b->written_applied, p, WRITTEN_BYTES);
            b->written_waiting = false;
            b->written_ack = true;
        }
    }
    if (b->written_ack && coop_session_send(b->session, 0, COOP_WRITTEN_ACK, b->written_applied, 8)) b->written_ack = false;
    return true;
}
CoopMagic* coop_magic_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, uint64_t now, CoopMagicActor actor, void* context)
{
    if (!session || !world || !epoch || !actor) return NULL;
    CoopMagic* b = calloc(1, sizeof(*b));
    if (b) {
        b->session = session;
        b->world = world;
        b->epoch = epoch;
        b->now = now;
        b->actor = actor;
        b->context = context;
        b->host = coop_session_is_host(session);
    }
    return b;
}
void coop_magic_retire(CoopMagic* b)
{
    if (!b || b->retired) return;
    b->retired = true;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) coop_engine_use_release(&b->peers[i].use);
}
void coop_magic_destroy(CoopMagic* b)
{
    if (!b) return;
    coop_magic_retire(b);
    free(b);
}
bool coop_magic_message(CoopMagic* b, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (!b || !p || size < 8 || coop_object_read32(p) != b->epoch) return false;
    if (type == COOP_WRITTEN_VIEW || type == COOP_WRITTEN_ACK) return written_message(b, sender, type, p, size);
    if (b->host) {
        CoopMagicRequest r;
        if (type != COOP_MAGIC_REQUEST || sender >= COOP_MAX_PLAYERS || !coop_session_players(b->session)[sender].connected
            || !coop_magic_request_decode(p, size, &r)) return false;
        MagicPeer* peer = &b->peers[sender];
        if (peer->request.sequence == r.sequence) {
            if (memcmp(peer->bytes, p, size)) return false;
            if (peer->phase == SENT) peer->phase = REPLY;
            return true;
        }
        if (peer->request.sequence == UINT32_MAX || r.sequence != peer->request.sequence + 1
            || (peer->phase && peer->phase != SENT)) return false;
        if (b->now - peer->window >= 1000) {
            peer->window = b->now;
            peer->count = 0;
        }
        if (++peer->count > 30) return false;
        peer->request = r;
        memcpy(peer->bytes, p, size);
        peer->phase = STAGED;
        return true;
    }
    uint32_t sequence = coop_object_read32(p + 4);
    if (sender || type != COOP_MAGIC_RESULT || size != RESULT_BYTES || !sequence || sequence != b->submitted) return false;
    unsigned status = coop_object_read32(p + 8);
    uint32_t revision = coop_object_read32(p + 12);
    if (status > COOP_MAGIC_LINE_OF_SIGHT) return false;
    if (b->received) return status == b->last_status && revision == b->waiting_revision;
    if (!b->pending) return false;
    b->last_status = status;
    b->waiting_revision = revision;
    b->received = true;
    return true;
}
static unsigned execute(CoopMagic* b, unsigned sender, MagicPeer* peer)
{
    int64_t pc = b->actor(b->context, (uint8_t)sender, peer->bytes);
    if (!pc) return COOP_MAGIC_OWNERSHIP;
    return coop_engine_magic_execute(pc, &peer->request, &peer->use);
}
static void present_result(CoopMagic* b)
{
    if (!b->pending || !b->received || coop_world_net_applied_revision(b->world) < b->waiting_revision) return;
    b->pending = false;
    b->last_result = b->submitted;
    static const char* errors[] = { "", "Spell controls are unavailable.", "You cannot control that character.",
        "That is not a valid spell target.", "That target is too far away.", "It is another character's turn.",
        "The turn changed. Try again.", "Finish your current action first.", "That spell cannot be cast now.",
        "Your character does not know that spell.", "Not enough Energy.", "That spell source is unavailable.", "You cannot see the target." };
    static const char* item_errors[] = { "", "Item use is unavailable.", "You cannot control that character.",
        "That is not a valid item target.", "That target is too far away.", "It is another character's turn.",
        "The turn changed. Try again.", "Finish your current action first.", "That item cannot be used now.",
        "That item effect is unavailable.", "Not enough Energy.", "That item is unavailable or belongs to another character.", "You cannot see the target." };
    if (b->last_status) {
        /* Action refusals belong in native message history. A hover string
         * alone is replaced by the previous cached message on full redraw. */
        const char** messages = b->last_operation == COOP_MAGIC_USE_ITEM ? item_errors : errors;
        UiMessage message = { .type = UI_MSG_TYPE_EXCLAMATION, .str = (char*)messages[b->last_status] };
        intgame_message_window_display_msg(&message);
    }
    /* A later receipt must not clear an ability the player just selected. */
}
static void completed(CoopMagic* b, unsigned sender, MagicPeer* peer)
{
    if (peer->status) b->rejected++;
    else b->executed++;
    peer->capture = coop_world_net_request_capture(b->world);
    peer->phase = CAPTURING;
    fprintf(stderr, "ARCMP magic action sender=%u sequence=%u operation=%u spell=%u status=%u\n", sender, peer->request.sequence, peer->request.operation, peer->request.spell, peer->status);
}
bool coop_magic_poll(CoopMagic* b, uint64_t now, bool accepting)
{
    if (coop_world_net_retired(b->world)) return true;
    if (now > b->now) b->now = now;
    if (b->pending && b->now - b->submitted_at > 30000) return false;
    if (b->host)
        for (unsigned sender = 0; sender < COOP_MAX_PLAYERS; sender++) {
            MagicPeer* peer = &b->peers[sender];
            if (peer->phase == STAGED) {
                peer->status = accepting ? execute(b, sender, peer) : COOP_MAGIC_UNAVAILABLE;
                if (coop_world_net_retired(b->world)) return true;
                if (peer->status == COOP_USE_PENDING) {
                    peer->phase = EXECUTING;
                    peer->use_started = b->now;
                } else completed(b, sender, peer);
            }
            if (peer->phase == EXECUTING) {
                peer->status = coop_engine_use_poll(&peer->use, b->now - peer->use_started > 15000);
                if (coop_world_net_retired(b->world)) return true;
                if (peer->status == COOP_USE_FAILURE) return false;
                if (peer->status != COOP_USE_PENDING) completed(b, sender, peer);
            }
            if (peer->phase == CAPTURING && coop_world_net_captures(b->world) >= peer->capture) {
                peer->revision = coop_world_net_revision(b->world);
                peer->phase = REPLY;
            }
            if (peer->phase == REPLY) {
                uint8_t p[RESULT_BYTES];
                coop_object_write32(p, b->epoch);
                coop_object_write32(p + 4, peer->request.sequence);
                coop_object_write32(p + 8, peer->status);
                coop_object_write32(p + 12, peer->revision);
                if (!sender) {
                    b->last_status = peer->status;
                    b->waiting_revision = peer->revision;
                    b->received = true;
                    peer->phase = SENT;
                } else if (coop_session_send(b->session, (uint8_t)sender, COOP_MAGIC_RESULT, p, sizeof(p)))
                    peer->phase = SENT;
            }
        }
    present_result(b);
    return written_poll(b);
}
bool coop_magic_submit(CoopMagic* b, const CoopMagicRequest* request)
{
    if (!b || b->pending || b->submitted == UINT32_MAX || !request) return false;
    CoopMagicRequest r = *request;
    r.epoch = b->epoch;
    r.sequence = b->submitted + 1;
    uint8_t p[COOP_MAGIC_REQUEST_BYTES];
    if (!coop_magic_request_encode(&r, p)) return false;
    uint8_t slot = coop_session_local_slot(b->session);
    if (!(b->host ? coop_magic_message(b, slot, COOP_MAGIC_REQUEST, p, sizeof(p)) : coop_session_send(b->session, 0, COOP_MAGIC_REQUEST, p, sizeof(p)))) return false;
    b->submitted++;
    b->last_operation = r.operation;
    b->pending = true;
    b->received = false;
    b->submitted_at = b->now;
    return true;
}
bool coop_magic_busy(const CoopMagic* b) { return b->pending; }
bool coop_magic_settled(const CoopMagic* b)
{
    if (b->host && !coop_engine_use_settled()) return false;
    if (b->pending || b->written_waiting || b->written_ack || b->written_failed) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if ((b->peers[i].phase && b->peers[i].phase != SENT) || b->written[i].count) return false;
    return true;
}
uint32_t coop_magic_last_result(const CoopMagic* b, unsigned* status)
{
    *status = b->last_status;
    return b->last_result;
}
unsigned coop_magic_executed(const CoopMagic* b) { return b->executed; }
unsigned coop_magic_rejected(const CoopMagic* b) { return b->rejected; }
