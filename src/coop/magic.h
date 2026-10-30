#ifndef ARCMP_MAGIC_H
#define ARCMP_MAGIC_H
#include "coop/magic_state.h"
#include "coop/session.h"
#include "coop/world_net.h"
enum { COOP_MAGIC_REQUEST = 47,
    COOP_MAGIC_RESULT = 48,
    COOP_WRITTEN_VIEW = 51,
    COOP_WRITTEN_ACK = 52 };
typedef enum CoopMagicStatus {
    COOP_MAGIC_OK,
    COOP_MAGIC_UNAVAILABLE,
    COOP_MAGIC_OWNERSHIP,
    COOP_MAGIC_TARGET,
    COOP_MAGIC_DISTANCE,
    COOP_MAGIC_TURN,
    COOP_MAGIC_STALE,
    COOP_MAGIC_BUSY,
    COOP_MAGIC_REFUSED,
    COOP_MAGIC_UNKNOWN,
    COOP_MAGIC_FATIGUE,
    COOP_MAGIC_SOURCE,
    COOP_MAGIC_LINE_OF_SIGHT,
} CoopMagicStatus;
typedef struct CoopMagic CoopMagic;
typedef int64_t (*CoopMagicActor)(void* context, uint8_t sender, const uint8_t* common_header);
CoopMagic* coop_magic_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, uint64_t now, CoopMagicActor actor, void* context);
void coop_magic_destroy(CoopMagic* magic);
void coop_magic_retire(CoopMagic* magic);
bool coop_magic_message(CoopMagic* magic, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_magic_poll(CoopMagic* magic, uint64_t now, bool accepting);
bool coop_magic_submit(CoopMagic* magic, const CoopMagicRequest* request);
bool coop_magic_busy(const CoopMagic* magic);
bool coop_magic_settled(const CoopMagic* magic);
bool coop_magic_written(CoopMagic* magic, uint8_t recipient, unsigned type, int number, bool vendigroth);
bool coop_magic_written_last(const CoopMagic* magic, uint8_t recipient, uint8_t out[COOP_WRITTEN_BYTES]);
uint32_t coop_magic_last_result(const CoopMagic* magic, unsigned* status);
unsigned coop_magic_executed(const CoopMagic* magic);
unsigned coop_magic_rejected(const CoopMagic* magic);
#endif
