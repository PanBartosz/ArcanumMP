#include "coop/dialogue.h"
#include "coop/bootstrap.h"
#include <stdlib.h>
#include <string.h>
enum { QUEUE = 32 };
typedef struct DialogueUpdate {
    struct DialogueUpdate* next;
    uint8_t* bytes;
    size_t size;
    uint32_t crc;
    uint32_t revision;
    bool displayed;
    bool begun[COOP_MAX_PLAYERS];
    bool acknowledged[COOP_MAX_PLAYERS];
    size_t offset[COOP_MAX_PLAYERS];
} DialogueUpdate;
typedef struct DialogueRequest {
    bool pending;
    bool processed;
    bool accepted;
    uint32_t sequence;
    uint32_t conversation;
    uint32_t revision;
    unsigned option;
} DialogueRequest;
struct CoopDialogue {
    CoopSession* session;
    bool host;
    bool retired;
    uint32_t epoch;
    uint32_t revision;
    uint32_t command;
    uint32_t awaiting_result;
    uint64_t now;
    uint64_t progressed;
    uint32_t roster;
    DialogueUpdate* first;
    DialogueUpdate* last;
    unsigned queued;
    uint8_t* incoming;
    size_t incoming_size;
    size_t received;
    uint32_t incoming_crc;
    bool displayed;
    CoopDialogueView current;
    DialogueRequest requests[COOP_MAX_PLAYERS];
    uint32_t sequences[COOP_MAX_PLAYERS];
    uint64_t request_window[COOP_MAX_PLAYERS];
    unsigned request_count[COOP_MAX_PLAYERS];
    unsigned accepted;
    unsigned rejected;
    CoopDialoguePresent present;
    CoopDialogueChoose choose;
    void* context;
};
CoopDialogue* coop_dialogue_create(CoopSession* session, uint32_t epoch, uint64_t now,
    CoopDialoguePresent present, CoopDialogueChoose choose, void* context)
{
    if (!session || !epoch) return NULL;
    CoopDialogue* d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->session = session;
    d->host = coop_session_is_host(session);
    d->epoch = epoch;
    d->now = d->progressed = now;
    d->roster = coop_session_revision(session);
    d->present = present;
    d->choose = choose;
    d->context = context;
    return d;
}
void coop_dialogue_retire(CoopDialogue* d) { if (d) d->retired = true; }
void coop_dialogue_destroy(CoopDialogue* d)
{
    if (!d) return;
    while (d->first) {
        DialogueUpdate* update = d->first;
        d->first = update->next;
        free(update->bytes);
        free(update);
    }
    free(d->incoming);
    free(d);
}
bool coop_dialogue_publish(CoopDialogue* d, const CoopDialogueView* view)
{
    if (!d || !view || !d->host || d->queued >= QUEUE || d->revision == UINT32_MAX
        || view->chooser >= COOP_MAX_PLAYERS || !coop_session_players(d->session)[view->chooser].connected) return false;
    CoopDialogueView copy = *view;
    copy.revision = d->revision + 1;
    DialogueUpdate* update = calloc(1, sizeof(*update));
    if (!update) return false;
    update->bytes = coop_dialogue_view_encode(&copy, &update->size);
    if (!update->bytes) { free(update); return false; }
    update->crc = coop_snapshot_crc(update->bytes, update->size);
    update->revision = copy.revision;
    if (d->last) d->last->next = update;
    else d->first = update;
    d->last = update;
    if (!d->queued++) d->progressed = d->now;
    d->revision++;
    return true;
}
static bool request(CoopDialogue* d, uint8_t sender, const uint8_t* p, size_t size)
{
    if (size != 20 || sender >= COOP_MAX_PLAYERS || !coop_session_players(d->session)[sender].connected) return false;
    uint32_t sequence = coop_object_read32(p + 4);
    if (!sequence || sequence <= d->sequences[sender] || d->requests[sender].pending) return false;
    if (d->now - d->request_window[sender] >= 1000) {
        d->request_window[sender] = d->now;
        d->request_count[sender] = 0;
    }
    if (++d->request_count[sender] > 30) return false;
    d->sequences[sender] = sequence;
    d->requests[sender] = (DialogueRequest) { .pending = true, .sequence = sequence,
        .conversation = coop_object_read32(p + 8), .revision = coop_object_read32(p + 12),
        .option = coop_object_read32(p + 16) };
    return true;
}
bool coop_dialogue_message(CoopDialogue* d, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (!d || !p || size < 8 || coop_object_read32(p) != d->epoch) return false;
    uint32_t revision = coop_object_read32(p + 4);
    if (d->host) {
        if (!sender || sender >= COOP_MAX_PLAYERS) return false;
        if (type == COOP_DIALOGUE_CHOICE) return request(d, sender, p, size);
        DialogueUpdate* update = d->first;
        if (type != COOP_DIALOGUE_ACK || size != 8 || !update || !update->displayed
            || revision != update->revision || !update->begun[sender]
            || update->offset[sender] != update->size || update->acknowledged[sender]) return false;
        update->acknowledged[sender] = true;
        d->progressed = d->now;
        return true;
    }
    if (sender) return false;
    if (type == COOP_DIALOGUE_RESULT) {
        if (size != 9 || !d->awaiting_result || revision != d->awaiting_result || p[8] > 1) return false;
        d->awaiting_result = 0;
        return true;
    }
    if (type == COOP_DIALOGUE_BEGIN) {
        if (size != 16 || d->incoming || !revision || revision != d->revision + 1) return false;
        size_t length = coop_object_read32(p + 8);
        if (length < 101 || length > COOP_DIALOGUE_LIMIT) return false;
        d->incoming = malloc(length);
        if (!d->incoming) return false;
        d->incoming_size = length;
        d->received = 0;
        d->displayed = false;
        d->incoming_crc = coop_object_read32(p + 12);
        d->revision = revision;
        d->progressed = d->now;
        return true;
    }
    if (type != COOP_DIALOGUE_CHUNK || !d->incoming || size <= 12 || size > COOP_MESSAGE_CAPACITY
        || revision != d->revision || coop_object_read32(p + 8) != d->received
        || size - 12 > d->incoming_size - d->received) return false;
    memcpy(d->incoming + d->received, p + 12, size - 12);
    d->received += size - 12;
    d->progressed = d->now;
    return true;
}
bool coop_dialogue_choose(CoopDialogue* d, uint32_t conversation, uint32_t revision, unsigned option)
{
    if (!d || d->awaiting_result || !d->current.active || d->current.chooser != coop_session_local_slot(d->session)
        || conversation != d->current.conversation || revision != d->current.revision
        || (option >= d->current.option_count && option != COOP_DIALOGUE_CANCEL) || d->command == UINT32_MAX) return false;
    uint8_t p[20];
    coop_object_write32(p, d->epoch);
    coop_object_write32(p + 4, d->command + 1);
    coop_object_write32(p + 8, conversation);
    coop_object_write32(p + 12, revision);
    coop_object_write32(p + 16, option);
    bool sent = d->host ? request(d, 0, p, sizeof(p)) : coop_session_send(d->session, 0, COOP_DIALOGUE_CHOICE, p, sizeof(p));
    if (sent) {
        d->command++;
        if (!d->host) d->awaiting_result = d->command;
    }
    return sent;
}
static bool present(CoopDialogue* d, const CoopDialogueView* view)
{
    if (d->present && !d->present(d->context, view, view->chooser == coop_session_local_slot(d->session))) return false;
    if (view->mode != COOP_DIALOGUE_FLOAT) d->current = *view;
    return true;
}
bool coop_dialogue_poll(CoopDialogue* d, uint64_t now)
{
    if (d->retired) return true;
    if (now > d->now) d->now = now;
    if (coop_session_phase(d->session) != COOP_LOBBY || coop_session_revision(d->session) != d->roster) return false;
    if ((d->first || d->incoming) && d->now - d->progressed > 30000) return false;
    uint8_t p[COOP_MESSAGE_CAPACITY];
    coop_object_write32(p, d->epoch);
    if (!d->host) {
        if (!d->incoming || d->received != d->incoming_size) return true;
        if (!d->displayed) {
            CoopDialogueView view;
            if (coop_snapshot_crc(d->incoming, d->incoming_size) != d->incoming_crc
                || !coop_dialogue_view_decode(d->incoming, d->incoming_size, &view) || view.revision != d->revision) return false;
            if (!present(d, &view)) return true;
            if (d->retired) return true;
            d->displayed = true;
        }
        coop_object_write32(p + 4, d->revision);
        if (coop_session_send(d->session, 0, COOP_DIALOGUE_ACK, p, 8)) {
            free(d->incoming);
            d->incoming = NULL;
        }
        return true;
    }
    if (d->first) {
        DialogueUpdate* update = d->first;
        if (!update->displayed) {
            CoopDialogueView view;
            if (!coop_dialogue_view_decode(update->bytes, update->size, &view)) return false;
            if (!present(d, &view)) return true;
            if (d->retired) return true;
            update->displayed = true;
        }
        coop_object_write32(p + 4, update->revision);
        const CoopPlayer* players = coop_session_players(d->session);
        bool acknowledged = true;
        for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++) {
            if (!players[i].connected) continue;
            if (!update->begun[i]) {
                coop_object_write32(p + 8, (uint32_t)update->size);
                coop_object_write32(p + 12, update->crc);
                update->begun[i] = coop_session_send(d->session, i, COOP_DIALOGUE_BEGIN, p, 16);
            }
            for (unsigned chunk = 0; update->begun[i] && update->offset[i] < update->size && chunk < 8; chunk++) {
                size_t count = update->size - update->offset[i];
                if (count > COOP_MESSAGE_CAPACITY - 12) count = COOP_MESSAGE_CAPACITY - 12;
                coop_object_write32(p + 8, (uint32_t)update->offset[i]);
                memcpy(p + 12, update->bytes + update->offset[i], count);
                if (!coop_session_send(d->session, i, COOP_DIALOGUE_CHUNK, p, count + 12)) break;
                update->offset[i] += count;
                d->progressed = d->now;
            }
            acknowledged &= update->acknowledged[i];
        }
        if (!acknowledged) return true;
        d->first = update->next;
        if (!d->first) d->last = NULL;
        free(update->bytes);
        free(update);
        d->queued--;
        d->progressed = d->now;
        /* Display queued pages on separate polls, keeping their order. */
        if (d->first) return true;
    }
    for (uint8_t i = 0; i < COOP_MAX_PLAYERS; i++) {
        DialogueRequest* request = &d->requests[i];
        if (!request->pending) continue;
        if (!request->processed) {
            request->accepted = d->current.active && (d->current.mode == 0 || request->option == COOP_DIALOGUE_CANCEL) && d->current.chooser == i
                && request->conversation == d->current.conversation && request->revision == d->current.revision
                && (request->option < d->current.option_count || request->option == COOP_DIALOGUE_CANCEL)
                && d->choose && d->choose(d->context, request->option);
            if (d->retired) return true;
            request->processed = true;
            if (request->accepted) d->accepted++;
            else d->rejected++;
        }
        coop_object_write32(p + 4, request->sequence);
        p[8] = request->accepted;
        if (!i || coop_session_send(d->session, i, COOP_DIALOGUE_RESULT, p, 9)) request->pending = false;
        /* A successful engine choice may publish multiple new pages. */
        if (d->first) break;
    }
    return true;
}
const CoopDialogueView* coop_dialogue_current(const CoopDialogue* d) { return d ? &d->current : NULL; }
bool coop_dialogue_settled(const CoopDialogue* d) { return d && !d->first && !d->incoming; }
unsigned coop_dialogue_pending_count(const CoopDialogue* d) { return d && d->host ? d->queued : 0; }
bool coop_dialogue_pending_view(const CoopDialogue* d, unsigned index, CoopDialogueView* view, bool* displayed)
{
    if (!d || !d->host || !view || !displayed || index >= d->queued) return false;
    const DialogueUpdate* update = d->first;
    while (index--) update = update->next;
    if (!coop_dialogue_view_decode(update->bytes, update->size, view)) return false;
    *displayed = update->displayed;
    return true;
}
unsigned coop_dialogue_accepted(const CoopDialogue* d) { return d->accepted; }
unsigned coop_dialogue_rejected(const CoopDialogue* d) { return d->rejected; }
