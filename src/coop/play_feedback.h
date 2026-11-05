#ifndef ARCMP_PLAY_FEEDBACK_H
#define ARCMP_PLAY_FEEDBACK_H
#include "coop/session.h"
void coop_feedback_reset(void);
void coop_feedback_tick(uint64_t now);
void coop_feedback_network(CoopSession* session, uint32_t epoch, unsigned ready);
bool coop_feedback_receive(uint8_t sender, const uint8_t* bytes, size_t size);
#endif
