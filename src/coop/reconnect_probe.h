#ifndef ARCMP_RECONNECT_PROBE_H
#define ARCMP_RECONNECT_PROBE_H
#include <stdbool.h>
bool coop_reconnect_probe(int argc,char** argv);
bool coop_reconnect_barrier_probe(int argc,char** argv);
void coop_reconnect_probe_cleanup(void);
#endif
