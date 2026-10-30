#ifndef ARCMP_ENGINE_PROBE_H
#define ARCMP_ENGINE_PROBE_H
#include <stdbool.h>
/* Returns -1 when no explicit probe was requested; never runs during normal play. */
int coop_engine_probe(int argc, char** argv);
/* Capture the healthy party before a fault probe deliberately removes it. */
bool coop_engine_probe_report(const char* mode);
struct CoopDialogueView;
/* Read-only original dialogue graph routing for explicit campaign probes. */
int coop_campaign_probe_option(const struct CoopDialogueView* view, int script, int goal);
#endif
