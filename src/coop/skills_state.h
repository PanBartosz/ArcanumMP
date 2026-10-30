#ifndef ARCMP_SKILLS_STATE_H
#define ARCMP_SKILLS_STATE_H
#include "coop/identity.h"
#include "coop/object_state.h"

#define COOP_SKILL_REQUEST_BYTES 51
#define COOP_SKILL_FEEDBACK_BYTES 36
typedef enum CoopSkillAction {
    COOP_SKILL_CONCEAL,
    COOP_SKILL_REPAIR,
    COOP_SKILL_DISARM,
} CoopSkillAction;
typedef struct CoopSkillRequest {
    uint32_t epoch, sequence, map;
    uint8_t actor[COOP_IDENTITY_SIZE];
    CoopSkillAction action;
    uint8_t value;
    CoopObjectId target;
    uint32_t generation;
} CoopSkillRequest;
typedef struct CoopSkillFeedback {
    uint32_t epoch, sequence, map, revision;
    uint8_t actor[COOP_IDENTITY_SIZE];
    uint16_t message;
    bool red;
    uint8_t catalogue; /* 0: skill, 1: trap, 2: spell, 3: item, 4: blessing, 5: curse, 6: trap placement. */
} CoopSkillFeedback;
bool coop_skill_request_encode(const CoopSkillRequest* request, uint8_t out[COOP_SKILL_REQUEST_BYTES]);
bool coop_skill_request_decode(const void* data, size_t size, CoopSkillRequest* out);
bool coop_skill_feedback_encode(const CoopSkillFeedback* feedback, uint8_t out[COOP_SKILL_FEEDBACK_BYTES]);
bool coop_skill_feedback_decode(const void* data, size_t size, CoopSkillFeedback* out);
#endif
