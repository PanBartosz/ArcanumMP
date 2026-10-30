#ifndef ARCMP_ENGINE_OPTIONS_H
#define ARCMP_ENGINE_OPTIONS_H
#include "coop/environment_state.h"
enum { COOP_OPTION_AUTO_ATTACK, COOP_OPTION_SWITCH_WEAPON, COOP_OPTION_ALWAYS_RUN, COOP_OPTION_FOLLOWER_SKILLS, COOP_OPTION_RUN_INTERACTIONS, COOP_OPTION_COUNT };
void coop_engine_options_reset(void);
void coop_engine_options_capture(CoopEnvironmentState* state);
void coop_engine_options_install(const CoopEnvironmentState* state);
int coop_engine_options_difficulty(void); /* -1 outside a restored campaign */
bool coop_engine_options_difficulty_set(unsigned value);
int coop_engine_options_get(int64_t actor, unsigned option); /* -1 uses native defaults */
unsigned coop_engine_options_record(int64_t actor);
unsigned coop_engine_options_defaults(void);
bool coop_engine_options_run_interaction(int64_t actor);
/* Host-controlled debugging preference, persisted with each campaign owner. */
bool coop_engine_options_immortal(int64_t actor);
bool coop_engine_options_immortal_set(unsigned member, bool enabled);
bool coop_engine_options_apply(int64_t actor, unsigned expected, unsigned mask);
void coop_engine_options_tick(void);
#endif
