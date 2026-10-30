#include "coop/control_state.h"
#include <limits.h>
#include <string.h>
static bool route_valid(CoopControlAction action, uint32_t destination, uint8_t count, const uint32_t* route)
{
    if (count > COOP_JOURNEY_WAYPOINTS || (count && action != COOP_CONTROL_TRAVEL)) return false;
    if (count && route[count - 1] != destination) return false;
    for (unsigned i = 0; i < COOP_JOURNEY_WAYPOINTS; i++)
        if ((i >= count && route[i]) || (i > 0 && i < count && route[i] == route[i - 1])) return false;
    return true;
}
static bool request_valid(const CoopControlRequest* r)
{
    if (!r || !r->epoch || !r->sequence || r->map > INT32_MAX || !coop_identity_valid(r->actor)
        || !route_valid(r->action, r->value, r->route_count, r->route)
        || !coop_object_id_valid(&r->bed, true) || !coop_save_label_valid(r->description) || r->checkpoint_present > 1) return false;
    bool checkpoint = r->action == COOP_CONTROL_SAVE || r->action == COOP_CONTROL_LOAD;
    if ((!checkpoint && (r->checkpoint_present || r->checkpoint_fingerprint || r->description[0]))
        || (!r->checkpoint_present && r->checkpoint_fingerprint)) return false;
    if (r->action == COOP_CONTROL_REST) return r->value <= 7 && !r->proposal;
    if (r->bed.bytes[0]) return false;
    if (r->action == COOP_CONTROL_VOTE) return r->proposal && r->value <= 1;
    if (r->action == COOP_CONTROL_CANCEL) return r->proposal && !r->value;
    if (r->proposal) return false;
    if (r->action == COOP_CONTROL_TRAVEL) return true;
    if (r->action == COOP_CONTROL_PAUSE) return r->value <= 1;
    if (r->action == COOP_CONTROL_SAVE) return r->value < COOP_SAVE_TOTAL && r->value != COOP_SAVE_RECOVERY && (!r->value || r->description[0]);
    if (r->action == COOP_CONTROL_LOAD) return r->value < COOP_SAVE_TOTAL && (!r->value || r->checkpoint_present);
    return !r->value && r->action == COOP_CONTROL_RESUME_ALL;
}
bool coop_control_request_encode(const CoopControlRequest* r, uint8_t out[COOP_CONTROL_REQUEST_BYTES])
{
    if (!out || !request_valid(r)) return false;
    coop_object_write32(out, r->epoch); coop_object_write32(out + 4, r->sequence); coop_object_write32(out + 8, r->map);
    memcpy(out + 12, r->actor, 16); out[28] = (uint8_t)r->action;
    coop_object_write32(out + 29, r->value); coop_object_write32(out + 33, r->proposal); memcpy(out + 37, r->bed.bytes, 17);
    coop_object_write32(out + 54, r->checkpoint_fingerprint); out[58] = r->checkpoint_present;
    memcpy(out + 59, r->description, sizeof(r->description));
    out[123] = r->route_count;
    for (unsigned i = 0; i < COOP_JOURNEY_WAYPOINTS; i++) coop_object_write32(out + 124 + 4 * i, r->route[i]);
    return true;
}
bool coop_control_request_decode(const void* bytes, size_t size, CoopControlRequest* out)
{
    if (!bytes || !out || size != COOP_CONTROL_REQUEST_BYTES) return false;
    const uint8_t* p = bytes;
    CoopControlRequest r = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4), .map = coop_object_read32(p + 8),
        .action = (CoopControlAction)p[28], .value = coop_object_read32(p + 29), .proposal = coop_object_read32(p + 33) };
    memcpy(r.actor, p + 12, 16); memcpy(r.bed.bytes, p + 37, 17);
    r.checkpoint_fingerprint = coop_object_read32(p + 54); r.checkpoint_present = p[58];
    memcpy(r.description, p + 59, sizeof(r.description));
    r.route_count = p[123];
    for (unsigned i = 0; i < COOP_JOURNEY_WAYPOINTS; i++) r.route[i] = coop_object_read32(p + 124 + 4 * i);
    if (!request_valid(&r)) return false;
    *out = r; return true;
}
static bool view_valid(const CoopControlView* v)
{
    if (!v || !v->epoch || !v->revision || v->phase < COOP_CONTROL_IDLE || v->phase > COOP_CONTROL_JOURNEY
        || v->action < COOP_CONTROL_PAUSE || v->action > COOP_CONTROL_TRAVEL || v->notice > COOP_CONTROL_CHANGED
        || !(v->participants & 1) || (v->participants & ~15u) || v->map > INT32_MAX
        || ((v->paused | v->agreed | v->refused) & ~v->participants) || (v->agreed & v->refused) || v->remaining_ms > 30000
        || !route_valid(v->action, v->value, v->route_count, v->route)
        || !coop_save_label_valid(v->description) || v->checkpoint_present > 1 || (!v->checkpoint_present && v->checkpoint_fingerprint)) return false;
    bool checkpoint = v->action == COOP_CONTROL_SAVE || v->action == COOP_CONTROL_LOAD;
    if ((!checkpoint && (v->checkpoint_present || v->checkpoint_fingerprint || v->description[0]))
        || (v->action == COOP_CONTROL_SAVE && (v->value >= COOP_SAVE_TOTAL || v->value == COOP_SAVE_RECOVERY))
        || (v->action == COOP_CONTROL_LOAD && v->value >= COOP_SAVE_TOTAL)
        || (v->action == COOP_CONTROL_SAVE && v->value && !v->description[0])
        || (v->action == COOP_CONTROL_LOAD && v->value && !v->checkpoint_present)) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if (v->statuses[i] > COOP_CONTROL_CHANGED || (!v->sequences[i] && v->statuses[i])) return false;
    if (v->phase == COOP_CONTROL_IDLE) {
        if (v->proposal || v->remaining_ms || v->agreed || v->refused || (v->proposer != COOP_NO_SLOT && (v->proposer >= COOP_MAX_PLAYERS || !(v->participants & (1u << v->proposer))))) return false;
        return v->action == COOP_CONTROL_TRAVEL ? v->completed <= 1000000 : v->action == COOP_CONTROL_REST ? v->value <= 7 && v->completed <= 168 : checkpoint ? !v->completed : !v->value && !v->completed;
    }
    if (!v->proposal || v->proposer >= COOP_MAX_PLAYERS || !(v->participants & (1u << v->proposer))) return false;
    if (v->action == COOP_CONTROL_REST) {
        if (v->value > 7 || v->completed > 168) return false;
    } else if (v->action == COOP_CONTROL_TRAVEL) { if (v->completed > 1000000) return false; }
    else if (!checkpoint || v->completed) return false;
    if (v->phase == COOP_CONTROL_JOURNEY && v->action != COOP_CONTROL_TRAVEL) return false;
    if (v->phase == COOP_CONTROL_RESTING && v->action != COOP_CONTROL_REST) return false;
    if (v->phase == COOP_CONTROL_SAVING && v->action != COOP_CONTROL_SAVE) return false;
    if (v->phase == COOP_CONTROL_LOADING && v->action != COOP_CONTROL_LOAD) return false;
    if (v->phase == COOP_CONTROL_VOTING) return v->remaining_ms && !v->completed;
    return v->agreed == v->participants && !v->refused;
}
bool coop_control_view_encode(const CoopControlView* v, uint8_t out[COOP_CONTROL_VIEW_BYTES])
{
    if (!out || !view_valid(v)) return false;
    memset(out, 0, COOP_CONTROL_VIEW_BYTES);
    coop_object_write32(out, v->epoch); coop_object_write32(out + 4, v->revision);
    out[8] = (uint8_t)v->phase; out[9] = v->paused; out[10] = v->participants; out[11] = v->agreed; out[12] = v->refused;
    out[13] = v->proposer; out[14] = (uint8_t)v->action; out[15] = v->notice;
    coop_object_write32(out + 16, v->proposal); coop_object_write32(out + 20, v->value); coop_object_write32(out + 24, v->completed);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) { coop_object_write32(out + 28 + 4 * i, v->sequences[i]); out[44 + i] = v->statuses[i]; }
    coop_object_write32(out + 48, v->remaining_ms); coop_object_write32(out + 52, v->world_revision); coop_object_write32(out + 56, v->map);
    coop_object_write32(out + 64, v->checkpoint_fingerprint); out[68] = v->checkpoint_present;
    memcpy(out + 69, v->description, sizeof(v->description));
    out[133] = v->route_count;
    for (unsigned i = 0; i < COOP_JOURNEY_WAYPOINTS; i++) coop_object_write32(out + 134 + 4 * i, v->route[i]);
    return true;
}
bool coop_control_view_decode(const void* bytes, size_t size, CoopControlView* out)
{
    if (!bytes || !out || size != COOP_CONTROL_VIEW_BYTES) return false;
    const uint8_t* p = bytes;
    if (coop_object_read32(p + 60)) return false;
    CoopControlView v = { .epoch = coop_object_read32(p), .revision = coop_object_read32(p + 4), .phase = (CoopControlPhase)p[8],
        .paused = p[9], .participants = p[10], .agreed = p[11], .refused = p[12], .proposer = p[13], .action = (CoopControlAction)p[14], .notice = p[15],
        .proposal = coop_object_read32(p + 16), .value = coop_object_read32(p + 20), .completed = coop_object_read32(p + 24),
        .remaining_ms = coop_object_read32(p + 48), .world_revision = coop_object_read32(p + 52), .map = coop_object_read32(p + 56) };
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) { v.sequences[i] = coop_object_read32(p + 28 + 4 * i); v.statuses[i] = p[44 + i]; }
    v.checkpoint_fingerprint = coop_object_read32(p + 64); v.checkpoint_present = p[68];
    memcpy(v.description, p + 69, sizeof(v.description));
    v.route_count = p[133];
    for (unsigned i = 0; i < COOP_JOURNEY_WAYPOINTS; i++) v.route[i] = coop_object_read32(p + 134 + 4 * i);
    if (!view_valid(&v)) return false;
    *out = v; return true;
}
