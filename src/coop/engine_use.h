#ifndef ARCMP_ENGINE_USE_H
#define ARCMP_ENGINE_USE_H
#include "coop/magic.h"
#include "game/anim.h"
#include "game/obj_id.h"
#include "game/target.h"

/* Local completion state, never serialized or sent to another process. */
#define COOP_USE_PENDING UINT32_MAX
#define COOP_USE_FAILURE (UINT32_MAX - 1)
typedef struct CoopUseTicket {
    AnimID goal;
    ObjectID actor, item, target;
    int64_t location;
    unsigned token, map, generation, status;
    bool tile, force, dispatched, done;
} CoopUseTicket;
unsigned coop_engine_use_execute(int64_t actor, int64_t item, const TargetDescriptor* target, bool force, bool own_skill, CoopUseTicket* ticket);
CoopMagicStatus coop_engine_use_default(int64_t actor, int64_t item, TargetDescriptor* target, bool force);
unsigned coop_engine_use_poll(CoopUseTicket* ticket, bool expired);
void coop_engine_use_release(CoopUseTicket* ticket);
void coop_engine_use_reached(unsigned token, const AnimID* goal, int64_t actor, int64_t item, int64_t target, int64_t location, bool tile);
bool coop_engine_use_settled(void);
#endif
