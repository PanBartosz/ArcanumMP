#ifndef ARCMP_PROGRESSION_STATE_H
#define ARCMP_PROGRESSION_STATE_H
#include "coop/object_state.h"
#define COOP_PROGRESSION_REQUEST_BYTES 44
typedef enum CoopProgressionOperation { COOP_PROGRESS_OPEN, COOP_PROGRESS_CLOSE, COOP_PROGRESS_EDIT, COOP_PROGRESS_FATE, COOP_PROGRESS_OPTIONS } CoopProgressionOperation;
typedef enum CoopProgressionKind { COOP_PROGRESS_STAT, COOP_PROGRESS_HP, COOP_PROGRESS_FATIGUE, COOP_PROGRESS_SKILL, COOP_PROGRESS_COLLEGE, COOP_PROGRESS_DISCIPLINE, COOP_PROGRESS_SCHEME } CoopProgressionKind;
typedef enum CoopProgressionStatus {
    COOP_PROGRESS_OK, COOP_PROGRESS_UNAVAILABLE, COOP_PROGRESS_OWNERSHIP,
    COOP_PROGRESS_LEASE, COOP_PROGRESS_CHANGED, COOP_PROGRESS_POINTS,
    COOP_PROGRESS_LIMIT, COOP_PROGRESS_REQUIREMENTS, COOP_PROGRESS_BASELINE,
} CoopProgressionStatus;
typedef struct CoopProgressionRequest {
    uint32_t epoch, sequence, map;
    uint8_t actor[16];
    CoopProgressionOperation operation;
    CoopProgressionKind kind;
    uint8_t index, direction; /* 1 increases, 2 refunds; zero for open/close. */
    uint32_t lease, expected, points;
} CoopProgressionRequest;
bool coop_progression_request_encode(const CoopProgressionRequest* request, uint8_t out[COOP_PROGRESSION_REQUEST_BYTES]);
bool coop_progression_request_decode(const void* bytes, size_t size, CoopProgressionRequest* request);
bool coop_progression_selection_valid(CoopProgressionKind kind, unsigned index);
#endif
