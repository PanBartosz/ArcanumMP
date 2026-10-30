#ifndef ARCANUM_UI_LOGBOOK_UI_H_
#define ARCANUM_UI_LOGBOOK_UI_H_

#include "game/context.h"

bool logbook_ui_init(GameInitInfo* init_info);
void logbook_ui_exit(void);
void logbook_ui_reset(void);
void logbook_ui_open(int64_t obj);
void logbook_ui_open_quests(int64_t obj);
void logbook_ui_close(void);
bool logbook_ui_is_created(void);
void logbook_ui_check(void);
/* Refresh visible entries without closing the book or losing its bookmark. */
void logbook_ui_tick(uint64_t now);
/* Read-only displayed-data diagnostics for the native integration harness. */
bool logbook_ui_view(int* tab, int* page, int* entries, uint32_t* revision);
bool logbook_ui_entry(int index, int* id, int* state);

#endif /* ARCANUM_UI_LOGBOOK_UI_H_ */
