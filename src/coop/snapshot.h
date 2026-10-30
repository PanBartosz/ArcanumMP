#ifndef ARCMP_SNAPSHOT_H
#define ARCMP_SNAPSHOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct CoopSnapshotFiles {
    const uint8_t* index;
    size_t index_size;
    const uint8_t* archive;
    size_t archive_size;
} CoopSnapshotFiles;
/* Portable envelope around the engine's little-endian archive pair. Validates
 * the index's bounds, relative path components, nesting and total data lengths.
 * The returned file spans borrow the bundle; no native structs are serialized. */
bool coop_snapshot_parse(const void* bundle, size_t size, CoopSnapshotFiles* files);
uint8_t* coop_snapshot_pack(const CoopSnapshotFiles* files, size_t* size);
#endif
