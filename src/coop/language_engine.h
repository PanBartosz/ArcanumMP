#ifndef ARCMP_LANGUAGE_ENGINE_H
#define ARCMP_LANGUAGE_ENGINE_H
#include "coop/dialogue.h"
#include "coop/localization.h"
void coop_language_engine_init(void);
void coop_language_view(CoopDialogueView* view);
const char* coop_language_dialogue(const char* text, int64_t pc, int64_t npc, char* buffer, size_t size);
#endif
