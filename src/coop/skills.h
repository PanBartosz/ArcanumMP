#ifndef ARCMP_SKILLS_H
#define ARCMP_SKILLS_H
#include "coop/session.h"
#include "coop/skills_state.h"
#include "coop/world_net.h"
enum { COOP_SKILL_REQUEST = 53, COOP_SKILL_RESULT = 54, COOP_SKILL_FEEDBACK = 55, COOP_SKILL_FEEDBACK_ACK = 56 };
typedef enum CoopSkillStatus {
    COOP_SKILL_OK, COOP_SKILL_UNAVAILABLE, COOP_SKILL_OWNERSHIP, COOP_SKILL_TARGET,
    COOP_SKILL_DISTANCE, COOP_SKILL_TURN, COOP_SKILL_STALE, COOP_SKILL_BUSY, COOP_SKILL_REFUSED,
} CoopSkillStatus;
typedef struct CoopSkills CoopSkills;
typedef struct CoopItems CoopItems;
typedef int64_t (*CoopSkillActor)(void* context, uint8_t sender, const uint8_t* common_header);
CoopSkills* coop_skills_create(CoopSession* session, CoopWorldNetwork* world, CoopItems* items, uint32_t epoch, uint64_t now, CoopSkillActor actor, void* context);
void coop_skills_destroy(CoopSkills* skills);
bool coop_skills_message(CoopSkills* skills, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_skills_poll(CoopSkills* skills, uint64_t now, bool accepting);
bool coop_skills_submit(CoopSkills* skills, const CoopSkillRequest* request);
bool coop_skills_feedback(CoopSkills* skills, uint8_t recipient, unsigned message, bool red, unsigned catalogue);
bool coop_skills_feedback_last(const CoopSkills* skills, uint8_t recipient, uint8_t out[COOP_SKILL_FEEDBACK_BYTES]);
bool coop_skills_busy(const CoopSkills* skills);
bool coop_skills_settled(const CoopSkills* skills);
uint32_t coop_skills_last_result(const CoopSkills* skills, unsigned* status);
unsigned coop_skills_executed(const CoopSkills* skills);
unsigned coop_skills_rejected(const CoopSkills* skills);
#endif
