#ifndef ARCMP_PROGRESSION_PROBE_H
#define ARCMP_PROGRESSION_PROBE_H
#include "coop/gameplay.h"
typedef struct CoopProgressionProbe CoopProgressionProbe;
CoopProgressionProbe* coop_progression_probe_create(CoopSession* session, CoopGameplay* game, int member, bool framebuffer);
int coop_progression_probe_poll(CoopProgressionProbe* probe, uint64_t now, bool start);
bool coop_progression_probe_report(CoopProgressionProbe* probe);
void coop_progression_probe_destroy(CoopProgressionProbe* probe);
#endif
