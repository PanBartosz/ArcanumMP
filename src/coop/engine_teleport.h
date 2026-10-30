#ifndef ARCMP_ENGINE_TELEPORT_H
#define ARCMP_ENGINE_TELEPORT_H
#include "coop/magic_state.h"
/* Original native movement callbacks register an already-authorized cast.
 * The caster chooses the destination through replicated state, including item
 * casts whose source may have been consumed after the callback. */
bool coop_engine_teleport_select(int64_t source, int spell);
bool coop_engine_teleport_pending(int64_t actor);
bool coop_engine_teleport_any(void);
void coop_engine_teleport_capture(CoopMagicChoice choices[COOP_MAGIC_CHOICES]);
unsigned coop_engine_teleport_apply(int64_t actor, const CoopMagicRequest* request);
void coop_engine_teleport_reset(void);
bool coop_engine_teleport_save(void);
bool coop_engine_teleport_load(void);
void coop_engine_teleport_ui_tick(void);
bool coop_engine_teleport_submit(uint64_t token, unsigned area);
void coop_engine_teleport_ui_closed(uint64_t token);
#endif
