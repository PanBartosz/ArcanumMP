#ifndef ARCMP_ENGINE_TRANSITION_H
#define ARCMP_ENGINE_TRANSITION_H
#include "game/teleport.h"
#include "game/obj_id.h"
/* A teleport already committed by an authoritative native script. Preserve
 * it independently of connection services so quest effects/fare are not lost
 * or repeated when the owner rejoins or loads the recovery snapshot. */
bool coop_engine_transition_set(const TeleportData* data, ObjectID actor);
bool coop_engine_transition_pending(void);
bool coop_engine_transition_get(TeleportData* data, ObjectID* actor);
void coop_engine_transition_reset(void);
bool coop_engine_transition_save(void);
bool coop_engine_transition_load(void);
#endif
