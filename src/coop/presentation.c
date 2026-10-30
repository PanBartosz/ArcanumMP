#include "coop/presentation.h"
#include "coop/object_state.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
struct CoopPresentation {
    CoopSession* session; CoopPresentView view;
    uint8_t received[COOP_PRESENT_BYTES], state;
    uint32_t acknowledged[4]; uint64_t sent[4], now, opened, ack_at;
    bool host, dirty, have_view, skip;
};
static bool valid(const CoopPresentView* v)
{
    if (!v || !v->sequence || !v->revision || v->kind > COOP_PRESENT_FADE || v->phase > COOP_PRESENT_FAILED
        || !(v->participants & 1) || (v->participants & ~15u) || ((v->ready | v->done | v->skip | v->failed) & ~v->participants)
        || (v->done & ~v->ready) || v->id > INT32_MAX || v->track > 65535) return false;
    if (v->phase == COOP_PRESENT_PREPARING && v->done) return false;
    if ((v->phase == COOP_PRESENT_PLAYING || v->phase == COOP_PRESENT_COMPLETE) && v->ready != v->participants) return false;
    if (v->phase == COOP_PRESENT_COMPLETE && v->done != v->participants) return false;
    if ((v->phase == COOP_PRESENT_FAILED) != (v->failed != 0)) return false;
    bool terminated = false; size_t length = 0;
    for (unsigned i = 0; i < sizeof(v->path); i++) {
        unsigned char c = (unsigned char)v->path[i];
        if (!c) terminated = true;
        else { if (terminated || c < 32 || c > 126 || c == ':') return false; length++; }
    }
    if (!terminated) return false;
    if (v->kind == COOP_PRESENT_FADE) return !length && v->id <= 0xffffffu && v->track <= 60000
        && !(v->flags & ~0x00ffff01u) && (v->flags >> 8) <= 1024;
    if (v->flags > 15) return false;
    if (v->kind != COOP_PRESENT_MOVIE) return !length && !v->track && !v->flags && (v->kind != COOP_PRESENT_END || v->id <= 1);
    if (length < 12 || (strncmp(v->path, "movies\\", 7) && strncmp(v->path, "movies/", 7)) || strstr(v->path, "..") || v->id) return false;
    return v->path[length - 4] == '.' && tolower((unsigned char)v->path[length - 3]) == 'b'
        && tolower((unsigned char)v->path[length - 2]) == 'i' && tolower((unsigned char)v->path[length - 1]) == 'k';
}
bool coop_present_encode(const CoopPresentView* v, uint8_t out[COOP_PRESENT_BYTES])
{
    if (!out || !valid(v)) return false;
    memset(out, 0, COOP_PRESENT_BYTES);
    coop_object_write32(out, v->sequence); coop_object_write32(out + 4, v->revision);
    out[8] = (uint8_t)v->kind; out[9] = (uint8_t)v->phase; out[10] = v->participants; out[11] = v->ready;
    out[12] = v->done; out[13] = v->skip; out[14] = v->failed;
    coop_object_write32(out + 16, v->flags); coop_object_write32(out + 20, v->id); coop_object_write32(out + 24, v->track);
    memcpy(out + 28, v->path, 260); return true;
}
bool coop_present_decode(const void* bytes, size_t size, CoopPresentView* out)
{
    if (!bytes || !out || size != COOP_PRESENT_BYTES) return false;
    const uint8_t* p = bytes; if (p[15]) return false;
    CoopPresentView v = { .sequence = coop_object_read32(p), .revision = coop_object_read32(p + 4),
        .kind = (CoopPresentKind)p[8], .phase = (CoopPresentPhase)p[9], .participants = p[10], .ready = p[11],
        .done = p[12], .skip = p[13], .failed = p[14], .flags = coop_object_read32(p + 16), .id = coop_object_read32(p + 20), .track = coop_object_read32(p + 24) };
    memcpy(v.path, p + 28, 260); if (!valid(&v)) return false; *out = v; return true;
}
CoopPresentation* coop_present_create(CoopSession* session)
{
    CoopPresentation* p = session ? calloc(1, sizeof(*p)) : NULL;
    if (p) { p->session = session; p->host = coop_session_is_host(session); }
    return p;
}
void coop_present_destroy(CoopPresentation* p) { free(p); }
static bool changed(CoopPresentation* p)
{
    if (p->view.revision == UINT32_MAX) return false;
    p->view.revision++; p->dirty = true; return true;
}
bool coop_present_settled(const CoopPresentation* p)
{
    if (!p || !p->view.sequence) return true;
    if (p->view.phase != COOP_PRESENT_COMPLETE) return false;
    if (p->host) for (unsigned i = 1; i < 4; i++) if ((p->view.participants & (1u << i)) && p->acknowledged[i] != p->view.revision) return false;
    return p->host || p->ack_at != 0;
}
bool coop_present_begin(CoopPresentation* p, CoopPresentKind kind, unsigned id, const char* path, unsigned flags, unsigned track, uint64_t now)
{
    if (!p || !p->host || !coop_present_settled(p) || p->view.sequence == UINT32_MAX || (path && strlen(path) >= 260)) return false;
    CoopPresentView v = { .sequence = p->view.sequence + 1, .revision = 1, .kind = kind, .id = id, .flags = flags, .track = track };
    for (unsigned i = 0; i < 4; i++) if (coop_session_players(p->session)[i].connected) v.participants |= (uint8_t)(1u << i);
    if (path) strcpy(v.path, path);
    if (!valid(&v)) return false;
    p->view = v; p->state = 0; p->skip = false; p->now = p->opened = now; p->dirty = true;
    memset(p->acknowledged, 0, sizeof(p->acknowledged)); memset(p->sent, 0, sizeof(p->sent)); return true;
}
static bool acknowledge(CoopPresentation* p, unsigned sender, unsigned state, bool skip)
{
    uint8_t bit = (uint8_t)(1u << sender);
    if (p->view.phase == COOP_PRESENT_COMPLETE || p->view.phase == COOP_PRESENT_FAILED) return true;
    if (state == 2 && (!(p->view.ready & bit) || p->view.phase != COOP_PRESENT_PLAYING)) return false;
    uint8_t ready = p->view.ready, done = p->view.done, skipped = p->view.skip, failed = p->view.failed;
    if (state == 1 || state == 2) p->view.ready |= bit;
    if (state == 2) p->view.done |= bit;
    if (state == 3) { p->view.failed |= bit; p->view.phase = COOP_PRESENT_FAILED; }
    if (skip) p->view.skip |= bit;
    return (ready == p->view.ready && done == p->view.done && skipped == p->view.skip && failed == p->view.failed) || changed(p);
}
bool coop_present_message(CoopPresentation* p, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    if (!p || !bytes) return false;
    if (p->host) {
        if (type != COOP_PRESENT_ACK || size != 10 || sender >= 4 || !(p->view.participants & (1u << sender)) || !coop_session_players(p->session)[sender].connected) return false;
        uint32_t sequence = coop_object_read32(bytes), revision = coop_object_read32(bytes + 4);
        if (sequence < p->view.sequence) return sequence != 0;
        if (sequence != p->view.sequence || !revision || revision > p->view.revision || bytes[8] > 3 || bytes[9] > 1) return false;
        if (revision > p->acknowledged[sender]) p->acknowledged[sender] = revision;
        return acknowledge(p, sender, bytes[8], bytes[9] != 0);
    }
    CoopPresentView v;
    if (sender || type != COOP_PRESENT_VIEW || !coop_present_decode(bytes, size, &v)) return false;
    if (v.sequence < p->view.sequence || (v.sequence == p->view.sequence && v.revision < p->view.revision)) return true;
    if (v.sequence == p->view.sequence && p->have_view) {
        if (v.revision == p->view.revision && memcmp(bytes, p->received, size)) return false;
        if (v.kind != p->view.kind || v.id != p->view.id || v.flags != p->view.flags || v.track != p->view.track
            || v.participants != p->view.participants || memcmp(v.path, p->view.path, sizeof(v.path))) return false;
        if ((v.ready & p->view.ready) != p->view.ready || (v.done & p->view.done) != p->view.done || (v.skip & p->view.skip) != p->view.skip
            || v.phase < p->view.phase || (p->view.phase >= COOP_PRESENT_COMPLETE && v.phase != p->view.phase)) return false;
    } else {
        if (p->view.sequence == UINT32_MAX || v.sequence != p->view.sequence + 1 || (p->view.sequence && p->view.phase != COOP_PRESENT_COMPLETE)) return false;
        p->state = 0; p->skip = false; p->opened = p->now;
    }
    p->view = v; p->have_view = true; p->ack_at = 0; memcpy(p->received, bytes, size); return true;
}
bool coop_present_poll(CoopPresentation* p, uint64_t now)
{
    if (!p) return false;
    p->now = now;
    if (!p->view.sequence) return true;
    if (coop_present_active(p) && now - p->opened > (p->view.phase == COOP_PRESENT_PREPARING ? 30000u : 1800000u)) return false;
    if (!p->host) {
        if (now - p->ack_at >= 100) {
            uint8_t ack[10]; coop_object_write32(ack, p->view.sequence); coop_object_write32(ack + 4, p->view.revision);
            ack[8] = p->state; ack[9] = p->skip;
            if (coop_session_send(p->session, 0, COOP_PRESENT_ACK, ack, sizeof(ack))) p->ack_at = now;
        }
        return true;
    }
    for (unsigned i = 0; i < 4; i++) if ((p->view.participants & (1u << i)) && !coop_session_players(p->session)[i].connected) return false;
    if (!acknowledge(p, 0, p->state, p->skip)) return false;
    if (p->view.phase == COOP_PRESENT_PREPARING && p->view.ready == p->view.participants) { p->view.phase = COOP_PRESENT_PLAYING; if (!changed(p)) return false; }
    if (p->view.phase == COOP_PRESENT_PLAYING && p->view.done == p->view.participants) { p->view.phase = COOP_PRESENT_COMPLETE; if (!changed(p)) return false; }
    uint8_t bytes[COOP_PRESENT_BYTES]; if (!coop_present_encode(&p->view, bytes)) return false;
    for (unsigned i = 1; i < 4; i++) if ((p->view.participants & (1u << i)) && (p->dirty || (p->acknowledged[i] != p->view.revision && now - p->sent[i] >= 100))) {
        if (coop_session_send(p->session, (uint8_t)i, COOP_PRESENT_VIEW, bytes, sizeof(bytes))) p->sent[i] = now;
    }
    p->dirty = false; return true;
}
const CoopPresentView* coop_present_current(const CoopPresentation* p) { return p ? &p->view : NULL; }
void coop_present_ready(CoopPresentation* p, bool success) { if (p) { p->state = success ? 1 : 3; p->ack_at = 0; } }
void coop_present_done(CoopPresentation* p, bool success) { if (p) { p->state = success ? 2 : 3; p->ack_at = 0; } }
void coop_present_skip(CoopPresentation* p) { if (p) { p->skip = true; p->ack_at = 0; } }
bool coop_present_active(const CoopPresentation* p) { return p && p->view.sequence && (p->view.phase == COOP_PRESENT_PREPARING || p->view.phase == COOP_PRESENT_PLAYING); }
