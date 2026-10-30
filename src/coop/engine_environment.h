#ifndef ARCMP_ENGINE_ENVIRONMENT_H
#define ARCMP_ENGINE_ENVIRONMENT_H
#include "coop/environment_state.h"
bool coop_engine_environment_capture(CoopEnvironmentState* state);
bool coop_engine_environment_matches_content(const CoopEnvironmentState* state);
bool coop_engine_environment_install(const CoopEnvironmentState* state);
bool coop_engine_environment_save(void);
bool coop_engine_environment_load(void);
void coop_engine_environment_view_begin(void);
void coop_engine_environment_view_end(void);
#endif
