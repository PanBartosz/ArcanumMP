#ifndef ARCMP_SAVES_UI_H
#define ARCMP_SAVES_UI_H
#include <stdbool.h>
typedef enum CoopSavesMode { COOP_SAVES_SAVE, COOP_SAVES_LOAD, COOP_SAVES_RESUME } CoopSavesMode;
bool coop_saves_ui_open(CoopSavesMode mode);
bool coop_saves_ui_open_autosaves(CoopSavesMode mode);
void coop_saves_ui_close(void);
void coop_saves_ui_tick(void);
bool coop_saves_ui_active(void);
#endif
