#ifndef ARCMP_COMBAT_STATE_H
#define ARCMP_COMBAT_STATE_H
#include "coop/identity.h"
#include "coop/object_state.h"
#define COOP_COMBAT_ORDER_MAX 32
#define COOP_COMBAT_STATE_BYTES (38 + COOP_COMBAT_ORDER_MAX * 17)
#define COOP_COMBAT_REQUEST_BYTES 51
typedef struct CoopCombatState {
    bool turn_based;
    bool active;
    uint32_t round;
    uint32_t generation;
    uint32_t points;
    CoopObjectId actor;
    uint8_t order_count;
    /* Actual native initiative list, rotated to the current actor. */
    CoopObjectId order[COOP_COMBAT_ORDER_MAX];
} CoopCombatState;
typedef enum CoopCombatOperation {
    COOP_COMBAT_ATTACK,
    COOP_COMBAT_STANCE,
    COOP_COMBAT_END_TURN,
    COOP_COMBAT_POLICY,
    COOP_COMBAT_IMMORTAL, /* value: campaign member << 1 | enabled */
} CoopCombatOperation;
/* Attack value: force bit, native hit location, then per-intent attack mode. */
enum { COOP_ATTACK_FORCE = 1,
    COOP_ATTACK_AIM_SHIFT = 1,
    COOP_ATTACK_AIM_MASK = 6,
    COOP_ATTACK_MODE_SHIFT = 3,
    COOP_ATTACK_MODE_MASK = 24,
    COOP_ATTACK_VALUE_MASK = 31 };
typedef enum CoopAttackMode {
    COOP_ATTACK_DEFAULT,
    COOP_ATTACK_ONCE,
    COOP_ATTACK_REPEAT,
    COOP_ATTACK_STAND,
} CoopAttackMode;
typedef struct CoopCombatRequest {
    uint32_t epoch;
    uint32_t sequence;
    uint32_t map;
    uint8_t actor[COOP_IDENTITY_SIZE];
    CoopCombatOperation operation;
    uint8_t value; /* Attack flags above, desired stance, or desired TB policy. */
    CoopObjectId target;
    uint32_t generation;
} CoopCombatRequest;
bool coop_combat_state_valid(const CoopCombatState* state);
bool coop_combat_state_encode(const CoopCombatState* state, uint8_t out[COOP_COMBAT_STATE_BYTES]);
bool coop_combat_state_decode(const void* data, size_t size, CoopCombatState* out);
bool coop_combat_request_encode(const CoopCombatRequest* request, uint8_t out[COOP_COMBAT_REQUEST_BYTES]);
bool coop_combat_request_decode(const void* data, size_t size, CoopCombatRequest* out);
#endif
