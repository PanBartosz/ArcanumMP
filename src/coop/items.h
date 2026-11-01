#ifndef ARCMP_ITEMS_H
#define ARCMP_ITEMS_H
#include "coop/object_state.h"
#include "coop/session.h"
#include "coop/world_net.h"
#include "game/anim.h"
enum { COOP_ITEM_REQUEST = 42, COOP_ITEM_RESULT = 43, COOP_LOOT_ACCESS = 44 };
typedef enum CoopItemOperation {
    COOP_ITEM_PICKUP,
    COOP_ITEM_MOVE,
    COOP_ITEM_DROP,
    COOP_ITEM_GIVE,
    COOP_ITEM_ARRANGE,
    COOP_LOOT_OPEN,
    COOP_LOOT_CLOSE,
    COOP_LOOT_TAKE,
    COOP_LOOT_PUT,
    COOP_LOOT_TAKE_ALL,
    COOP_LOOT_MOVE,
    COOP_STEAL_OPEN,
    COOP_ITEM_GIVE_AMOUNT,
    COOP_WORLD_USE,
    COOP_WORLD_EXAMINE,
    COOP_LOOT_TAKE_AMOUNT,
    COOP_LOOT_PUT_AMOUNT,
    COOP_ITEM_DROP_AMOUNT,
    COOP_ITEM_GOLD,
} CoopItemOperation;
enum { COOP_ITEM_UNAVAILABLE = 1000, COOP_ITEM_OWNERSHIP, COOP_ITEM_TARGET,
    COOP_ITEM_DISTANCE, COOP_ITEM_REFUSED, COOP_ITEM_INTERRUPTED, COOP_LOOT_DENIED, COOP_LOOT_PARTIAL,
    COOP_LOOT_LOCKED, COOP_LOOT_JAMMED, COOP_LOOT_HELD };
typedef struct CoopItems CoopItems;
typedef int64_t (*CoopItemActor)(void* context, uint8_t sender, const uint8_t* common_header);
CoopItems* coop_items_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, CoopItemActor actor, void* context);
void coop_items_destroy(CoopItems* items);
void coop_items_retire(CoopItems* items);
bool coop_items_message(CoopItems* items, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_items_poll(CoopItems* items, uint64_t now, bool accepting);
bool coop_items_submit(CoopItems* items, int map, const uint8_t actor[16], CoopItemOperation operation, CoopObjectId item, uint8_t recipient, int location);
bool coop_items_submit_access(CoopItems* items, int map, const uint8_t actor[16], CoopItemOperation operation, CoopObjectId item, uint8_t recipient, int location, uint32_t access);
/* Validates a tracked host use goal immediately before its native script. */
bool coop_items_native_world_used(CoopItems* items, int64_t actor, int64_t target, const AnimID* goal);
bool coop_items_native_world_allowed(CoopItems* items, int64_t actor, int64_t target, const AnimID* goal);
/* Stages the host's native loot callback without opening a remote PC's window. */
void coop_items_native_loot(CoopItems* items, int64_t pc, int64_t target);
int64_t coop_items_loot_target(const CoopItems* items);
uint32_t coop_items_loot_generation(const CoopItems* items);
bool coop_items_has_access(const CoopItems* items, uint8_t sender, int64_t target);
bool coop_items_stealing(const CoopItems* items);
void coop_items_release_loot(CoopItems* items);
bool coop_items_busy(const CoopItems* items);
bool coop_items_settled(const CoopItems* items);
uint32_t coop_items_last_result(const CoopItems* items, unsigned* status);
unsigned coop_items_executed(const CoopItems* items);
unsigned coop_items_rejected(const CoopItems* items);
#endif
