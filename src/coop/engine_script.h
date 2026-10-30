#ifndef ARCMP_ENGINE_SCRIPT_H
#define ARCMP_ENGINE_SCRIPT_H
#include "game/script.h"
/* Resolve retail SFO_PLAYER for the current host invocation without replacing
 * the host's local UI character. Nested invocations inherit the resolved owner. */
int64_t coop_engine_script_player(const ScriptInvocation* invocation, int64_t inherited);
#endif
