#ifndef ARCMP_ENGINE_THEFT_H
#define ARCMP_ENGINE_THEFT_H
#include "game/anim.h"
#include "game/obj_id.h"
#include <stdint.h>
#define COOP_THEFT_PENDING UINT32_MAX
#define COOP_THEFT_FAILURE (UINT32_MAX - 1)
typedef bool (*CoopTheftAllowed)(void* context);
typedef struct CoopTheftTicket {
    AnimID goal;
    ObjectID actor, target, item;
    unsigned token, map, generation, status;
    bool plant, dispatched, done, success;
    CoopTheftAllowed allowed;
    void* context;
} CoopTheftTicket;
unsigned coop_engine_theft_start(CoopTheftTicket* ticket, int64_t actor, int64_t target, int64_t item, bool plant, CoopTheftAllowed allowed, void* context);
unsigned coop_engine_theft_poll(CoopTheftTicket* ticket, bool expired);
bool coop_engine_theft_reached(unsigned token, const AnimID* goal, int64_t actor, int64_t target, int64_t item, bool plant);
void coop_engine_theft_release(CoopTheftTicket* ticket);
#endif
