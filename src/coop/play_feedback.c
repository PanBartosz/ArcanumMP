#include "coop/play_feedback.h"
#include "coop/modern.h"
#include "coop/authority.h"
#include "coop/display.h"
#include "coop/engine_object.h"
#include "coop/localization.h"
#include "coop/runtime.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/player.h"
#include "game/tf.h"
#include <stdio.h>
#include <string.h>

/* Transient feedback has its own lifetime. Saved combat history must never
 * replay old status text or shake a player's screen when loading/rejoining. */
enum { CAPACITY = 128, BYTES = 128, LIFETIME = 2500 };
typedef struct Feedback {
    uint32_t sequence, map;
    uint64_t expires;
    CoopObjectId actor;
    uint8_t kind, active;
    bool shown;
    char status[92];
} Feedback;
static Feedback events[CAPACITY];
static uint32_t first = 1, next = 1, cursors[4], received;
static CoopSession* connection;
static uint32_t epoch;
static bool drawing;

void coop_feedback_reset(void)
{
    memset(events, 0, sizeof(events));
    memset(cursors, 0, sizeof(cursors));
    first = next = 1; received = 0; connection = NULL; epoch = 0;
    coop_view_effect_reset();
}
static void show(Feedback* event)
{
    if (event->shown || event->map != (uint32_t)map_current_map()) return;
    int64_t actor = obj_pool_perm_lookup(coop_object_id_to_engine(event->actor));
    if (!actor || !obj_handle_is_valid(actor)) return;
    event->shown = true;
    if (event->kind) {
        if (actor == player_get_local_pc_obj()) coop_view_critical_shake();
    } else {
        char text[180];
        snprintf(text, sizeof(text), coop_tr(event->active ? "%s" : "%s ended"), coop_tr(event->status));
        drawing = true;
        tf_add_local(actor, event->active ? TF_TYPE_YELLOW : TF_TYPE_GREEN, text);
        drawing = false;
    }
}
static void emit(int64_t actor, const char* status, unsigned kind, bool active)
{
    if (drawing || !coop_authority_can_simulate() || !actor || !obj_handle_is_valid(actor)
        || next == UINT32_MAX || (coop_runtime_session() && coop_runtime_phase() != COOP_RUNTIME_PLAYING)) return;
    Feedback event = { .sequence = next, .map = (uint32_t)map_current_map(),
        .expires = SDL_GetTicks() + LIFETIME, .kind = (uint8_t)kind, .active = active };
    if (!coop_object_id_from_engine(obj_get_id(actor), &event.actor)) return;
    snprintf(event.status, sizeof(event.status), "%s", status ? status : "");
    events[next++ % CAPACITY] = event;
    if (next - first > CAPACITY) first = next - CAPACITY;
    show(&events[event.sequence % CAPACITY]);
}
void coop_play_status_feedback(int64_t actor, const char* status, bool active)
{ if (status && *status) emit(actor, status, 0, active); }
void coop_play_critical_feedback(int64_t target)
{ emit(target, NULL, 1, false); }
void coop_feedback_tick(uint64_t now)
{
    for (uint32_t n = first; n < next; n++) {
        Feedback* event = &events[n % CAPACITY];
        if (!event->shown && now < event->expires) show(event);
    }
    coop_view_effect_tick(now);
}
void coop_feedback_network(CoopSession* session, uint32_t generation, unsigned ready)
{
    if (connection != session || epoch != generation) {
        if (connection) coop_feedback_reset();
        connection = session; epoch = generation;
        /* New epochs do not replay transients from the replaced world. */
        for (unsigned i = 0; i < 4; i++) cursors[i] = first;
    }
    if (!coop_session_is_host(session)) return;
    uint64_t now = SDL_GetTicks();
    for (unsigned i = 1; i < 4; i++) if (ready & (1u << i)) {
        uint32_t n = SDL_max(cursors[i], first);
        for (unsigned budget = 0; n < next && budget < 16; n++, budget++) {
            Feedback* event = &events[n % CAPACITY];
            if (now >= event->expires) continue;
            uint8_t bytes[BYTES] = { 0 };
            coop_object_write32(bytes, generation); coop_object_write32(bytes + 4, n);
            coop_object_write32(bytes + 8, event->map); memcpy(bytes + 12, event->actor.bytes, 17);
            bytes[29] = event->kind; bytes[30] = event->active;
            coop_object_write32(bytes + 32, (uint32_t)(event->expires - now));
            memcpy(bytes + 36, event->status, sizeof(event->status));
            if (!coop_session_send(session, (uint8_t)i, COOP_PLAY_FEEDBACK, bytes, sizeof(bytes))) break;
        }
        cursors[i] = n;
    }
}
bool coop_feedback_receive(uint8_t sender, const uint8_t* bytes, size_t size)
{
    if (sender || size != BYTES || bytes[29] > 1 || bytes[30] > 1 || bytes[31]
        || !memchr(bytes + 36, 0, 92)) return false;
    uint32_t sequence = coop_object_read32(bytes + 4), remaining = coop_object_read32(bytes + 32);
    if (!sequence || sequence == UINT32_MAX || remaining > LIFETIME) return false;
    Feedback event = { .sequence = sequence, .map = coop_object_read32(bytes + 8),
        .expires = SDL_GetTicks() + remaining, .kind = bytes[29], .active = bytes[30] };
    memcpy(event.actor.bytes, bytes + 12, 17); memcpy(event.status, bytes + 36, 92);
    if (!coop_object_id_valid(&event.actor, false) || (event.actor.bytes[0] != 2 && event.actor.bytes[0] != 3)) return false;
    if (sequence <= received) return true;
    received = sequence;
    /* Host and client sequence spaces may start after discarded old packets. */
    if (sequence >= next) next = sequence + 1;
    if (next - first > CAPACITY) first = next - CAPACITY;
    events[sequence % CAPACITY] = event;
    if (remaining) show(&events[sequence % CAPACITY]);
    return true;
}
