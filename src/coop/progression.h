#ifndef ARCMP_PROGRESSION_H
#define ARCMP_PROGRESSION_H
#include "coop/progression_state.h"
#include "coop/session.h"
#include "coop/world_net.h"
enum { COOP_PROGRESSION_REQUEST = 57, COOP_PROGRESSION_RESULT = 58 };
typedef struct CoopProgression CoopProgression;
typedef int64_t (*CoopProgressionActor)(void* context, uint8_t sender, const uint8_t* common);
CoopProgression* coop_progression_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, uint64_t now, CoopProgressionActor actor, void* context);
void coop_progression_destroy(CoopProgression* progression);
bool coop_progression_message(CoopProgression* progression, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_progression_poll(CoopProgression* progression, uint64_t now, bool accepting);
bool coop_progression_submit(CoopProgression* progression, const CoopProgressionRequest* request);
void coop_progression_editor(CoopProgression* progression, bool open);
bool coop_progression_change(CoopProgression* progression, CoopProgressionKind kind, unsigned index, bool increase);
bool coop_progression_option(CoopProgression* progression, unsigned option, bool value);
bool coop_progression_defaults(CoopProgression* progression, unsigned mask);
bool coop_progression_settled(const CoopProgression* progression);
bool coop_progression_busy(const CoopProgression* progression);
bool coop_progression_editing(const CoopProgression* progression);
uint32_t coop_progression_lease(const CoopProgression* progression);
uint32_t coop_progression_last_result(const CoopProgression* progression, unsigned* status);
unsigned coop_progression_executed(const CoopProgression* progression);
unsigned coop_progression_rejected(const CoopProgression* progression);
#endif
