#ifndef ARCMP_MAGIC_STATE_H
#define ARCMP_MAGIC_STATE_H
#include "coop/object_state.h"

#define COOP_MAGIC_RUN_LIMIT 512
#define COOP_MAGIC_SPELL_COUNT 223
#define COOP_MAGIC_CHOICES 4
#define COOP_MAGIC_CHOICE_BYTES 32
#define COOP_MAGIC_VIEW_HEADER (8 + COOP_MAGIC_CHOICES * COOP_MAGIC_CHOICE_BYTES)
#define COOP_MAGIC_ENTRY_BYTES 43
#define COOP_MAGIC_REQUEST_BYTES 86
#define COOP_WRITTEN_BYTES 40
typedef struct CoopWrittenView {
    uint32_t epoch, sequence, map, revision;
    uint8_t reader[16];
    uint8_t type, variant;
    uint32_t number;
} CoopWrittenView;
bool coop_written_encode(const CoopWrittenView* view, uint8_t out[COOP_WRITTEN_BYTES]);
bool coop_written_decode(const void* bytes, size_t size, CoopWrittenView* out);
enum { COOP_MAGIC_FORCE = 1,
    COOP_MAGIC_LOCATION = 2, COOP_MAGIC_OWN_SKILL = 4 };
typedef enum CoopMagicOperation { COOP_MAGIC_CAST,
    COOP_MAGIC_CANCEL,
    COOP_MAGIC_USE_ITEM,
    COOP_MAGIC_DESTINATION } CoopMagicOperation;
typedef struct CoopMagicChoice {
    uint8_t actor[16];
    uint64_t token;
    uint32_t map;
    uint16_t spell;
    uint8_t charge;
} CoopMagicChoice;
typedef struct CoopMagicEntry {
    uint64_t token;
    uint8_t owner[16];
    uint16_t spell;
    CoopObjectId target;
} CoopMagicEntry;
typedef struct CoopMagicView {
    uint32_t count;
    CoopMagicChoice choices[COOP_MAGIC_CHOICES];
    CoopMagicEntry entries[COOP_MAGIC_RUN_LIMIT];
} CoopMagicView;
typedef struct CoopMagicRequest {
    uint32_t epoch, sequence, map;
    uint8_t actor[16];
    CoopMagicOperation operation;
    uint16_t spell;
    uint8_t flags;
    CoopObjectId source, target;
    uint32_t x, y;
    uint64_t token;
    uint32_t generation;
} CoopMagicRequest;
bool coop_magic_view_valid(const CoopMagicView* view);
size_t coop_magic_view_size(const CoopMagicView* view);
bool coop_magic_view_encode(const CoopMagicView* view, void* bytes, size_t size);
bool coop_magic_view_decode(const void* bytes, size_t size, CoopMagicView* out);
bool coop_magic_request_encode(const CoopMagicRequest* request, uint8_t out[COOP_MAGIC_REQUEST_BYTES]);
bool coop_magic_request_decode(const void* bytes, size_t size, CoopMagicRequest* out);
#endif
