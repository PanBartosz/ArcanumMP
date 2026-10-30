#include "coop/audio.h"
#include "coop/object_state.h"
#include <stdlib.h>
#include <string.h>

struct CoopAudio {
    CoopSession* session;
    CoopAudioApply apply;
    void* context;
    bool host, sealed, failed, applied, ack_due;
    uint32_t epoch, map, revision;
    uint32_t digest;
    uint64_t now, progress, events;
    unsigned head, count;
    CoopAudioEvent queue[COOP_AUDIO_QUEUE_EVENTS];
    uint8_t packet[COOP_MESSAGE_CAPACITY], previous[COOP_MESSAGE_CAPACITY];
    size_t size, previous_size;
    uint32_t acknowledged[COOP_MAX_PLAYERS];
    bool sent[COOP_MAX_PLAYERS];
};
static bool valid(const CoopAudioEvent* e)
{
    if (!e || !e->token || e->operation > COOP_AUDIO_STOP || e->size > 3 || e->sound > INT32_MAX || e->fade > 60000) return false;
    if (e->operation == COOP_AUDIO_START) return !e->fade;
    if (e->sound || e->loops || e->size) return false;
    return e->operation == COOP_AUDIO_MOVE ? !e->fade : !e->x && !e->y;
}
bool coop_audio_event_encode(const CoopAudioEvent* e, uint8_t out[COOP_AUDIO_EVENT_BYTES])
{
    if (!out || !valid(e)) return false;
    out[0] = e->operation; out[1] = e->size;
    out[2] = (uint8_t)(e->loops >> 8); out[3] = (uint8_t)e->loops;
    coop_object_write32(out + 4, e->sound); coop_object_write64(out + 8, e->token);
    coop_object_write32(out + 16, e->x); coop_object_write32(out + 20, e->y); coop_object_write32(out + 24, e->fade);
    return true;
}
bool coop_audio_event_decode(const void* bytes, size_t size, CoopAudioEvent* out)
{
    if (!bytes || !out || size != COOP_AUDIO_EVENT_BYTES) return false;
    const uint8_t* p = bytes;
    CoopAudioEvent e = { .operation = p[0], .size = p[1], .loops = (uint16_t)((p[2] << 8) | p[3]),
        .sound = coop_object_read32(p + 4), .token = coop_object_read64(p + 8),
        .x = coop_object_read32(p + 16), .y = coop_object_read32(p + 20), .fade = coop_object_read32(p + 24) };
    if (!valid(&e)) return false;
    *out = e;
    return true;
}
CoopAudio* coop_audio_create(CoopSession* session, uint32_t epoch, uint32_t map, CoopAudioApply apply, void* context)
{
    if (!session || !epoch || !apply) return NULL;
    CoopAudio* a = calloc(1, sizeof(*a));
    if (a) {
        a->session = session; a->epoch = epoch; a->map = map;
        a->host = coop_session_is_host(session); a->apply = apply; a->context = context;
        a->digest = UINT32_C(2166136261);
    }
    return a;
}
void coop_audio_destroy(CoopAudio* a) { free(a); }
bool coop_audio_emit(CoopAudio* a, const CoopAudioEvent* e)
{
    if (!a || !a->host || a->sealed || a->failed || !valid(e)) return false;
    /* An unsent move can be superseded until a later start/stop for that token.
     * Published batches stay immutable and keep their exact replay identity. */
    if (e->operation == COOP_AUDIO_MOVE) {
        for (unsigned i = a->count; i > 0; i--) {
            CoopAudioEvent* old = &a->queue[(a->head + i - 1) % COOP_AUDIO_QUEUE_EVENTS];
            if (old->token != e->token) continue;
            if (old->operation == COOP_AUDIO_MOVE) { *old = *e; return true; }
            break;
        }
    }
    if (a->count == COOP_AUDIO_QUEUE_EVENTS) { a->failed = true; return false; }
    a->queue[(a->head + a->count++) % COOP_AUDIO_QUEUE_EVENTS] = *e;
    return true;
}
static void account(CoopAudio* a)
{
    for (size_t i = COOP_AUDIO_HEADER_BYTES; i < a->size; i++)
        a->digest = (a->digest ^ a->packet[i]) * UINT32_C(16777619);
    a->events += (a->size - COOP_AUDIO_HEADER_BYTES) / COOP_AUDIO_EVENT_BYTES;
}
static bool decode_packet(CoopAudio* a, const uint8_t* p, size_t size, CoopAudioEvent* events, unsigned* count)
{
    if (size < COOP_AUDIO_HEADER_BYTES || size > COOP_MESSAGE_CAPACITY || coop_object_read32(p) != a->epoch
        || coop_object_read32(p + 8) != a->map || p[14] || p[15]) return false;
    unsigned n = (unsigned)((p[12] << 8) | p[13]);
    if (!n || n > COOP_AUDIO_BATCH_EVENTS || size != COOP_AUDIO_HEADER_BYTES + n * COOP_AUDIO_EVENT_BYTES) return false;
    for (unsigned i = 0; i < n; i++)
        if (!coop_audio_event_decode(p + COOP_AUDIO_HEADER_BYTES + i * COOP_AUDIO_EVENT_BYTES, COOP_AUDIO_EVENT_BYTES, &events[i])) return false;
    *count = n;
    return true;
}
bool coop_audio_message(CoopAudio* a, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (!a || !p || a->failed || size < 8 || coop_object_read32(p) != a->epoch) return false;
    uint32_t revision = coop_object_read32(p + 4);
    if (a->host) {
        if (!sender || sender >= COOP_MAX_PLAYERS || type != COOP_AUDIO_ACK || size != 8 || !revision) return false;
        if (a->acknowledged[sender] == revision) return true;
        if (!a->size || revision != a->revision || !a->sent[sender]) return false;
        a->acknowledged[sender] = revision; a->progress = a->now;
        return true;
    }
    if (sender || type != COOP_AUDIO_BATCH || !revision) return false;
    if (a->previous_size && revision == a->revision) {
        if (size != a->previous_size || memcmp(p, a->previous, size)) return false;
        a->ack_due = true;
        return true;
    }
    if (a->size) return size == a->size && !memcmp(p, a->packet, size);
    if (a->revision == UINT32_MAX || revision != a->revision + 1) return false;
    CoopAudioEvent events[COOP_AUDIO_BATCH_EVENTS]; unsigned count;
    if (!decode_packet(a, p, size, events, &count)) return false;
    memcpy(a->packet, p, size); a->size = size; a->progress = a->now; a->applied = false;
    return true;
}
bool coop_audio_poll(CoopAudio* a, uint64_t now)
{
    if (!a || a->failed) return false;
    if (now > a->now) a->now = now;
    if (a->size && a->now - a->progress > 30000) return false;
    if (!a->host) {
        if (a->size && !a->applied) {
            CoopAudioEvent events[COOP_AUDIO_BATCH_EVENTS]; unsigned count;
            if (!decode_packet(a, a->packet, a->size, events, &count) || !a->apply(a->context, events, count)) return false;
            a->applied = true; account(a);
            a->revision = coop_object_read32(a->packet + 4);
            memcpy(a->previous, a->packet, a->size); a->previous_size = a->size;
            a->ack_due = true;
        }
        if (a->ack_due) {
            uint8_t ack[8]; coop_object_write32(ack, a->epoch); coop_object_write32(ack + 4, a->revision);
            if (coop_session_send(a->session, 0, COOP_AUDIO_ACK, ack, sizeof(ack))) {
                a->ack_due = false;
                if (a->applied) { a->size = 0; a->applied = false; }
            }
        }
        return true;
    }
    const CoopPlayer* players = coop_session_players(a->session);
    if (a->size) {
        bool complete = true;
        for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++) {
            if (!players[i].connected) continue;
            if (!a->sent[i]) a->sent[i] = coop_session_send(a->session, i, COOP_AUDIO_BATCH, a->packet, a->size);
            complete &= a->acknowledged[i] == a->revision;
        }
        if (!complete) return true;
        account(a);
        a->size = 0;
    }
    if (!a->count) return true;
    if (a->revision == UINT32_MAX) return false;
    a->revision++;
    coop_object_write32(a->packet, a->epoch); coop_object_write32(a->packet + 4, a->revision); coop_object_write32(a->packet + 8, a->map);
    unsigned count = a->count > COOP_AUDIO_BATCH_EVENTS ? COOP_AUDIO_BATCH_EVENTS : a->count;
    a->packet[12] = (uint8_t)(count >> 8); a->packet[13] = (uint8_t)count; a->packet[14] = a->packet[15] = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!coop_audio_event_encode(&a->queue[a->head], a->packet + COOP_AUDIO_HEADER_BYTES + i * COOP_AUDIO_EVENT_BYTES)) return false;
        a->head = (a->head + 1) % COOP_AUDIO_QUEUE_EVENTS; a->count--;
    }
    a->size = COOP_AUDIO_HEADER_BYTES + count * COOP_AUDIO_EVENT_BYTES;
    memset(a->sent, 0, sizeof(a->sent)); a->progress = a->now;
    return true;
}
void coop_audio_seal(CoopAudio* a) { if (a) a->sealed = true; }
bool coop_audio_settled(const CoopAudio* a) { return a && a->sealed && !a->failed && !a->size && !a->count; }
uint64_t coop_audio_events(const CoopAudio* a) { return a ? a->events : 0; }
uint32_t coop_audio_digest(const CoopAudio* a) { return a ? a->digest : 0; }
