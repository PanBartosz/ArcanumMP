#ifndef ARCMP_DIALOGUE_H
#define ARCMP_DIALOGUE_H
#include "coop/object_state.h"
#include "coop/session.h"
#include "coop/localization.h"
#define COOP_DIALOGUE_TEXT 1000
#define COOP_DIALOGUE_OPTIONS 5
#define COOP_DIALOGUE_LIMIT 8192
#define COOP_DIALOGUE_CANCEL UINT32_MAX
#define COOP_DIALOGUE_FLOAT 10u
typedef struct CoopDialogueView {
    uint32_t conversation;
    uint32_t revision;
    bool active;
    uint32_t mode;
    uint8_t chooser;
    uint8_t pc_guid[16];
    CoopObjectId npc;
    int32_t speech;
    uint32_t option_count;
    char chooser_name[COOP_NAME_SIZE];
    char reply[COOP_DIALOGUE_TEXT];
    char selected[COOP_DIALOGUE_TEXT];
    char options[COOP_DIALOGUE_OPTIONS][COOP_DIALOGUE_TEXT];
    CoopTextRef text_refs[COOP_DIALOGUE_OPTIONS+2];
} CoopDialogueView;
bool coop_dialogue_view_valid(const CoopDialogueView* view);
uint8_t* coop_dialogue_view_encode(const CoopDialogueView* view, size_t* size);
bool coop_dialogue_view_decode(const void* data, size_t size, CoopDialogueView* view);
enum { COOP_DIALOGUE_BEGIN = 36, COOP_DIALOGUE_CHUNK, COOP_DIALOGUE_ACK,
    COOP_DIALOGUE_CHOICE, COOP_DIALOGUE_RESULT };
typedef struct CoopDialogue CoopDialogue;
/* Presentation returns false while referenced objects are still in transit.
 * It must not partially display an update before it can return true. */
typedef bool (*CoopDialoguePresent)(void* context, const CoopDialogueView* view, bool chooser);
typedef bool (*CoopDialogueChoose)(void* context, unsigned option);
/* now uses the same monotonic clock as poll(), including before the first page. */
CoopDialogue* coop_dialogue_create(CoopSession* session, uint32_t epoch, uint64_t now,
    CoopDialoguePresent present, CoopDialogueChoose choose, void* context);
void coop_dialogue_destroy(CoopDialogue* dialogue);
void coop_dialogue_retire(CoopDialogue* dialogue);
bool coop_dialogue_message(CoopDialogue* dialogue, uint8_t sender, uint8_t type, const uint8_t* payload, size_t size);
bool coop_dialogue_poll(CoopDialogue* dialogue, uint64_t now);
/* Host only; assigns a fresh revision. Queued updates are never coalesced. */
bool coop_dialogue_publish(CoopDialogue* dialogue, const CoopDialogueView* view);
/* The connection selects the actor. Expected conversation/page bind a click to
 * the exact options the user saw; observers and stale requests cannot execute. */
bool coop_dialogue_choose(CoopDialogue* dialogue, uint32_t conversation, uint32_t revision, unsigned option);
const CoopDialogueView* coop_dialogue_current(const CoopDialogue* dialogue);
bool coop_dialogue_settled(const CoopDialogue* dialogue);
/* Host delivery queue, copied without advancing it. Recovery must retain
 * floating messages that have not been acknowledged by every observer. */
unsigned coop_dialogue_pending_count(const CoopDialogue* dialogue);
bool coop_dialogue_pending_view(const CoopDialogue* dialogue, unsigned index,
    CoopDialogueView* view, bool* displayed);
unsigned coop_dialogue_accepted(const CoopDialogue* dialogue);
unsigned coop_dialogue_rejected(const CoopDialogue* dialogue);
#endif
