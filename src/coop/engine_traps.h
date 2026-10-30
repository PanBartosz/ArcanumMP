#ifndef ARCMP_ENGINE_TRAPS_H
#define ARCMP_ENGINE_TRAPS_H
#include <stdbool.h>
#include <stdint.h>
/* Native trap placement keeps the original trap scripts and effects. This
 * catalogue supplies the persistent human responsible for their damage. */
int coop_engine_trap_member(int64_t actor);
bool coop_engine_trap_remember(int64_t trap, int64_t actor);
int64_t coop_engine_trap_owner(int64_t trap);
void coop_engine_trap_forget(int64_t trap);
void coop_engine_traps_reset(void);
bool coop_engine_traps_save(void);
bool coop_engine_traps_load(void);
#endif
