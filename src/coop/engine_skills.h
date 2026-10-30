#ifndef ARCMP_ENGINE_SKILLS_H
#define ARCMP_ENGINE_SKILLS_H
#include "coop/skills.h"
CoopSkillStatus coop_engine_skills_execute(int64_t actor, const CoopSkillRequest* request, CoopItems* items, uint8_t sender);
#endif
