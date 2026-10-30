#ifndef ARCMP_ENGINE_SECTOR_H
#define ARCMP_ENGINE_SECTOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct CoopSectorStream CoopSectorStream;
typedef struct CoopSectorTracker CoopSectorTracker;
typedef struct CoopSectorBatch CoopSectorBatch;
struct Sector;
/* Supplemental native save data for structural roofs and static-light lists. */
bool coop_sector_visual_save(struct Sector* sector, const char* directory);
bool coop_sector_visual_load(struct Sector* sector, int64_t id, const char* directory, bool native_light_dif);
void coop_sector_visual_reset(struct Sector* sector);
bool coop_sector_persistence_probe(void);
CoopSectorStream* coop_sector_stream_create(void);
bool coop_sector_stream_poll(CoopSectorStream* stream);
void coop_sector_stream_destroy(CoopSectorStream* stream);
unsigned coop_sector_stream_count(const CoopSectorStream* stream);
CoopSectorTracker* coop_sector_tracker_create(void);
void coop_sector_tracker_destroy(CoopSectorTracker* tracker);
bool coop_sector_capture(CoopSectorTracker* tracker, uint8_t** data, size_t* size);
/* Decodes every record first, then pins all referenced regions until discard. */
CoopSectorBatch* coop_sector_stage(const void* data, size_t size);
void coop_sector_install(CoopSectorBatch* batch);
void coop_sector_discard(CoopSectorBatch* batch);
#endif
