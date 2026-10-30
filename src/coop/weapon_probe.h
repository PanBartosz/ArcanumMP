#ifndef ARCMP_WEAPON_PROBE_H
#define ARCMP_WEAPON_PROBE_H
#include "coop/gameplay.h"
typedef struct CoopWeaponProbe CoopWeaponProbe;
CoopWeaponProbe* coop_weapon_probe_create(CoopSession* session, CoopGameplay* game, int local, bool framebuffer);
bool coop_weapon_probe_start(CoopWeaponProbe* probe);
/* -1 failure, 0 pending, 1 all controllers have completed the fixture. */
int coop_weapon_probe_poll(CoopWeaponProbe* probe, uint64_t now);
bool coop_weapon_probe_report(CoopWeaponProbe* probe);
void coop_weapon_probe_destroy(CoopWeaponProbe* probe);
#endif
