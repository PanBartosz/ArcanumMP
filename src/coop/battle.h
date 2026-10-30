#ifndef ARCMP_BATTLE_H
#define ARCMP_BATTLE_H
#include "coop/combat_state.h"
#include "coop/session.h"
#include "coop/world_net.h"
enum { COOP_COMBAT_REQUEST = 45,
    COOP_COMBAT_RESULT = 46 };
typedef enum CoopCombatStatus {
    COOP_COMBAT_OK,
    COOP_COMBAT_UNAVAILABLE,
    COOP_COMBAT_OWNERSHIP,
    COOP_COMBAT_TARGET,
    COOP_COMBAT_DISTANCE,
    COOP_COMBAT_TURN,
    COOP_COMBAT_STALE,
    COOP_COMBAT_BUSY,
    COOP_COMBAT_REFUSED,
    COOP_COMBAT_HOST_ONLY,
} CoopCombatStatus;
typedef struct CoopBattle CoopBattle;
typedef int64_t (*CoopCombatActor)(void* context, uint8_t sender, const uint8_t* common_header);
CoopBattle* coop_battle_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, uint64_t now, CoopCombatActor actor, void* context);
void coop_battle_destroy(CoopBattle* battle);
bool coop_battle_message(CoopBattle* battle, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_battle_poll(CoopBattle* battle, uint64_t now, bool accepting);
bool coop_battle_submit(CoopBattle* battle, int map, const uint8_t actor[16], CoopCombatOperation operation, uint8_t value, CoopObjectId target, uint32_t generation);
bool coop_battle_busy(const CoopBattle* battle);
bool coop_battle_settled(const CoopBattle* battle);
uint32_t coop_battle_last_result(const CoopBattle* battle, unsigned* status);
unsigned coop_battle_executed(const CoopBattle* battle);
unsigned coop_battle_rejected(const CoopBattle* battle);
unsigned coop_battle_continued(const CoopBattle* battle);
#endif
