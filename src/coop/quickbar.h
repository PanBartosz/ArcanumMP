#ifndef ARCMP_QUICKBAR_H
#define ARCMP_QUICKBAR_H
#include "coop/object_state.h"
#include "coop/identity.h"
enum { COOP_QUICKBAR_COUNT = 12, COOP_QUICKBAR_BYTES = 392, COOP_QUICKBAR_PATH = 110 };
typedef struct CoopQuickbarSlot { uint8_t kind; uint32_t data, prototype; CoopObjectId item; } CoopQuickbarSlot;
typedef struct CoopQuickbar {
    uint8_t owner[COOP_IDENTITY_SIZE], character[16];
    CoopQuickbarSlot slots[COOP_QUICKBAR_COUNT];
} CoopQuickbar;
bool coop_quickbar_encode(const CoopQuickbar* bar, uint8_t bytes[COOP_QUICKBAR_BYTES]);
bool coop_quickbar_decode(CoopQuickbar* bar, const void* bytes, size_t size);
bool coop_quickbar_path(const CoopQuickbar* bar, char path[COOP_QUICKBAR_PATH]);
bool coop_quickbar_save(const CoopQuickbar* bar);
/* Missing/damaged preferences leave the caller's defaults unchanged. */
bool coop_quickbar_load(CoopQuickbar* bar);
#endif
