#include "coop/services.h"
#include "coop/engine_service.h"
#include "coop/engine_craft.h"
#include "coop/engine_object.h"
#include "coop/engine_dialogue.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/player.h"
#include "ui/dialog_ui.h"
#include "ui/inven_ui.h"
#include "ui/schematic_ui.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { STAGED = 1, EXECUTING, CAPTURING, REPLY, SENT, RESULT_BYTES = 24 };
typedef struct ServicePeer {
    uint8_t bytes[COOP_SERVICE_REQUEST_BYTES]; CoopServiceRequest request;
    CoopServiceQuote quoted;
    CoopObjectId item;
    uint32_t conversation, page, token, serial, revision, result_token;
    unsigned phase, status, count; int price;
    uint64_t window, capture;
} ServicePeer;
struct CoopServices {
    CoopSession* session; CoopWorldNetwork* world; CoopDialogue* dialogue;
    CoopServiceActor actor; void* context;
    ServicePeer peers[COOP_MAX_PLAYERS];
    uint32_t epoch, map, submitted, last_result, revision, token, received_token, quote_conversation, quote_page;
    unsigned status, executed, rejected, quantity; int price, received_price;
    uint64_t now, submitted_at, hover_at;
    bool host, pending, received, close_requested, close_sent, quote_attempted;
    CoopObjectId wanted;
    CoopServiceRequest submitted_request;
};
CoopServices* coop_services_create(CoopSession* session, CoopWorldNetwork* world, CoopDialogue* dialogue, uint32_t epoch, uint64_t now, CoopServiceActor actor, void* context)
{
    if (!session || !world || !dialogue || !epoch || !actor) return NULL;
    CoopServices* s = calloc(1, sizeof(*s));
    if (s) { s->session = session; s->world = world; s->dialogue = dialogue; s->epoch = epoch; s->now = now; s->actor = actor; s->context = context;
        s->map = (uint32_t)map_current_map(); s->host = coop_session_is_host(session); }
    return s;
}
void coop_services_destroy(CoopServices* s) { free(s); }
bool coop_services_message(CoopServices* s, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (!s || !p || size < 8 || coop_object_read32(p) != s->epoch) return false;
    if (s->host) {
        CoopServiceRequest r;
        if (sender >= COOP_MAX_PLAYERS || !coop_session_players(s->session)[sender].connected || type != COOP_SERVICE_REQUEST
            || !coop_service_request_decode(p, size, &r)) return false;
        ServicePeer* peer = &s->peers[sender];
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
    if (sender || type != COOP_SERVICE_RESULT || size != RESULT_BYTES || !s->submitted || coop_object_read32(p + 4) != s->submitted) return false;
    uint32_t status = coop_object_read32(p + 8), revision = coop_object_read32(p + 12), token = coop_object_read32(p + 16), price = coop_object_read32(p + 20);
    if (status > COOP_SERVICE_KNOWLEDGE || !revision || price > INT_MAX || (token ? status != 0 : price != 0)) return false;
    if (s->received) return status == s->status && revision == s->revision && token == s->received_token && (int)price == s->received_price;
    s->received = true; s->status = status; s->revision = revision; s->received_token = token; s->received_price = (int)price;
    return true;
}
static unsigned execute(CoopServices* s, ServicePeer* peer, unsigned sender, bool accepting)
{
    const CoopServiceRequest* r = &peer->request;
    peer->result_token = 0; peer->price = 0;
    if (!accepting) return COOP_SERVICE_UNAVAILABLE;
    int64_t pc = s->actor(s->context, (uint8_t)sender, peer->bytes);
    if (!pc || r->map != s->map) return COOP_SERVICE_OWNERSHIP;
    const CoopDialogueView* v = coop_dialogue_current(s->dialogue);
    if (r->operation == COOP_SERVICE_CRAFT) {
        int64_t crafter = obj_pool_perm_lookup(coop_object_id_to_engine(r->item));
        if (crafter == pc) {
            if (v->active || r->conversation || r->page) return COOP_SERVICE_CONVERSATION;
        } else if (!v->active || v->mode != 7 || v->chooser != sender || r->conversation != v->conversation || r->page != v->revision
            || memcmp(v->pc_guid, r->actor, 16) || !crafter || !objid_is_equal(obj_get_id(crafter), coop_object_id_to_engine(v->npc))
            || !dialog_ui_coop_service_valid(pc, crafter, 7)) return COOP_SERVICE_CONVERSATION;
        peer->token = 0;
        unsigned status = coop_engine_craft(pc, crafter, r->recipe);
        if (!status) s->executed++;
        return status;
    }
    int64_t npc = obj_pool_perm_lookup(coop_object_id_to_engine(v->npc));
    if (!v->active || (v->mode != 3 && v->mode != 8 && v->mode != 9) || v->chooser != sender || r->conversation != v->conversation || r->page != v->revision
        || memcmp(v->pc_guid, r->actor, 16) || !npc || !dialog_ui_coop_service_valid(pc, npc, v->mode)) return COOP_SERVICE_CONVERSATION;
    int64_t item = obj_pool_perm_lookup(coop_object_id_to_engine(r->item));
    if (!item) return COOP_SERVICE_TARGET;
    if (r->operation == COOP_SERVICE_QUOTE) {
        peer->token = 0;
        CoopServiceQuote q;
        unsigned status = coop_engine_service_quote_quantity(pc, npc, item, v->mode, r->quantity, &q);
        if (status) return status;
        if (peer->serial == UINT32_MAX) return COOP_SERVICE_LIMIT;
        peer->quoted = q; peer->item = r->item; peer->conversation = v->conversation; peer->page = v->revision;
        peer->result_token = peer->token = ++peer->serial; peer->price = q.price;
        return COOP_SERVICE_OK;
    }
    if (!peer->token || r->quote != peer->token || peer->conversation != v->conversation || peer->page != v->revision
        || memcmp(peer->item.bytes, r->item.bytes, 17) || peer->quoted.quantity != r->quantity) return COOP_SERVICE_CHANGED;
    /* Retire before any native script callback, including a rejected purchase. */
    peer->token = 0;
    unsigned status = coop_engine_service_buy(pc, npc, item, &peer->quoted);
    if (!status) s->executed++;
    return status;
}
bool coop_services_submit(CoopServices* s, const CoopServiceRequest* request)
{
    if (!s || !request || s->pending || s->submitted == UINT32_MAX) return false;
    CoopServiceRequest r = *request; r.epoch = s->epoch; r.sequence = s->submitted + 1;
    uint8_t bytes[COOP_SERVICE_REQUEST_BYTES];
    if (!coop_service_request_encode(&r, bytes)) return false;
    uint8_t slot = coop_session_local_slot(s->session);
    if (!(s->host ? coop_services_message(s, slot, COOP_SERVICE_REQUEST, bytes, sizeof(bytes)) : coop_session_send(s->session, 0, COOP_SERVICE_REQUEST, bytes, sizeof(bytes)))) return false;
    s->submitted++; s->pending = true; s->received = false; s->submitted_at = s->now; s->submitted_request = r;
    return true;
}
static bool local(CoopServices* s, CoopServiceOperation op, CoopObjectId item, uint32_t token)
{
    const CoopDialogueView* v = coop_dialogue_current(s->dialogue);
    int64_t pc = player_get_local_pc_obj();
    if (!pc || !v->active || (v->mode != 3 && v->mode != 8 && v->mode != 9) || v->chooser != coop_session_local_slot(s->session) || s->close_requested) return false;
    CoopServiceRequest r = { .map = s->map, .conversation = v->conversation, .page = v->revision, .operation = op, .item = item, .quote = token, .quantity = s->quantity };
    memcpy(r.actor, obj_get_id(pc).d.g.data, 16);
    bool sent = coop_services_submit(s, &r);
    if (sent && op == COOP_SERVICE_QUOTE) s->quote_attempted = true;
    return sent;
}
void coop_services_hover(CoopServices* s, int64_t item)
{
    coop_services_hover_quantity(s, item, 1);
}
void coop_services_hover_quantity(CoopServices* s, int64_t item, unsigned quantity)
{
    if (!s || !quantity || quantity > 1000000) return;
    CoopObjectId id = { 0 };
    if (item && !coop_object_id_from_engine(obj_get_id(item), &id)) return;
    if (!memcmp(id.bytes, s->wanted.bytes, 17) && quantity == s->quantity) return;
    s->wanted = id; s->quantity = quantity; s->token = 0; s->price = 0; s->quote_attempted = false;
    inven_ui_coop_service_feedback(COOP_SERVICE_UNAVAILABLE, 0, false);
}
bool coop_services_buy(CoopServices* s, int64_t item)
{
    return coop_services_buy_quantity(s, item, 1);
}
bool coop_services_buy_quantity(CoopServices* s, int64_t item, unsigned quantity)
{
    if (!s || !item || !quantity || quantity > 1000000) return false;
    CoopObjectId id; if (!coop_object_id_from_engine(obj_get_id(item), &id)) return false;
    const CoopDialogueView* v = coop_dialogue_current(s->dialogue);
    if (!s->pending && s->token && quantity == s->quantity && !memcmp(s->wanted.bytes, id.bytes, 17) && s->quote_conversation == v->conversation && s->quote_page == v->revision)
        return local(s, COOP_SERVICE_BUY, id, s->token);
    coop_services_hover_quantity(s, item, quantity);
    if (!s->pending) s->quote_attempted = false;
    inven_ui_coop_service_feedback(COOP_SERVICE_UNAVAILABLE, 0, false);
    return false;
}
bool coop_services_craft(CoopServices* s, int64_t crafter, unsigned recipe)
{
    if (!s || !crafter || !recipe || s->close_requested) return false;
    int64_t pc = player_get_local_pc_obj();
    const CoopDialogueView* v = coop_dialogue_current(s->dialogue);
    CoopServiceRequest r = { .map = s->map, .operation = COOP_SERVICE_CRAFT, .quantity = 1, .recipe = recipe };
    if (!pc || !coop_object_id_from_engine(obj_get_id(crafter), &r.item)) return false;
    if (crafter != pc) {
        if (!v->active || v->mode != 7 || v->chooser != coop_session_local_slot(s->session)) return false;
        r.conversation = v->conversation; r.page = v->revision;
    } else if (v->active) return false;
    memcpy(r.actor, obj_get_id(pc).d.g.data, 16);
    return coop_services_submit(s, &r);
}
void coop_services_close(CoopServices* s)
{
    if (s) { s->close_requested = true; s->close_sent = false; s->wanted = (CoopObjectId) { 0 }; s->token = 0; }
}
bool coop_services_poll(CoopServices* s, uint64_t now, bool accepting)
{
    if (coop_world_net_retired(s->world)) return true;
    if (now > s->now) s->now = now;
    if (s->pending && s->now - s->submitted_at > 30000) return false;
    if (s->host) for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        ServicePeer* p = &s->peers[i];
        if (p->phase == STAGED) {
            p->phase = EXECUTING; p->status = execute(s, p, i, accepting);
            if (coop_world_net_retired(s->world)) return true;
            if (p->status) s->rejected++;
            p->capture = coop_world_net_request_capture(s->world); p->phase = CAPTURING;
            fprintf(stderr, "ARCMP service sender=%u sequence=%u operation=%u status=%u quote=%u price=%d\n", i, p->request.sequence, p->request.operation, p->status, p->result_token, p->price);
        }
        if (p->phase == CAPTURING && coop_world_net_captures(s->world) >= p->capture) { p->revision = coop_world_net_revision(s->world); p->phase = REPLY; }
        if (p->phase == REPLY) {
            uint8_t bytes[RESULT_BYTES]; coop_object_write32(bytes, s->epoch); coop_object_write32(bytes + 4, p->request.sequence);
            coop_object_write32(bytes + 8, p->status); coop_object_write32(bytes + 12, p->revision); coop_object_write32(bytes + 16, p->result_token); coop_object_write32(bytes + 20, (uint32_t)p->price);
            if (!i) { s->received = true; s->status = p->status; s->revision = p->revision; s->received_token = p->result_token; s->received_price = p->price; p->phase = SENT; }
            else if (coop_session_send(s->session, (uint8_t)i, COOP_SERVICE_RESULT, bytes, sizeof(bytes))) p->phase = SENT;
        }
    }
    const CoopDialogueView* v = coop_dialogue_current(s->dialogue);
    if (s->pending && s->received && coop_world_net_applied_revision(s->world) >= s->revision) {
        s->pending = false; s->last_result = s->submitted; s->token = 0; s->price = 0;
        const CoopServiceRequest* r = &s->submitted_request;
        if (r->operation == COOP_SERVICE_CRAFT) schematic_ui_coop_feedback(s->status);
        else if (v->active && v->conversation == r->conversation && v->revision == r->page && !s->close_requested
            && r->quantity == s->quantity && !memcmp(s->wanted.bytes, r->item.bytes, 17)) {
            s->token = s->received_token; s->price = s->received_price; s->quote_conversation = v->conversation; s->quote_page = v->revision;
            inven_ui_coop_service_feedback(s->status, s->price, r->operation == COOP_SERVICE_BUY);
            if (r->operation == COOP_SERVICE_BUY) { s->wanted = (CoopObjectId) { 0 }; s->quote_attempted = false; }
        }
    }
    if (!v->active || (v->mode != 3 && v->mode != 5 && v->mode != 6 && v->mode != 7 && v->mode != 8 && v->mode != 9)) {
        s->close_requested = s->close_sent = false;
        s->wanted = (CoopObjectId) { 0 };
        s->token = 0;
    }
    /* Closing an existing panel must remain possible while arrival events wait
     * for that conversation. New service mutations still require admission. */
    if (s->close_requested && !s->close_sent && !s->pending) s->close_sent = coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
    if (s->token && (s->quote_conversation != v->conversation || s->quote_page != v->revision)) { s->token = 0; s->quote_attempted = false; }
    if (accepting && !s->pending && !s->close_requested && s->wanted.bytes[0] && !s->token && !s->quote_attempted && s->now - s->hover_at >= 200) {
        /* Failed quotes remain until a new hover/click instead of retrying at
         * the world cadence. A different wanted item gets its own quote. */
        if (local(s, COOP_SERVICE_QUOTE, s->wanted, 0)) s->hover_at = s->now;
    }
    return true;
}
bool coop_services_settled(const CoopServices* s)
{
    if (s->pending) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (s->peers[i].phase && s->peers[i].phase != SENT) return false;
    return true;
}
bool coop_services_busy(const CoopServices* s) { return s->pending; }
uint32_t coop_services_last_result(const CoopServices* s, unsigned* status) { *status = s->status; return s->last_result; }
uint32_t coop_services_quote(const CoopServices* s, int* price) { *price = s->price; return s->token; }
unsigned coop_services_executed(const CoopServices* s) { return s->executed; }
unsigned coop_services_rejected(const CoopServices* s) { return s->rejected; }
