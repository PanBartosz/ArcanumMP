#ifndef ARCMP_ENGINE_OBJECT_H
#define ARCMP_ENGINE_OBJECT_H
#include "coop/object_state.h"
#include "game/obj_id.h"
typedef struct CoopStagedObject CoopStagedObject;
bool coop_object_id_from_engine(ObjectID id, CoopObjectId* out);
ObjectID coop_object_id_to_engine(CoopObjectId id);
/* These implementations live beside the private object field metadata. Export
 * never freezes/converts the live object. Stage owns an independent field set;
 * malformed/unavailable schema is rejected before modifying live data. */
uint8_t* coop_engine_object_export(int64_t object, size_t* size);
uint8_t* coop_engine_object_export_fields(int64_t object, size_t* size, bool separate_pc_pose);
CoopStagedObject* coop_engine_object_stage(const void* bytes, size_t size);
void coop_engine_object_discard(CoopStagedObject* state);
ObjectID coop_engine_object_id(const CoopStagedObject* state);
ObjectID coop_engine_object_prototype(const CoopStagedObject* state);
bool coop_engine_object_matches(const CoopStagedObject* state, int64_t object);
/* Resolve after all new batch objects have been allocated. Reject dangling IDs
 * rather than silently deleting inventory/follower/reference relationships. */
bool coop_engine_object_resolve(CoopStagedObject* state);
bool coop_engine_object_resolve_except(CoopStagedObject* state, const ObjectID* deleted, unsigned count);
bool coop_engine_object_refers_to(int64_t object, const ObjectID* ids, unsigned count);
int64_t coop_engine_object_location(CoopStagedObject* state, int* x, int* y);
void coop_engine_object_install(CoopStagedObject* state, int64_t object);
#endif
