#ifndef ARCMP_ENCOUNTER_PROBE_H
#define ARCMP_ENCOUNTER_PROBE_H
#include "coop/character.h"
#include <stdbool.h>
#include <stdint.h>
bool coop_encounter_character(unsigned player, CoopCharacter* build);
/* Fixture placement checks terrain and native blocking occupants. */
bool coop_encounter_place_far(int64_t pc, int64_t desired);
bool coop_encounter_place_near(int64_t pc, int64_t enemy);
bool coop_encounter_probe(int argc, char** argv);
void coop_encounter_probe_cleanup(void);
#endif
