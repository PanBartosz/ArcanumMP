#include "coop/save_slots.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool coop_save_label_valid(const char* text)
{
    if (!text) return false;
    bool end = false;
    for (unsigned i = 0; i < COOP_CHECKPOINT_DESCRIPTION; i++) {
        unsigned char c = (unsigned char)text[i];
        if (!c) end = true;
        else if (end || c < 32 || c > 126) return false;
    }
    return end;
}
static int compare_owner(const void* left, const void* right) { return memcmp(left, right, COOP_IDENTITY_SIZE); }
bool coop_save_path(const CoopSaveSet* party, unsigned slot, char path[COOP_SAVE_PATH])
{
    if (!party || !path || slot >= COOP_SAVE_TOTAL || !party->members || (party->members & ~15u)) return false;
    if (slot == 0) { strcpy(path, "arcmp-campaign.acsave"); return true; }
    if (slot == COOP_SAVE_RECOVERY) { strcpy(path, "arcmp-recovery.acsave"); return true; }
    uint8_t owners[COOP_MAX_PLAYERS][COOP_IDENTITY_SIZE]; unsigned count = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (party->members & (1u << i)) {
        if (!coop_identity_valid(party->owners[i])) return false;
        memcpy(owners[count++], party->owners[i], COOP_IDENTITY_SIZE);
    }
    qsort(owners, count, COOP_IDENTITY_SIZE, compare_owner);
    uint64_t key = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < 32 + count * COOP_IDENTITY_SIZE; i++) {
        key ^= i < 32 ? party->content[i] : ((const uint8_t*)owners)[i - 32];
        key *= UINT64_C(1099511628211);
    }
    snprintf(path, COOP_SAVE_PATH, "arcmp-save-%016llx-%02u.acsave", (unsigned long long)key, slot);
    return true;
}
bool coop_save_matches(const CoopSaveSet* party, const CoopCheckpointInfo* info)
{
    if (!party || !info || memcmp(party->content, info->content, 32)) return false;
    unsigned required = 0, present = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (party->members & (1u << i)) {
        present++;
        bool found = false;
        for (unsigned j = 0; j < COOP_MAX_PLAYERS; j++) if ((info->members & (1u << j)) && !memcmp(party->owners[i], info->owners[j], 16)) found = true;
        if (!found) return false;
    }
    for (unsigned j = 0; j < COOP_MAX_PLAYERS; j++) required += (info->members >> j) & 1u;
    return present == required;
}
void coop_save_inspect(const CoopSaveSet* party, unsigned slot, CoopSaveEntry* entry)
{
    *entry = (CoopSaveEntry) { .slot = slot, .status = COOP_CHECKPOINT_INVALID };
    char path[COOP_SAVE_PATH];
    if (!coop_save_path(party, slot, path)) return;
    entry->status = coop_checkpoint_inspect(path, &entry->info, &entry->fingerprint, &entry->payload_size, &entry->modified);
    if (entry->status == COOP_CHECKPOINT_OK) {
        if (memcmp(party->content, entry->info.content, 32)) entry->status = COOP_CHECKPOINT_CONTENT;
        else if (!coop_save_matches(party, &entry->info)) entry->status = COOP_CHECKPOINT_OWNER;
    }
}
CoopCheckpointStatus coop_save_remove(const CoopSaveSet* party, unsigned slot, uint32_t fingerprint)
{
    if (!slot || slot >= COOP_SAVE_SLOTS) return COOP_CHECKPOINT_INVALID;
    CoopSaveEntry entry;
    coop_save_inspect(party, slot, &entry);
    if (entry.status != COOP_CHECKPOINT_OK) return entry.status;
    if (entry.fingerprint != fingerprint) return COOP_CHECKPOINT_INVALID;
    char path[COOP_SAVE_PATH];
    if (!coop_save_path(party, slot, path)) return COOP_CHECKPOINT_INVALID;
    return remove(path) == 0 ? COOP_CHECKPOINT_OK : COOP_CHECKPOINT_IO;
}
