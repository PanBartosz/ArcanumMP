#include "coop/skills_state.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
int main(void)
{
    CoopSkillRequest r = { .epoch = 1, .sequence = 2, .map = 3, .action = COOP_SKILL_CONCEAL, .value = 1, .generation = 7 }, parsed;
    r.actor[15] = 4;
    uint8_t bytes[COOP_SKILL_REQUEST_BYTES], changed[COOP_SKILL_REQUEST_BYTES];
    CHECK(coop_skill_request_encode(&r, bytes) && coop_skill_request_decode(bytes, sizeof(bytes), &parsed));
    CHECK(parsed.action == COOP_SKILL_CONCEAL && parsed.value == 1 && parsed.generation == 7 && parsed.actor[15] == 4);
    CHECK(!coop_skill_request_decode(bytes, sizeof(bytes) - 1, &parsed));
    memcpy(changed, bytes, sizeof(bytes)); changed[28] = 3; CHECK(!coop_skill_request_decode(changed, sizeof(changed), &parsed));
    memcpy(changed, bytes, sizeof(bytes)); changed[29] = 2; CHECK(!coop_skill_request_decode(changed, sizeof(changed), &parsed));
    r.action = COOP_SKILL_REPAIR; r.value = 0;
    CHECK(!coop_skill_request_encode(&r, bytes));
    r.target.bytes[0] = 2; r.target.bytes[16] = 9;
    for (int action = COOP_SKILL_REPAIR; action <= COOP_SKILL_DISARM; action++) {
        r.action = (CoopSkillAction)action;
        CHECK(coop_skill_request_encode(&r, bytes) && coop_skill_request_decode(bytes, sizeof(bytes), &parsed));
        CHECK(parsed.action == r.action && !memcmp(parsed.target.bytes, r.target.bytes, sizeof(r.target.bytes)));
        r.value = 1; CHECK(coop_skill_request_encode(&r, bytes) == (r.action == COOP_SKILL_DISARM)); r.value = 0;
    }
    r.action = COOP_SKILL_CONCEAL; CHECK(!coop_skill_request_encode(&r, bytes));
    r.action = COOP_SKILL_REPAIR; r.generation = UINT32_MAX; CHECK(!coop_skill_request_encode(&r, bytes)); r.generation = 0;
    memset(r.actor, 0, sizeof(r.actor)); CHECK(!coop_skill_request_encode(&r, bytes));
    CoopSkillFeedback f = { .epoch = 1, .sequence = 2, .map = 3, .revision = 4, .message = 550 }, read;
    f.actor[15] = 1;
    uint8_t feedback[COOP_SKILL_FEEDBACK_BYTES];
    CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read));
    CHECK(read.message == 550 && !read.red && read.revision == 4 && read.actor[15] == 1);
    CHECK(feedback[32] == 2 && feedback[33] == 38);
    feedback[35] = 1; CHECK(!coop_skill_feedback_decode(feedback, sizeof(feedback), &read)); feedback[35] = 0;
    feedback[34] = 2; CHECK(!coop_skill_feedback_decode(feedback, sizeof(feedback), &read));
    f.red = true; CHECK(!coop_skill_feedback_encode(&f, feedback));
    f.message = 551; CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read) && read.red);
    f.message = 499; CHECK(!coop_skill_feedback_encode(&f, feedback));
    f.message = 525; CHECK(!coop_skill_feedback_encode(&f, feedback)); f.red = false;
    CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read) && !read.red);
    f.red = true;
    f.message = 505; f.revision = 0; CHECK(!coop_skill_feedback_encode(&f, feedback));
    f.revision = 1; f.message = 0; f.red = false; f.catalogue = 1;
    CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read));
    CHECK(read.catalogue == 1 && !read.message && !read.red);
    feedback[35] = 2; CHECK(!coop_skill_feedback_decode(feedback, sizeof(feedback), &read));
    f.message = 500; CHECK(!coop_skill_feedback_encode(&f, feedback));
    f.catalogue = 2; f.red = true; f.message = 602;
    CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read) && read.catalogue == 2);
    f.message = 604; CHECK(!coop_skill_feedback_encode(&f, feedback)); f.message = 10000;
    CHECK(coop_skill_feedback_encode(&f, feedback)); f.catalogue = 3; f.message = 19;
    CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read));
    f.message = 20; CHECK(!coop_skill_feedback_encode(&f, feedback)); f.message = 0;
    CHECK(!coop_skill_feedback_encode(&f, feedback));
    f.catalogue = 4; f.message = 50; f.red = false;
    CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read) && read.catalogue == 4);
    f.red = true; CHECK(!coop_skill_feedback_encode(&f, feedback)); f.catalogue = 5;
    CHECK(coop_skill_feedback_encode(&f, feedback)); f.message = 49; CHECK(!coop_skill_feedback_encode(&f, feedback));
    f.catalogue = 6; f.message = 2; f.red = true;
    CHECK(coop_skill_feedback_encode(&f, feedback) && coop_skill_feedback_decode(feedback, sizeof(feedback), &read) && read.catalogue == 6);
    f.message = 3; CHECK(coop_skill_feedback_encode(&f, feedback));
    f.message = 4; CHECK(!coop_skill_feedback_encode(&f, feedback)); f.message = 2;
    f.red = false; CHECK(!coop_skill_feedback_encode(&f, feedback));
    /* Mutated packets must either reject or round-trip canonically. Every
     * decoded feedback remains within the native catalogue/color allowlist. */
    uint32_t state = 0x51A117u;
    r = (CoopSkillRequest){ .epoch = 1, .sequence = 2, .map = 3, .action = COOP_SKILL_REPAIR, .generation = 4 };
    r.actor[15] = 1; r.target.bytes[0] = 2; r.target.bytes[16] = 2;
    CHECK(coop_skill_request_encode(&r, bytes));
    f = (CoopSkillFeedback){ .epoch = 1, .sequence = 2, .map = 3, .revision = 4, .message = 560 }; f.actor[15] = 1;
    CHECK(coop_skill_feedback_encode(&f, feedback));
    for (unsigned i = 0; i < 20000; i++) {
        state = state * 1664525u + 1013904223u;
        memcpy(changed, bytes, sizeof(changed)); changed[state % sizeof(changed)] ^= (uint8_t)(1u << ((state >> 16) & 7));
        if (coop_skill_request_decode(changed, sizeof(changed), &parsed)) {
            uint8_t canonical[COOP_SKILL_REQUEST_BYTES]; CHECK(coop_skill_request_encode(&parsed, canonical));
            CHECK(!memcmp(changed, canonical, sizeof(canonical)));
        }
        uint8_t mutated[COOP_SKILL_FEEDBACK_BYTES]; memcpy(mutated, feedback, sizeof(mutated));
        mutated[(state >> 8) % sizeof(mutated)] ^= (uint8_t)(1u << ((state >> 24) & 7));
        if (coop_skill_feedback_decode(mutated, sizeof(mutated), &read)) {
            uint8_t canonical[COOP_SKILL_FEEDBACK_BYTES]; CHECK(coop_skill_feedback_encode(&read, canonical));
            CHECK(!memcmp(mutated, canonical, sizeof(canonical)));
        }
    }
    puts("skill request/feedback schemas PASS");
    return 0;
}
