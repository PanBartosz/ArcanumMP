#ifndef ARCMP_ENGINE_COMBAT_H
#define ARCMP_ENGINE_COMBAT_H
#include "coop/combat_state.h"
#include "game/obj_id.h"
bool coop_engine_combat_capture(CoopCombatState* state);
/* Call after staged object allocation and before committing a world batch. */
bool coop_engine_combat_resolve(const CoopCombatState* state, const ObjectID* removed, unsigned count);
bool coop_engine_combat_install(const CoopCombatState* state);
#endif
