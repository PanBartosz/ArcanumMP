#ifndef ARCANUM_UI_INVEN_UI_H_
#define ARCANUM_UI_INVEN_UI_H_

#include "game/context.h"
#include "game/mp_utils.h"
#include "game/target.h"

typedef enum InvenUiMode {
    INVEN_UI_MODE_INVENTORY,
    INVEN_UI_MODE_BARTER,
    INVEN_UI_MODE_LOOT,
    INVEN_UI_MODE_STEAL,
    INVEN_UI_MODE_IDENTIFY,
    INVEN_UI_MODE_NPC_IDENTIFY,
    INVEN_UI_MODE_NPC_REPAIR,
} InvenUiMode;

bool inven_ui_init(GameInitInfo* init_info);
void inven_ui_exit(void);
void inven_ui_reset(void);
bool sub_572370(int64_t pc_obj, int64_t target_obj, int mode);
bool sub_572510(int64_t pc_obj, int64_t target_obj, int mode);
bool inven_ui_open(int64_t pc_obj, int64_t target_obj, int mode);
int64_t inven_ui_play_merchant(void);
void sub_572640(int64_t pc_obj, int64_t target_obj, int mode);
bool inven_ui_create(int64_t pc_obj, int64_t target_obj, int mode);
void inven_ui_destroy(void);
void inven_ui_notify_object_destroyed(int64_t obj);
int inven_ui_is_created(void);
int64_t sub_573600(void);
int64_t inven_ui_drag_item_obj_get(void);
void sub_573630(int64_t obj);
void sub_5736E0(void);
void sub_573730(void);
void sub_573740(int64_t a1, bool a2);
void sub_573840(void);
bool inven_ui_can_open_inven(int64_t pc_obj, int64_t npc_obj);
void sub_575770(void);
void sub_575930(void);
void sub_575C50(int64_t obj);
void inven_ui_update(int64_t obj);
void sub_578B80(int a1);
bool sub_578EA0(Packet81* pkt);
int64_t sub_579760(void);
void inven_ui_coop_stop(void);
void inven_ui_coop_refresh(void);
bool inven_ui_coop_world_click(TargetDescriptor* target);
bool inven_ui_coop_present_loot(int64_t target);
bool inven_ui_coop_present_access(int64_t target, bool steal);
bool inven_ui_coop_present_service(int64_t target, unsigned mode);
void inven_ui_coop_service_feedback(unsigned status, int price, bool bought);
/* Quantity presentation keeps all object flags and inventory changes on host. */
bool inven_ui_coop_quantity_active(void);
int inven_ui_coop_quantity_limit(int available);
bool inven_ui_coop_quantity_submit(int amount);
void inven_ui_coop_quantity_cancel(void);

#endif /* ARCANUM_UI_INVEN_UI_H_ */
