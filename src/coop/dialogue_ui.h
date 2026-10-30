#ifndef ARCMP_DIALOGUE_UI_H
#define ARCMP_DIALOGUE_UI_H
#include "coop/dialogue.h"
bool coop_dialogue_ui_show(const CoopDialogueView* view, bool chooser, const char* speaker);
bool coop_dialogue_ui_browse(void);
bool coop_dialogue_ui_browsing(void);
unsigned coop_dialogue_ui_index(void);
void coop_dialogue_ui_hide(void);
void coop_dialogue_ui_reset(void);
#endif
