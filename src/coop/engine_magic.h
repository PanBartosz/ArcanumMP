#ifndef ARCMP_ENGINE_MAGIC_H
#define ARCMP_ENGINE_MAGIC_H
#include "coop/magic.h"
#include "coop/engine_use.h"
#include "game/obj_id.h"
#include "game/target.h"
bool coop_engine_magic_capture(CoopMagicView* view);
bool coop_engine_magic_resolve(const CoopMagicView* view, const ObjectID* removed, unsigned count);
bool coop_engine_magic_install(const CoopMagicView* view);
const CoopMagicView* coop_engine_magic_view(void);
void coop_engine_magic_reset(void);
unsigned coop_engine_magic_execute(int64_t pc, const CoopMagicRequest* request, CoopUseTicket* ticket);
bool coop_engine_magic_target_valid(int64_t actor, int64_t source, int spell, bool force, TargetDescriptor* target);
bool coop_engine_magic_item_apply(int64_t item, const TargetDescriptor* target);
#endif
