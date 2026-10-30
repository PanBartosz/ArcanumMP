#include "coop/magic_state.h"
#include "coop/identity.h"
#include <limits.h>
#include <string.h>

static bool instance(const CoopObjectId* id, bool nullable)
{
    return coop_object_id_valid(id, nullable) && id->bytes[0] != 1;
}
bool coop_magic_view_valid(const CoopMagicView* view)
{
    if (!view || view->count > COOP_MAGIC_RUN_LIMIT) return false;
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) {
        const CoopMagicChoice* choice = &view->choices[i];
        if (!choice->token) {
            static const uint8_t zero[16];
            if (choice->map || choice->spell || choice->charge || memcmp(choice->actor, zero, 16)) return false;
            continue;
        }
        if (!coop_identity_valid(choice->actor) || choice->spell >= COOP_MAGIC_SPELL_COUNT || choice->map > INT32_MAX || choice->charge > 1) return false;
        for (unsigned j = 0; j < i; j++) if (view->choices[j].token && (view->choices[j].token == choice->token || !memcmp(view->choices[j].actor, choice->actor, 16))) return false;
    }
    uint64_t previous = 0;
    for (uint32_t i = 0; i < view->count; i++) {
        const CoopMagicEntry* e = &view->entries[i];
        if (e->token <= previous || !coop_identity_valid(e->owner) || e->spell >= COOP_MAGIC_SPELL_COUNT
            || !instance(&e->target, true)) return false;
        previous = e->token;
    }
    return true;
}
size_t coop_magic_view_size(const CoopMagicView* view)
{
    return coop_magic_view_valid(view) ? COOP_MAGIC_VIEW_HEADER + view->count * COOP_MAGIC_ENTRY_BYTES : 0;
}
bool coop_magic_view_encode(const CoopMagicView* view, void* data, size_t size)
{
    if (!data || !size || size != coop_magic_view_size(view)) return false;
    uint8_t* p = data;
    memcpy(p, "ACM2", 4);
    coop_object_write32(p + 4, view->count);
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) {
        const CoopMagicChoice* choice = &view->choices[i];
        uint8_t* at = p + 8 + i * COOP_MAGIC_CHOICE_BYTES;
        memset(at, 0, COOP_MAGIC_CHOICE_BYTES); memcpy(at, choice->actor, 16);
        coop_object_write64(at + 16, choice->token); coop_object_write32(at + 24, choice->map);
        at[28] = (uint8_t)(choice->spell >> 8); at[29] = (uint8_t)choice->spell; at[30] = choice->charge;
    }
    for (uint32_t i = 0; i < view->count; i++) {
        const CoopMagicEntry* e = &view->entries[i];
        uint8_t* at = p + COOP_MAGIC_VIEW_HEADER + i * COOP_MAGIC_ENTRY_BYTES;
        coop_object_write64(at, e->token);
        memcpy(at + 8, e->owner, 16);
        at[24] = (uint8_t)(e->spell >> 8);
        at[25] = (uint8_t)e->spell;
        memcpy(at + 26, &e->target, COOP_OBJECT_ID_BYTES);
    }
    return true;
}
bool coop_magic_view_decode(const void* data, size_t size, CoopMagicView* out)
{
    const uint8_t* p = data;
    if (!p || !out || size < COOP_MAGIC_VIEW_HEADER || memcmp(p, "ACM2", 4)) return false;
    uint32_t count = coop_object_read32(p + 4);
    if (count > COOP_MAGIC_RUN_LIMIT || size != COOP_MAGIC_VIEW_HEADER + count * COOP_MAGIC_ENTRY_BYTES) return false;
    CoopMagicView view = { .count = count };
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) {
        const uint8_t* at = p + 8 + i * COOP_MAGIC_CHOICE_BYTES;
        CoopMagicChoice* choice = &view.choices[i];
        if (at[31]) return false;
        memcpy(choice->actor, at, 16); choice->token = coop_object_read64(at + 16);
        choice->map = coop_object_read32(at + 24); choice->spell = ((uint16_t)at[28] << 8) | at[29]; choice->charge = at[30];
    }
    for (uint32_t i = 0; i < count; i++) {
        CoopMagicEntry* e = &view.entries[i];
        const uint8_t* at = p + COOP_MAGIC_VIEW_HEADER + i * COOP_MAGIC_ENTRY_BYTES;
        e->token = coop_object_read64(at);
        memcpy(e->owner, at + 8, 16);
        e->spell = ((uint16_t)at[24] << 8) | at[25];
        memcpy(&e->target, at + 26, COOP_OBJECT_ID_BYTES);
    }
    if (!coop_magic_view_valid(&view)) return false;
    *out = view;
    return true;
}
static bool request_valid(const CoopMagicRequest* r)
{
    if (!r || !r->epoch || !r->sequence || r->map > INT32_MAX || !coop_identity_valid(r->actor)
        || r->generation == UINT32_MAX || !instance(&r->source, r->operation == COOP_MAGIC_CANCEL)
        || !instance(&r->target, true)) return false;
    if (r->operation == COOP_MAGIC_CANCEL)
        return r->token && !r->spell && !r->flags && !r->source.bytes[0] && !r->target.bytes[0] && !r->x && !r->y;
    if (r->operation == COOP_MAGIC_DESTINATION)
        return r->token && r->spell < COOP_MAGIC_SPELL_COUNT && !r->flags && r->source.bytes[0] == 2
            && !r->target.bytes[0] && r->x < 4096 && !r->y;
    if ((r->operation != COOP_MAGIC_CAST && r->operation != COOP_MAGIC_USE_ITEM)
        || r->spell >= COOP_MAGIC_SPELL_COUNT || (r->operation == COOP_MAGIC_USE_ITEM && r->spell)
        || r->token || (r->flags & ~7u)
        || ((r->flags & COOP_MAGIC_OWN_SKILL) && (r->operation != COOP_MAGIC_USE_ITEM || (r->flags & COOP_MAGIC_LOCATION)))) return false;
    if (r->operation == COOP_MAGIC_USE_ITEM && !r->target.bytes[0] && !r->flags)
        return !r->x && !r->y;
    return (r->flags & COOP_MAGIC_LOCATION) ? !r->target.bytes[0] && r->x <= INT32_MAX && r->y <= INT32_MAX
                                            : r->target.bytes[0] && !r->x && !r->y;
}
bool coop_magic_request_encode(const CoopMagicRequest* r, uint8_t out[COOP_MAGIC_REQUEST_BYTES])
{
    if (!out || !request_valid(r)) return false;
    coop_object_write32(out, r->epoch);
    coop_object_write32(out + 4, r->sequence);
    coop_object_write32(out + 8, r->map);
    memcpy(out + 12, r->actor, 16);
    out[28] = (uint8_t)r->operation;
    out[29] = (uint8_t)(r->spell >> 8);
    out[30] = (uint8_t)r->spell;
    out[31] = r->flags;
    memcpy(out + 32, &r->source, COOP_OBJECT_ID_BYTES);
    memcpy(out + 49, &r->target, COOP_OBJECT_ID_BYTES);
    coop_object_write32(out + 66, r->x);
    coop_object_write32(out + 70, r->y);
    coop_object_write64(out + 74, r->token);
    coop_object_write32(out + 82, r->generation);
    return true;
}
bool coop_magic_request_decode(const void* data, size_t size, CoopMagicRequest* out)
{
    if (!data || !out || size != COOP_MAGIC_REQUEST_BYTES) return false;
    const uint8_t* p = data;
    CoopMagicRequest r = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4), .map = coop_object_read32(p + 8), .operation = (CoopMagicOperation)p[28], .spell = ((uint16_t)p[29] << 8) | p[30], .flags = p[31], .x = coop_object_read32(p + 66), .y = coop_object_read32(p + 70), .token = coop_object_read64(p + 74), .generation = coop_object_read32(p + 82) };
    memcpy(r.actor, p + 12, 16);
    memcpy(&r.source, p + 32, COOP_OBJECT_ID_BYTES);
    memcpy(&r.target, p + 49, COOP_OBJECT_ID_BYTES);
    if (!request_valid(&r)) return false;
    *out = r;
    return true;
}
static bool written_valid(const CoopWrittenView* v)
{
    return v && v->epoch && v->sequence && v->revision && v->map <= INT32_MAX && coop_identity_valid(v->reader)
        && v->type < 7 && v->variant <= 1 && (!v->variant || v->type == 2 || v->type == 5) && v->number <= INT32_MAX - 10000;
}
bool coop_written_encode(const CoopWrittenView* v, uint8_t out[COOP_WRITTEN_BYTES])
{
    if (!out || !written_valid(v)) return false;
    memset(out, 0, COOP_WRITTEN_BYTES);
    coop_object_write32(out, v->epoch); coop_object_write32(out + 4, v->sequence);
    coop_object_write32(out + 8, v->map); coop_object_write32(out + 12, v->revision);
    memcpy(out + 16, v->reader, 16); out[32] = v->type; out[33] = v->variant;
    coop_object_write32(out + 36, v->number);
    return true;
}
bool coop_written_decode(const void* data, size_t size, CoopWrittenView* out)
{
    if (!data || !out || size != COOP_WRITTEN_BYTES) return false;
    const uint8_t* p = data;
    if (p[34] || p[35]) return false;
    CoopWrittenView v = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4),
        .map = coop_object_read32(p + 8), .revision = coop_object_read32(p + 12), .type = p[32], .variant = p[33], .number = coop_object_read32(p + 36) };
    memcpy(v.reader, p + 16, 16);
    if (!written_valid(&v)) return false;
    *out = v;
    return true;
}
