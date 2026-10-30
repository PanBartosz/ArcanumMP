#ifndef ARCMP_ENGINE_DIALOGUE_H
#define ARCMP_ENGINE_DIALOGUE_H
#include "coop/dialogue.h"
#include "game/dialog.h"
bool coop_engine_dialogue_bind(CoopDialogue* dialogue, CoopSession* session);
void coop_engine_dialogue_unbind(void);
bool coop_engine_dialogue_bound(void);
bool coop_engine_dialogue_can_start(int64_t pc);
/* Native callbacks are queued until the preceding page/closure is acknowledged.
 * Returns true when co-op handled the request, including a deferred start. */
bool coop_engine_dialogue_request(int64_t pc, int64_t npc, int script, int line, int number);
void coop_engine_dialogue_started(void);
bool coop_engine_dialogue_poll(bool accepting);
bool coop_engine_dialogue_pending(void);
/* Retain the authoritative choice page and FIFO callbacks before reconnect
 * tears down connection services. The native save carries them for recovery. */
bool coop_engine_dialogue_preserve(void);
void coop_engine_dialogue_reset(void);
bool coop_engine_dialogue_save(void);
bool coop_engine_dialogue_load(void);
bool coop_engine_dialogue_float(int64_t npc, int64_t pc, const char* text, int speech);
void coop_engine_dialogue_publish(const DialogState* state, bool active);
void coop_engine_dialogue_selected(const char* text);
bool coop_engine_dialogue_present(void* context, const CoopDialogueView* view, bool chooser);
bool coop_engine_dialogue_choose(void* context, unsigned option);
bool coop_engine_dialogue_submit(unsigned option);
bool coop_engine_dialogue_failed(void);
#endif
