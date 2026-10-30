#include "coop/service_state.h"
#include "coop/identity.h"
#include <limits.h>
#include <string.h>
static bool valid(const CoopServiceRequest* r)
{
    return r && r->epoch && r->sequence && r->map <= INT32_MAX && coop_identity_valid(r->actor)
        && coop_object_id_valid(&r->item, false) && r->quantity && r->quantity <= 1000000
        && (r->operation == COOP_SERVICE_CRAFT
            ? !r->quote && r->recipe && r->recipe <= INT32_MAX && r->quantity == 1 && ((r->conversation && r->page) || (!r->conversation && !r->page))
            : !r->recipe && r->conversation && r->page && (r->operation == COOP_SERVICE_QUOTE ? !r->quote : r->operation == COOP_SERVICE_BUY && r->quote));
}
bool coop_service_request_encode(const CoopServiceRequest* r, uint8_t out[COOP_SERVICE_REQUEST_BYTES])
{
    if (!out || !valid(r)) return false;
    coop_object_write32(out, r->epoch); coop_object_write32(out + 4, r->sequence); coop_object_write32(out + 8, r->map);
    memcpy(out + 12, r->actor, 16); coop_object_write32(out + 28, r->conversation); coop_object_write32(out + 32, r->page);
    out[36] = (uint8_t)r->operation; memcpy(out + 37, r->item.bytes, 17); coop_object_write32(out + 54, r->quote);
    coop_object_write32(out + 58, r->quantity); coop_object_write32(out + 62, r->recipe);
    return true;
}
bool coop_service_request_decode(const void* bytes, size_t size, CoopServiceRequest* out)
{
    if (!bytes || !out || size != COOP_SERVICE_REQUEST_BYTES) return false;
    const uint8_t* p = bytes;
    CoopServiceRequest r = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4), .map = coop_object_read32(p + 8),
        .conversation = coop_object_read32(p + 28), .page = coop_object_read32(p + 32), .operation = (CoopServiceOperation)p[36], .quote = coop_object_read32(p + 54) };
    memcpy(r.actor, p + 12, 16); memcpy(r.item.bytes, p + 37, 17);
    r.quantity = coop_object_read32(p + 58); r.recipe = coop_object_read32(p + 62);
    if (!valid(&r)) return false;
    *out = r; return true;
}
