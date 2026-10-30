#ifndef ARCMP_MAGIC_PROBE_H
#define ARCMP_MAGIC_PROBE_H
#include "coop/character.h"
#include "coop/gameplay.h"
typedef struct CoopMagicProbe CoopMagicProbe;
bool coop_magic_probe_character(unsigned player, CoopCharacter* build);
CoopMagicProbe* coop_magic_probe_create(CoopSession* session, CoopGameplay* game, int local, bool framebuffer);
bool coop_magic_probe_start(CoopMagicProbe* probe);
int coop_magic_probe_poll(CoopMagicProbe* probe, uint64_t now);
bool coop_magic_probe_report(CoopMagicProbe* probe);
void coop_magic_probe_destroy(CoopMagicProbe* probe);
#endif
