#ifndef ARCMP_TRAINING_PROBE_H
#define ARCMP_TRAINING_PROBE_H
#include "coop/gameplay.h"
/* Controlled retail-dialogue acceptance; borrows the caller's live session. */
bool coop_training_probe(CoopSession* session, bool framebuffer, CoopGameplay** held_gameplay);
#endif
