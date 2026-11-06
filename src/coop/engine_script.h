#ifndef ARCMP_ENGINE_SCRIPT_H
#define ARCMP_ENGINE_SCRIPT_H
#include "game/script.h"
/* Resolve retail SFO_PLAYER for the current host invocation without replacing
 * the host's local UI character. Nested invocations inherit the resolved owner. */
int64_t coop_engine_script_player(const ScriptInvocation* invocation, int64_t inherited);
/* Party access is separate from the physical following/command leader. */
bool coop_engine_party_companion(int64_t npc, int64_t pc);
bool coop_engine_party_lend(int64_t actor, int64_t holder);
/* Look up a story item, retaining the actual native instance/holder. Never
 * changes the low-level personal inventory search used by trade/crafting. */
int64_t coop_engine_story_item(int64_t actor, int name);
#endif
