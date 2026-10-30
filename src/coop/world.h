#ifndef ARCMP_WORLD_H
#define ARCMP_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define COOP_WORLD_LIMIT (16u * 1024u * 1024u)
/* Dense original towns load over 40,000 instances. The independent 16 MiB
 * byte limit still bounds retained state and every published batch. */
#define COOP_WORLD_OBJECT_LIMIT 131072u
typedef struct CoopWorldTracker CoopWorldTracker;
/* Capture compares canonical object bytes and yields a delta from the last
 * capture, retaining unchanged object records. Baseline includes every loaded
 * instance and campaign subsystem state. Transport must reliably publish each
 * nonempty delta in order. A failed capture requires discarding the tracker. */
CoopWorldTracker* coop_world_tracker_create(bool separate_pc_poses);
void coop_world_tracker_destroy(CoopWorldTracker* tracker);
unsigned coop_world_tracked_objects(const CoopWorldTracker* tracker);
bool coop_world_capture(CoopWorldTracker* tracker, uint8_t** data, size_t* size);
/* Decodes/stages every object and validates IDs/schema/references before
 * replacing any live normal fields. A batch is scoped to the current map. */
bool coop_world_apply(const void* data, size_t size);
unsigned coop_world_upserts(const void* data, size_t size);
unsigned coop_world_deletions(const void* data, size_t size);
#endif
