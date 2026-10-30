#ifndef ARCMP_ENGINE_PROGRESSION_H
#define ARCMP_ENGINE_PROGRESSION_H
#include "coop/progression_state.h"
typedef struct CoopProgressionBaseline { int values[50]; } CoopProgressionBaseline;
int coop_engine_progression_value(int64_t actor, CoopProgressionKind kind, unsigned index);
bool coop_engine_progression_open(int64_t actor, CoopProgressionBaseline* baseline);
CoopProgressionStatus coop_engine_progression_edit(int64_t actor, const CoopProgressionBaseline* baseline, const CoopProgressionRequest* request);
CoopProgressionStatus coop_engine_progression_fate(int64_t actor, const CoopProgressionRequest* request);
#endif
