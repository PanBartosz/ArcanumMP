#ifndef ARCMP_SKILLS_PROBE_H
#define ARCMP_SKILLS_PROBE_H
#include "coop/gameplay.h"
typedef struct CoopSkillsProbe CoopSkillsProbe;
CoopSkillsProbe* coop_skills_probe_create(CoopSession* session, CoopGameplay* gameplay, int member, bool framebuffer);
void coop_skills_probe_destroy(CoopSkillsProbe* probe);
int coop_skills_probe_poll(CoopSkillsProbe* probe, uint64_t now, bool start);
int coop_skills_probe_finish(CoopSkillsProbe* probe);
bool coop_skills_probe_report(CoopSkillsProbe* probe);
#endif
