#include "coop/followers.h"
#include "coop/engine_followers.h"
#include "coop/engine_object.h"
#include "game/combat.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/player.h"
#include "ui/follower_ui.h"
#include "ui/intgame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { STAGED = 1, EXECUTING, CAPTURING, REPLY, SENT, RESULT_BYTES = 16 };
typedef struct FollowerPeer {
    CoopFollowerRequest request;
    uint8_t bytes[COOP_FOLLOWER_REQUEST_BYTES];
    unsigned phase, status, count;
    uint32_t revision;
    uint64_t window, capture;
} FollowerPeer;
struct CoopFollowers {
    CoopSession* session; CoopWorldNetwork* world; CoopFollowerActor actor; void* context;
    uint32_t epoch, map, submitted, last_result, status, revision;
    uint64_t now, submitted_at;
    FollowerPeer peers[COOP_MAX_PLAYERS];
    unsigned executed, rejected;
    bool host, pending, received;
};
CoopFollowers* coop_followers_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, CoopFollowerActor actor, void* context)
{
    if (!session || !world || !epoch || !actor) return NULL;
    CoopFollowers* s = calloc(1, sizeof(*s));
    if (s) { s->session = session; s->world = world; s->epoch = epoch; s->map = (uint32_t)map_current_map();
        s->actor = actor; s->context = context; s->host = coop_session_is_host(session); }
    return s;
}
void coop_followers_destroy(CoopFollowers* s) { free(s); }
bool coop_followers_message(CoopFollowers* s, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    if (!s || !bytes || size < 8 || coop_object_read32(bytes) != s->epoch) return false;
    if (s->host) {
        CoopFollowerRequest r;
        if (sender >= COOP_MAX_PLAYERS || !coop_session_players(s->session)[sender].connected || type != COOP_FOLLOWER_REQUEST
            || !coop_follower_decode(bytes, size, &r)) return false;
        FollowerPeer* peer = &s->peers[sender];
        if (r.sequence == peer->request.sequence) {
            if (memcmp(bytes, peer->bytes, size)) return false;
            if (peer->phase == SENT) peer->phase = REPLY;
            return true;
        }
        if (peer->request.sequence == UINT32_MAX || r.sequence != peer->request.sequence + 1 || (peer->phase && peer->phase != SENT)) return false;
        if (s->now - peer->window >= 1000) { peer->window = s->now; peer->count = 0; }
        if (++peer->count > 30) return false;
        peer->request = r; memcpy(peer->bytes, bytes, size); peer->phase = STAGED; return true;
    }
    if (sender || type != COOP_FOLLOWER_RESULT || size != RESULT_BYTES || !s->submitted || coop_object_read32(bytes + 4) != s->submitted) return false;
    uint32_t status = coop_object_read32(bytes + 8), revision = coop_object_read32(bytes + 12);
    if (status > COOP_FOLLOW_BUSY || !revision) return false;
    if (s->received) return s->status == status && s->revision == revision;
    s->received = true; s->status = status; s->revision = revision; return true;
}
bool coop_followers_submit(CoopFollowers* s, const CoopFollowerRequest* request)
{
    if (!s || !request || s->pending || s->submitted == UINT32_MAX) return false;
    CoopFollowerRequest r = *request; r.epoch = s->epoch; r.sequence = s->submitted + 1;
    uint8_t bytes[COOP_FOLLOWER_REQUEST_BYTES]; if (!coop_follower_encode(&r, bytes)) return false;
    uint8_t slot = coop_session_local_slot(s->session);
    if (!(s->host ? coop_followers_message(s, slot, COOP_FOLLOWER_REQUEST, bytes, sizeof(bytes)) : coop_session_send(s->session, 0, COOP_FOLLOWER_REQUEST, bytes, sizeof(bytes)))) return false;
    s->submitted++; s->pending = true; s->received = false; s->submitted_at = s->now; return true;
}
bool coop_followers_order(CoopFollowers* s, int64_t follower, CoopFollowerOrder order, int64_t target, int64_t location, bool force)
{
    if (!s || !follower) return false;
    int64_t actor = player_get_local_pc_obj(); if (!actor) return false;
    CoopFollowerRequest r = { .map = (uint32_t)map_current_map(), .order = order, .force = force, .generation = combat_turn_generation_get() };
    memcpy(r.actor, obj_get_id(actor).d.g.data, 16);
    if (!coop_object_id_from_engine(obj_get_id(follower), &r.follower) || (target && !coop_object_id_from_engine(obj_get_id(target), &r.target))) return false;
    if (order == COOP_FOLLOW_WALK) { r.x = (uint32_t)location_get_x(location); r.y = (uint32_t)location_get_y(location); }
    return coop_followers_submit(s, &r);
}
bool coop_followers_broadcast(CoopFollowers* s, const char* command, int64_t target, int64_t location)
{
    int64_t actor = player_get_local_pc_obj();
    if (!s || !actor || !command || strlen(command) >= 128) return false;
    CoopFollowerRequest r = { .map = (uint32_t)map_current_map(), .order = COOP_FOLLOW_BROADCAST,
        .generation = combat_turn_generation_get(), .x = (uint32_t)location_get_x(location), .y = (uint32_t)location_get_y(location) };
    memcpy(r.actor, obj_get_id(actor).d.g.data, 16); memcpy(r.command, command, strlen(command));
    if (target && !coop_object_id_from_engine(obj_get_id(target), &r.target)) return false;
    return coop_followers_submit(s, &r);
}
bool coop_followers_poll(CoopFollowers* s, uint64_t now, bool accepting)
{
    if (coop_world_net_retired(s->world)) return true;
    if (!s) return false;
    if (now > s->now) s->now = now;
    if (s->pending && now - s->submitted_at > 30000) return false;
    if (s->host) for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        FollowerPeer* peer = &s->peers[i];
        if (peer->phase == STAGED) {
            peer->phase = EXECUTING;
            int64_t actor = s->actor(s->context, (uint8_t)i, peer->bytes);
            peer->status = !accepting ? COOP_FOLLOW_UNAVAILABLE : !actor || peer->request.map != s->map ? COOP_FOLLOW_OWNER : coop_engine_follower_order(actor, &peer->request);
            if (coop_world_net_retired(s->world)) return true;
            if (peer->status) s->rejected++; else s->executed++;
            peer->capture = coop_world_net_request_capture(s->world); peer->phase = CAPTURING;
            fprintf(stderr, "ARCMP follower sender=%u sequence=%u order=%u status=%u\n", i, peer->request.sequence, peer->request.order, peer->status);
        }
        if (peer->phase == CAPTURING && coop_world_net_captures(s->world) >= peer->capture) {
            peer->revision = coop_world_net_revision(s->world); peer->phase = REPLY;
        }
        if (peer->phase == REPLY) {
            uint8_t bytes[RESULT_BYTES]; coop_object_write32(bytes, s->epoch); coop_object_write32(bytes + 4, peer->request.sequence);
            coop_object_write32(bytes + 8, peer->status); coop_object_write32(bytes + 12, peer->revision);
            if (!i) { s->received = true; s->status = peer->status; s->revision = peer->revision; peer->phase = SENT; }
            else if (coop_session_send(s->session, (uint8_t)i, COOP_FOLLOWER_RESULT, bytes, sizeof(bytes))) peer->phase = SENT;
        }
    }
    if (s->pending && s->received && coop_world_net_applied_revision(s->world) >= s->revision) {
        s->pending = false; s->last_result = s->submitted; follower_ui_refresh();
        static char* feedback[] = { "Companion order accepted.", "Companion orders are unavailable now.", "That companion belongs to another traveller.",
            "That companion cannot take orders now.", "Move closer to your companion.", "Choose a reachable destination or valid enemy.",
            "Wait for your companion's turn.", "The combat turn changed. Try again.", "Your companion refused that order.", "Your companion is busy." };
        UiMessage message = { .type = s->status ? UI_MSG_TYPE_EXCLAMATION : UI_MSG_TYPE_FEEDBACK, .str = feedback[s->status] };
        intgame_message_window_display_msg(&message);
    }
    return true;
}
bool coop_followers_settled(const CoopFollowers* s)
{
    if (!s || s->pending) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (s->peers[i].phase && s->peers[i].phase != SENT) return false;
    return true;
}
bool coop_followers_busy(const CoopFollowers* s) { return s && s->pending; }
uint32_t coop_followers_last_result(const CoopFollowers* s, unsigned* status) { *status = s->status; return s->last_result; }
unsigned coop_followers_executed(const CoopFollowers* s) { return s->executed; }
unsigned coop_followers_rejected(const CoopFollowers* s) { return s->rejected; }
