#ifndef ARCMP_ENGINE_REST_H
#define ARCMP_ENGINE_REST_H
#include "coop/control_state.h"
#include "game/obj_id.h"
enum { COOP_REST_MAX_ACTORS = 256 };
typedef struct CoopRest {
    ObjectID actors[COOP_REST_MAX_ACTORS], initiator, bed;
    unsigned count, choice, completed, map;
    uint32_t epoch;
    uint64_t next;
    bool waiting, using_bed, active;
} CoopRest;
unsigned coop_engine_rest_validate(int64_t actor, int64_t bed, unsigned choice);
unsigned coop_engine_rest_begin(CoopRest* rest, int64_t actor, int64_t bed, unsigned choice, uint64_t now);
/* One native hour per call at most; caller runs the native scheduler between
 * calls and captures only after one last scheduler tick. */
unsigned coop_engine_rest_step(CoopRest* rest, uint64_t now, bool cancel, bool* complete);
void coop_engine_rest_end(CoopRest* rest);
#endif
