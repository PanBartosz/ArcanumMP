#include "coop/skills_state.h"
#include <limits.h>
#include <string.h>

static bool request_valid(const CoopSkillRequest* r)
{
    if (!r || !r->epoch || !r->sequence || r->map > INT32_MAX || !coop_identity_valid(r->actor)
        || r->generation == UINT32_MAX || r->action < COOP_SKILL_CONCEAL || r->action > COOP_SKILL_DISARM
        || !coop_object_id_valid(&r->target, r->action == COOP_SKILL_CONCEAL)
        || (r->target.bytes[0] && r->target.bytes[0] != 2 && r->target.bytes[0] != 3)) return false;
    return r->action == COOP_SKILL_CONCEAL ? !r->target.bytes[0] && r->value <= 1
        : r->target.bytes[0] && (r->action == COOP_SKILL_DISARM ? r->value <= 1 : !r->value);
}
bool coop_skill_request_encode(const CoopSkillRequest* r, uint8_t out[COOP_SKILL_REQUEST_BYTES])
{
    if (!out || !request_valid(r)) return false;
    coop_object_write32(out, r->epoch);
    coop_object_write32(out + 4, r->sequence);
    coop_object_write32(out + 8, r->map);
    memcpy(out + 12, r->actor, sizeof(r->actor));
    out[28] = (uint8_t)r->action;
    out[29] = r->value;
    memcpy(out + 30, r->target.bytes, sizeof(r->target.bytes));
    coop_object_write32(out + 47, r->generation);
    return true;
}
bool coop_skill_request_decode(const void* data, size_t size, CoopSkillRequest* out)
{
    if (!data || !out || size != COOP_SKILL_REQUEST_BYTES) return false;
    const uint8_t* p = data;
    CoopSkillRequest r = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4),
        .map = coop_object_read32(p + 8), .action = (CoopSkillAction)p[28], .value = p[29],
        .generation = coop_object_read32(p + 47) };
    memcpy(r.actor, p + 12, sizeof(r.actor));
    memcpy(r.target.bytes, p + 30, sizeof(r.target.bytes));
    if (!request_valid(&r)) return false;
    *out = r;
    return true;
}
static bool feedback_valid(const CoopSkillFeedback* f)
{
    if (!f || !f->epoch || !f->sequence || !f->revision || f->map > INT32_MAX || !coop_identity_valid(f->actor)) return false;
    if (f->catalogue == 6) return f->red && (f->message == 2 || f->message == 3);
    if (f->catalogue == 1) return !f->message && !f->red;
    if (f->catalogue == 2) return f->red && ((f->message >= 600 && f->message <= 603) || f->message == 605 || f->message == 10000);
    if (f->catalogue == 3) return f->red && f->message > 0 && f->message <= 19;
    if (f->catalogue == 4 || f->catalogue == 5) return f->message >= 50 && f->red == (f->catalogue == 5);
    if (f->catalogue) return false;
    switch (f->message) {
    case 500: case 525: case 550: case 560: return !f->red;
    case 501: case 505: case 512: case 551: case 561: return f->red;
    default: return false;
    }
}
bool coop_skill_feedback_encode(const CoopSkillFeedback* f, uint8_t out[COOP_SKILL_FEEDBACK_BYTES])
{
    if (!out || !feedback_valid(f)) return false;
    coop_object_write32(out, f->epoch);
    coop_object_write32(out + 4, f->sequence);
    coop_object_write32(out + 8, f->map);
    coop_object_write32(out + 12, f->revision);
    memcpy(out + 16, f->actor, sizeof(f->actor));
    out[32] = (uint8_t)(f->message >> 8); out[33] = (uint8_t)f->message;
    out[34] = f->red; out[35] = f->catalogue;
    return true;
}
bool coop_skill_feedback_decode(const void* data, size_t size, CoopSkillFeedback* out)
{
    if (!data || !out || size != COOP_SKILL_FEEDBACK_BYTES) return false;
    const uint8_t* p = data;
    if (p[34] > 1 || p[35] > 6) return false;
    CoopSkillFeedback f = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4),
        .map = coop_object_read32(p + 8), .revision = coop_object_read32(p + 12),
        .message = (uint16_t)((uint16_t)p[32] << 8 | p[33]), .red = p[34] != 0, .catalogue = p[35] };
    memcpy(f.actor, p + 16, sizeof(f.actor));
    if (!feedback_valid(&f)) return false;
    *out = f;
    return true;
}
