#ifndef ARCMP_ENGINE_SNAPSHOT_H
#define ARCMP_ENGINE_SNAPSHOT_H
#include "coop/identity.h"
#include <stddef.h>
/* Captures a normal private save into the bounded portable bundle. Caller frees
 * the result. Restore validates the bundle before writing the reserved slot;
 * the subsequent engine load is destructive and is not a rollback mechanism. */
uint8_t* coop_engine_snapshot_capture(size_t* size);
bool coop_engine_snapshot_restore(const void* data, size_t size, const uint8_t owner[COOP_IDENTITY_SIZE]);
bool coop_engine_snapshot_restore_host(const void* data, size_t size, const uint8_t owner[COOP_IDENTITY_SIZE]);
#endif
