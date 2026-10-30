#ifndef ARCMP_ENGINE_CHARACTER_H
#define ARCMP_ENGINE_CHARACTER_H
#include "coop/character.h"
bool coop_engine_character_capture(int64_t pc, CoopCharacter* character);
/* Seed identity only on a fresh generic PC, leaving its points unallocated. */
bool coop_engine_character_identity(int64_t pc, const CoopCharacter* character);
/* Applies only to a fresh level-one generic PC. A failed replay can partially
 * change that disposable object; callers must discard it. */
bool coop_engine_character_apply(int64_t pc, const CoopCharacter* character);
bool coop_engine_character_validate(const CoopCharacter* character);
bool coop_character_probe(void);
bool coop_character_ui_probe(unsigned player, bool framebuffer, CoopCharacter* character);
bool coop_character_edit_probe(unsigned player, bool framebuffer, CoopCharacter* character);
#endif
