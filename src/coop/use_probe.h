#ifndef ARCMP_USE_PROBE_H
#define ARCMP_USE_PROBE_H
#include "coop/gameplay.h"
typedef struct CoopUseProbe CoopUseProbe;
CoopUseProbe* coop_use_probe_create(CoopSession* session, CoopGameplay* game, int member, bool framebuffer);
int coop_use_probe_poll(CoopUseProbe* probe, uint64_t now, bool start);
bool coop_use_probe_report(CoopUseProbe* probe);
bool coop_use_probe_finish(CoopUseProbe* probe);
void coop_use_probe_destroy(CoopUseProbe* probe);
#endif
