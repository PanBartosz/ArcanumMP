#ifndef ARCMP_CHECKPOINT_H
#define ARCMP_CHECKPOINT_H
#include "coop/session.h"
enum { COOP_CHECKPOINT_HEADER = 196, COOP_CHECKPOINT_DESCRIPTION = 64 };
typedef struct CoopCheckpointInfo {
    uint8_t content[32], owners[COOP_MAX_PLAYERS][COOP_IDENTITY_SIZE], members;
    uint32_t epoch, map;
    char description[COOP_CHECKPOINT_DESCRIPTION];
} CoopCheckpointInfo;
typedef enum CoopCheckpointStatus {
    COOP_CHECKPOINT_OK, COOP_CHECKPOINT_MISSING, COOP_CHECKPOINT_INVALID,
    COOP_CHECKPOINT_CONTENT, COOP_CHECKPOINT_OWNER, COOP_CHECKPOINT_IO,
    /* Replacement committed; parent-directory durability could not be confirmed. */
    COOP_CHECKPOINT_COMMITTED_UNSYNCED,
} CoopCheckpointStatus;
/* One complete native snapshot per file. Failed precommit writes preserve the
 * old checkpoint. COMMITTED_UNSYNCED also denotes a committed valid file. */
CoopCheckpointStatus coop_checkpoint_save(const char* path, const CoopCheckpointInfo* info, const void* snapshot, size_t size);
/* The result owns *snapshot on success. Outputs remain untouched on failure. */
CoopCheckpointStatus coop_checkpoint_load(const char* path, const uint8_t content[32], const uint8_t owner[COOP_IDENTITY_SIZE], CoopCheckpointInfo* info, uint8_t** snapshot, size_t* size);
/* Header-only browser preview; payload integrity is checked on actual load. */
CoopCheckpointStatus coop_checkpoint_inspect(const char* path, CoopCheckpointInfo* info, uint32_t* fingerprint, uint32_t* payload_size, uint64_t* modified);
/* Binds consent to the exact header, including its payload checksum. */
CoopCheckpointStatus coop_checkpoint_load_checked(const char* path, const uint8_t content[32], const uint8_t owner[COOP_IDENTITY_SIZE], uint32_t fingerprint, CoopCheckpointInfo* info, uint8_t** snapshot, size_t* size);
#endif
