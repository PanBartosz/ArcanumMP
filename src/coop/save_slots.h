#ifndef ARCMP_SAVE_SLOTS_H
#define ARCMP_SAVE_SLOTS_H
#include "coop/checkpoint.h"
enum { COOP_SAVE_SLOTS = 64, COOP_SAVE_RECOVERY = 64, COOP_SAVE_AUTOSAVE_FIRST = 65,
    COOP_SAVE_AUTOSAVES = 3, COOP_SAVE_TOTAL = 68, COOP_SAVE_PATH = 80,
    COOP_SAVE_PAGE_ROWS = 4, COOP_SAVE_PAGES = 17 };
typedef struct CoopSaveSet {
    uint8_t content[32], owners[COOP_MAX_PLAYERS][COOP_IDENTITY_SIZE], members;
} CoopSaveSet;
typedef struct CoopSaveEntry {
    unsigned slot;
    CoopCheckpointStatus status;
    CoopCheckpointInfo info;
    uint32_t fingerprint, payload_size;
    uint64_t modified;
} CoopSaveEntry;
bool coop_save_path(const CoopSaveSet* party, unsigned slot, char path[COOP_SAVE_PATH]);
bool coop_save_matches(const CoopSaveSet* party, const CoopCheckpointInfo* info);
bool coop_save_label_valid(const char* text);
void coop_save_inspect(const CoopSaveSet* party, unsigned slot, CoopSaveEntry* entry);
/* Removes only a selected local named copy whose header still matches. */
CoopCheckpointStatus coop_save_remove(const CoopSaveSet* party, unsigned slot, uint32_t fingerprint);
#endif
