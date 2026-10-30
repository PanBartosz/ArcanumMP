#ifndef ARCMP_FOLLOWER_STATE_H
#define ARCMP_FOLLOWER_STATE_H
#include "coop/object_state.h"
enum { COOP_FOLLOWER_REQUEST = 66, COOP_FOLLOWER_RESULT = 67, COOP_FOLLOWER_REQUEST_BYTES = 204 };
typedef enum CoopFollowerOrder {
    COOP_FOLLOW_WALK,
    COOP_FOLLOW_ATTACK,
    COOP_FOLLOW_CLOSE,
    COOP_FOLLOW_SPREAD,
    COOP_FOLLOW_BACK,
    COOP_FOLLOW_WAIT,
    COOP_FOLLOW_RESUME,
    COOP_FOLLOW_DISMISS,
    COOP_FOLLOW_INVENTORY,
    COOP_FOLLOW_BROADCAST,
} CoopFollowerOrder;
typedef enum CoopFollowerStatus {
    COOP_FOLLOW_OK, COOP_FOLLOW_UNAVAILABLE, COOP_FOLLOW_OWNER, COOP_FOLLOW_INVALID,
    COOP_FOLLOW_DISTANCE, COOP_FOLLOW_TARGET, COOP_FOLLOW_TURN, COOP_FOLLOW_STALE,
    COOP_FOLLOW_REFUSED, COOP_FOLLOW_BUSY,
} CoopFollowerStatus;
typedef struct CoopFollowerRequest {
    uint32_t epoch, sequence, map, generation;
    uint8_t actor[16];
    CoopObjectId follower, target;
    uint32_t x, y;
    CoopFollowerOrder order;
    bool force;
    char command[128];
} CoopFollowerRequest;
bool coop_follower_encode(const CoopFollowerRequest* request, uint8_t bytes[COOP_FOLLOWER_REQUEST_BYTES]);
bool coop_follower_decode(const void* bytes, size_t size, CoopFollowerRequest* request);
#endif
