#ifndef ARCANUM_UI_DIALOG_UI_H_
#define ARCANUM_UI_DIALOG_UI_H_

#include "game/context.h"
#include "game/dialog.h"
#include "coop/dialogue.h"
#include "coop/dialogue_recovery.h"

bool dialog_ui_init(GameInitInfo* init_info);
void dialog_ui_exit(void);
void dialog_ui_reset(void);
bool dialog_ui_is_in_dialog(int64_t obj);
void dialog_ui_start_dialog(int64_t pc_obj, int64_t npc_obj, int script_num, int script_line, int num);
void dialog_ui_end_dialog(int64_t obj, int a2);
bool dialog_ui_is_local_pc_in_dialog(void);
void dialog_ui_notify_dialog_ended(int64_t obj);
void dialog_ui_notify_dialog_started(int64_t obj);
void sub_5681C0(int64_t pc_obj, int64_t npc_obj);
void dialog_ui_float_line(int64_t npc_obj, int64_t pc_obj, const char* str, int speech_id);
void dialog_ui_notify_object_destroyed(int64_t obj);
bool dialog_ui_coop_choose(unsigned option);
/* Host opens the owning human's native companion exchange without a quest dialogue. */
bool dialog_ui_coop_companion_inventory(int64_t pc, int64_t npc);
bool dialog_ui_coop_service_valid(int64_t pc, int64_t npc, unsigned mode);
bool dialog_ui_coop_present(const CoopDialogueView* view, bool chooser);
void dialog_ui_coop_stop(void);
bool dialog_ui_coop_capture(CoopDialogueRecovery* recovery);
bool dialog_ui_coop_restore(const CoopDialogueRecovery* recovery);
/* Called after closing an NPC character sheet or dialogue-opened map. */
void dialog_ui_coop_panel_closed(unsigned mode);
void dialog_ui_coop_float(int64_t npc, int64_t pc, const char* text, int speech);

#endif /* ARCANUM_UI_DIALOG_UI_H_ */
