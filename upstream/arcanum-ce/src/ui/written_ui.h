#ifndef ARCANUM_UI_WRITTEN_UI_H_
#define ARCANUM_UI_WRITTEN_UI_H_

#include "game/context.h"
#include "game/written.h"
#include <stddef.h>

bool written_ui_mod_load(void);
void written_ui_mod_unload(void);
void written_ui_start_obj(int64_t written_obj, int64_t pc_obj);
void written_ui_start_type(WrittenType written_type, int num);
bool written_ui_coop_present(unsigned type, int number, bool vendigroth);
unsigned written_ui_coop_presentations(void);
bool written_ui_coop_state(unsigned* type, int* number, bool* vendigroth);
bool written_ui_coop_valid(unsigned type, int number);
bool written_ui_coop_title(unsigned type, int number, char* title, size_t capacity);
void written_ui_coop_stop(void);
void written_ui_close(void);
void written_ui_newspaper_headline(int num, char* str);

#endif /* ARCANUM_UI_WRITTEN_UI_H_ */
