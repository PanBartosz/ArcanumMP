#ifndef ARCMP_ENVIRONMENT_PROBE_H
#define ARCMP_ENVIRONMENT_PROBE_H
#include <stdbool.h>
bool coop_environment_probe(int argc, char** argv);
void coop_environment_probe_cleanup(void);
#endif
