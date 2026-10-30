#ifndef ARCMP_AUDIO_PROBE_H
#define ARCMP_AUDIO_PROBE_H
#include "coop/gameplay.h"
typedef struct CoopAudioProbe CoopAudioProbe;
CoopAudioProbe* coop_audio_probe_create(CoopSession* session, CoopGameplay* gameplay, int member, bool framebuffer);
int coop_audio_probe_poll(CoopAudioProbe* probe, uint64_t now, bool start);
bool coop_audio_probe_report(CoopAudioProbe* probe);
void coop_audio_probe_stop(CoopAudioProbe* probe);
void coop_audio_probe_destroy(CoopAudioProbe* probe);
#endif
