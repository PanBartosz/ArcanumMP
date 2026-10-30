#include "coop/bootstrap.h"
#include <stdlib.h>
#include <string.h>

enum { CHUNK_DATA = COOP_MESSAGE_CAPACITY - 8,
    CHUNKS_PER_POLL = 8,
    PROGRESS_TIMEOUT = 30000 };
typedef struct Sender {
    bool offered;
    bool ready;
    bool committed;
    bool cancelled;
    uint32_t offset;
} Sender;
struct CoopBootstrap {
    CoopSession* session;
    CoopBootstrapPhase phase;
    bool host;
    bool loaded;
    uint8_t participants;
    uint32_t revision;
    uint32_t epoch;
    uint32_t checksum;
    uint32_t rolling_crc;
    uint32_t size;
    uint32_t received;
    const uint8_t* source;
    uint8_t* incoming;
    uint64_t now;
    uint64_t progressed;
    Sender senders[COOP_MAX_PLAYERS];
    const char* error;
};

static uint32_t read_u32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void write_u32(uint8_t* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(value >> (24 - i * 8));
}
static uint32_t crc_update(uint32_t crc, const uint8_t* data, size_t size)
{
    while (size--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & (0u - (crc & 1)));
    }
    return crc;
}
uint32_t coop_snapshot_crc(const void* data, size_t size)
{
    return ~crc_update(UINT32_MAX, data, size);
}
static bool failed(CoopBootstrap* b, const char* error)
{
    b->phase = COOP_BOOT_FAILED;
    b->error = error;
    return false;
}
static uint8_t participant_mask(CoopSession* session)
{
    uint8_t mask = 0;
    const CoopPlayer* players = coop_session_players(session);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if (players[i].connected) mask |= (uint8_t)(1u << i);
    return mask;
}

CoopBootstrap* coop_bootstrap_create(CoopSession* session)
{
    if (!session) return NULL;
    CoopBootstrap* b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->session = session;
    b->host = coop_session_is_host(session);
    b->error = "";
    coop_session_set_handler(session, coop_bootstrap_message, b);
    return b;
}
void coop_bootstrap_destroy(CoopBootstrap* b)
{
    if (!b) return;
    coop_session_set_handler(b->session, NULL, NULL);
    free(b->incoming);
    free(b);
}
bool coop_bootstrap_begin(CoopBootstrap* b, uint32_t epoch, const void* data, size_t size, uint64_t now)
{
    if (b->phase == COOP_BOOT_IDLE) b->host = coop_session_is_host(b->session);
    if (!b->host || b->phase != COOP_BOOT_IDLE || !epoch || !data || !size || size > COOP_SNAPSHOT_LIMIT
        || !coop_session_all_ready(b->session)) return false;
    b->source = data;
    b->size = (uint32_t)size;
    b->epoch = epoch;
    b->checksum = coop_snapshot_crc(data, size);
    b->participants = participant_mask(b->session);
    b->revision = coop_session_revision(b->session);
    b->now = b->progressed = now;
    b->phase = COOP_BOOT_SENDING;
    b->senders[0].ready = true;
    return true;
}

bool coop_bootstrap_message(void* context, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    CoopBootstrap* b = context;
    /* A receiver can be created long after process startup, then receive its
     * offer before its first poll. Progress belongs to packet arrival time. */
    uint64_t received_at = coop_session_now(b->session);
    if (received_at > b->now) b->now = received_at;
    if (b->phase == COOP_BOOT_IDLE) b->host = coop_session_is_host(b->session);
    if (type < COOP_SNAPSHOT_OFFER || type > COOP_SNAPSHOT_CANCEL || size < 4) return false;
    uint32_t epoch = read_u32(p);
    if (b->host) {
        if (sender == 0 || sender >= COOP_MAX_PLAYERS || !(b->participants & (1u << sender))
            || (b->phase != COOP_BOOT_SENDING && b->phase != COOP_BOOT_WAITING) || epoch != b->epoch || size != 4) return false;
        if (type == COOP_SNAPSHOT_CANCEL) {
            failed(b, "A player could not load the campaign snapshot.");
            return true;
        }
        if (type != COOP_SNAPSHOT_READY || b->senders[sender].ready
            || b->senders[sender].offset != b->size) return false;
        b->senders[sender].ready = true;
        b->progressed = b->now;
        return true;
    }
    if (sender != 0 || b->phase == COOP_BOOT_FAILED || b->phase == COOP_BOOT_COMMITTED) return false;
    if (type == COOP_SNAPSHOT_OFFER) {
        if (b->phase != COOP_BOOT_IDLE || size != 13 || !epoch) return false;
        uint32_t bytes = read_u32(p + 4);
        uint8_t mask = p[12];
        if (!bytes || bytes > COOP_SNAPSHOT_LIMIT || mask != participant_mask(b->session)
            || !(mask & 1) || (mask & (mask - 1)) == 0 || !coop_session_all_ready(b->session)) return false;
        b->incoming = malloc(bytes);
        if (!b->incoming) return failed(b, "Not enough memory for the campaign snapshot.");
        b->epoch = epoch;
        b->size = bytes;
        b->checksum = read_u32(p + 8);
        b->participants = mask;
        b->revision = coop_session_revision(b->session);
        b->rolling_crc = UINT32_MAX;
        b->phase = COOP_BOOT_RECEIVING;
        b->progressed = b->now;
        return true;
    }
    if (epoch != b->epoch || b->phase == COOP_BOOT_IDLE) return false;
    if (type == COOP_SNAPSHOT_CANCEL && size == 4) {
        failed(b, "The host cancelled campaign loading.");
        return true;
    }
    if (type == COOP_SNAPSHOT_CHUNK) {
        if (b->phase != COOP_BOOT_RECEIVING || size <= 8 || size > COOP_MESSAGE_CAPACITY
            || read_u32(p + 4) != b->received || size - 8 > b->size - b->received) return false;
        memcpy(b->incoming + b->received, p + 8, size - 8);
        b->rolling_crc = crc_update(b->rolling_crc, p + 8, size - 8);
        b->received += (uint32_t)(size - 8);
        b->progressed = b->now;
        if (b->received == b->size) {
            if (~b->rolling_crc != b->checksum) return failed(b, "Campaign snapshot checksum differs.");
            b->phase = COOP_BOOT_RECEIVED;
        }
        return true;
    }
    if (type == COOP_SNAPSHOT_COMMIT && size == 4 && b->phase == COOP_BOOT_WAITING) {
        b->phase = COOP_BOOT_COMMITTED;
        return true;
    }
    return false;
}

void coop_bootstrap_poll(CoopBootstrap* b, uint64_t now)
{
    if (now > b->now) b->now = now;
    if (b->phase == COOP_BOOT_IDLE || b->phase == COOP_BOOT_COMMITTED) return;
    if (b->phase != COOP_BOOT_FAILED && (coop_session_phase(b->session) != COOP_LOBBY || participant_mask(b->session) != b->participants || coop_session_revision(b->session) != b->revision))
        failed(b, "Party membership or character selection changed while loading.");
    if (b->phase != COOP_BOOT_FAILED && b->now - b->progressed >= PROGRESS_TIMEOUT)
        failed(b, "Campaign snapshot loading timed out.");
    uint8_t message[COOP_MESSAGE_CAPACITY];
    write_u32(message, b->epoch);
    if (b->phase == COOP_BOOT_FAILED) {
        if (b->host) {
            for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++)
                if ((b->participants & (1u << i)) && !b->senders[i].cancelled)
                    b->senders[i].cancelled = coop_session_send(b->session, i, COOP_SNAPSHOT_CANCEL, message, 4);
        } else if (!b->senders[0].cancelled) {
            b->senders[0].cancelled = coop_session_send(b->session, 0, COOP_SNAPSHOT_CANCEL, message, 4);
        }
        return;
    }
    if (!b->host) {
        if (b->phase == COOP_BOOT_RECEIVED && b->loaded
            && coop_session_send(b->session, 0, COOP_SNAPSHOT_READY, message, 4)) b->phase = COOP_BOOT_WAITING;
        return;
    }
    bool all_sent = true;
    bool all_ready = true;
    for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++) {
        if (!(b->participants & (1u << i))) continue;
        Sender* sender = &b->senders[i];
        if (!sender->offered) {
            write_u32(message + 4, b->size);
            write_u32(message + 8, b->checksum);
            message[12] = b->participants;
            sender->offered = coop_session_send(b->session, i, COOP_SNAPSHOT_OFFER, message, 13);
        }
        for (unsigned chunk = 0; sender->offered && sender->offset < b->size && chunk < CHUNKS_PER_POLL; chunk++) {
            size_t bytes = b->size - sender->offset;
            if (bytes > CHUNK_DATA) bytes = CHUNK_DATA;
            write_u32(message + 4, sender->offset);
            memcpy(message + 8, b->source + sender->offset, bytes);
            if (!coop_session_send(b->session, i, COOP_SNAPSHOT_CHUNK, message, 8 + bytes)) break;
            sender->offset += (uint32_t)bytes;
            b->progressed = b->now;
        }
        all_sent &= sender->offset == b->size;
        all_ready &= sender->ready;
    }
    if (all_sent) b->phase = COOP_BOOT_WAITING;
    if (all_ready) {
        bool committed = true;
        for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++) {
            if (!(b->participants & (1u << i))) continue;
            if (!b->senders[i].committed)
                b->senders[i].committed = coop_session_send(b->session, i, COOP_SNAPSHOT_COMMIT, message, 4);
            committed &= b->senders[i].committed;
        }
        if (committed) b->phase = COOP_BOOT_COMMITTED;
    }
}
bool coop_bootstrap_loaded(CoopBootstrap* b)
{
    if (b->host || b->phase != COOP_BOOT_RECEIVED || b->loaded) return false;
    b->loaded = true;
    return true;
}
void coop_bootstrap_cancel(CoopBootstrap* b)
{
    failed(b, "Campaign loading cancelled locally.");
}
CoopBootstrapPhase coop_bootstrap_phase(const CoopBootstrap* b) { return b->phase; }
const uint8_t* coop_bootstrap_data(const CoopBootstrap* b, size_t* size)
{
    *size = b->size;
    return b->phase == COOP_BOOT_RECEIVED || b->phase == COOP_BOOT_WAITING || b->phase == COOP_BOOT_COMMITTED ? b->incoming : NULL;
}
const char* coop_bootstrap_error(const CoopBootstrap* b) { return b->error; }
