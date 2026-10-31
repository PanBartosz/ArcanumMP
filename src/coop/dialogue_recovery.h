#ifndef ARCMP_DIALOGUE_RECOVERY_H
#define ARCMP_DIALOGUE_RECOVERY_H
#include "coop/dialogue.h"
#include "coop/journal.h"
#include "coop/object_state.h"
enum { COOP_DIALOGUE_STARTS = 32,
    COOP_DIALOGUE_ACTION_TEXT = 4096,
    COOP_DIALOGUE_RECOVERY_LIMIT = 131072 };
typedef struct CoopDialogueNotice {
    uint8_t pc_guid[16];
    CoopObjectId npc;
    int32_t speech;
    uint32_t journal_page;
    char speaker[128], text[COOP_DIALOGUE_TEXT];
    CoopTextRef text_ref;
} CoopDialogueNotice;
typedef struct CoopDialogueStart {
    CoopObjectId pc, npc;
    int32_t map, script, line, page;
} CoopDialogueStart;
/* A native choice page or service panel after its entry effects have run. Restoring
 * these values must never re-enter the NPC's Talk script or repeat effects. */
typedef struct CoopDialogueRecovery {
    bool active;
    unsigned queued;
    CoopDialogueStart current, starts[COOP_DIALOGUE_STARTS];
    /* Opening callbacks already admitted during the current conversation
     * chain. Heartbeats must not restart them between scripted speakers. */
    unsigned opened;
    CoopDialogueStart openings[COOP_DIALOGUE_STARTS];
    unsigned notice_count;
    CoopDialogueNotice notices[COOP_DIALOGUE_STARTS];
    int32_t state_script, last_reply, speech, response;
    uint32_t seed, options;
    /* One-based transcript page already containing this native continuation.
     * Zero means it has not been recorded. Network revisions reset on rejoin. */
    uint32_t journal_page, mode;
    char selected[COOP_DIALOGUE_TEXT];
    char reply[COOP_DIALOGUE_TEXT];
    char text[COOP_DIALOGUE_OPTIONS][COOP_DIALOGUE_TEXT];
    int32_t target[COOP_DIALOGUE_OPTIONS], argument[COOP_DIALOGUE_OPTIONS], auxiliary[COOP_DIALOGUE_OPTIONS];
    char actions[COOP_DIALOGUE_OPTIONS][COOP_DIALOGUE_ACTION_TEXT];
    CoopTextRef text_refs[COOP_DIALOGUE_OPTIONS + 2];
} CoopDialogueRecovery;
/* Continuations retain their native line/page semantics. Repeated openings
 * from another human observing the same NPC are part of the same encounter. */
bool coop_dialogue_recovery_admit(CoopDialogueRecovery* state, const CoopDialogueStart* request);
/* Append unacknowledged transport hints before retiring the connection epoch.
 * retained_revision identifies the replay already present at the FIFO head. */
bool coop_dialogue_recovery_capture_notices(CoopDialogueRecovery* state,
    const CoopDialogue* dialogue, const CoopJournal* journal, uint32_t retained_revision);
uint8_t* coop_dialogue_recovery_encode(const CoopDialogueRecovery* state, size_t* size);
bool coop_dialogue_recovery_decode(CoopDialogueRecovery* state, const void* bytes, size_t size);
#endif
