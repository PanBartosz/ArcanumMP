#include "coop/engine_dialogue.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_documents.h"
#include "coop/engine_journal.h"
#include "coop/engine_object.h"
#include "coop/saves_ui.h"
#include "coop/runtime.h"
#include "coop/language_engine.h"
#include "game/ai.h"
#include "game/critter.h"
#include "game/description.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "ui/dialog_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static CoopDialogue* network;
static CoopSession* connection;
static uint32_t conversation;
static bool conversation_open;
static bool failed;
enum { STARTS = COOP_DIALOGUE_STARTS };
typedef struct DialogueStart {
    ObjectID pc, npc;
    int map, script, line, number;
} DialogueStart;
static DialogueStart starts[STARTS];
static unsigned start_count;
static CoopDialogueRecovery recovery;
static const char recovery_path[] = "Save\\Current\\arcmp-dialogue.bin";
static bool dispatching, releasing;
static uint32_t notice_revision;
static uint32_t recorded_revision;
static uint32_t recorded_native_revision;
static uint32_t published_revision;
static CoopDialogueView native_page;
static char last_speaker[COOP_JOURNAL_SPEAKER];
static char selected[COOP_DIALOGUE_TEXT];
static CoopTextRef selected_ref;
static bool publish(CoopDialogueView* view)
{
    if (!coop_dialogue_publish(network, view)) return false;
    view->revision = ++published_revision;
    if (view->mode != COOP_DIALOGUE_FLOAT) native_page = *view;
    return true;
}
static bool same_page(const CoopDialogueView* a, const CoopDialogueView* b)
{
    if (a->active != b->active || a->mode != b->mode || a->speech != b->speech
        || a->option_count != b->option_count || memcmp(a->pc_guid, b->pc_guid, sizeof(a->pc_guid))
        || memcmp(a->npc.bytes, b->npc.bytes, sizeof(a->npc.bytes))
        || strcmp(a->reply, b->reply) || strcmp(a->selected, b->selected)) return false;
    for (unsigned i = 0; i < a->option_count; i++)
        if (strcmp(a->options[i], b->options[i])) return false;
    for (unsigned i=0;i<COOP_DIALOGUE_OPTIONS+2;i++)
        if(a->text_refs[i].key!=b->text_refs[i].key||a->text_refs[i].variant!=b->text_refs[i].variant
            ||strcmp(a->text_refs[i].path,b->text_refs[i].path)) return false;
    return true;
}
bool coop_engine_dialogue_bind(CoopDialogue* dialogue, CoopSession* session)
{
    if (network || !dialogue || !session) return false;
    network = dialogue;
    connection = session;
    conversation = 0;
    conversation_open = failed = false;
    selected[0] = 0;
    selected_ref=(CoopTextRef){0};
    start_count = 0;
    dispatching = releasing = false;
    notice_revision = 0;
    recorded_revision = 0;
    recorded_native_revision = 0;
    published_revision = 0;
    native_page = (CoopDialogueView) { 0 };
    last_speaker[0] = 0;
    return true;
}
void coop_engine_dialogue_unbind(void)
{
    if (!network) return;
    dialog_ui_coop_stop();
    network = NULL;
    connection = NULL;
    conversation_open = false;
    start_count = 0;
}
bool coop_engine_dialogue_bound(void) { return network != NULL; }
bool coop_engine_dialogue_failed(void) { return failed; }
bool coop_engine_dialogue_can_start(int64_t pc)
{
    return !network || (coop_session_is_host(connection) && !conversation_open && !start_count && !recovery.active && !recovery.queued && !recovery.notice_count
        && !dispatching && coop_dialogue_settled(network) && coop_campaign_slot_for_object(pc) >= 0);
}
bool coop_engine_dialogue_pending(void)
{
    return (!network || coop_session_is_host(connection)) && (start_count != 0 || recovery.active || recovery.queued != 0 || recovery.notice_count != 0);
}
void coop_engine_dialogue_reset(void) { recovery = (CoopDialogueRecovery) { 0 }; }
static bool capture_recovery(CoopDialogueRecovery* saved)
{
    *saved = recovery;
    if (!network || !coop_session_is_host(connection)) return true;
    *saved = (CoopDialogueRecovery) { 0 };
    bool good = dialog_ui_coop_capture(saved);
    if (good && saved->active && native_page.active) {
        snprintf(saved->selected, sizeof(saved->selected), "%s", native_page.selected);
        memcpy(saved->text_refs,native_page.text_refs,sizeof(saved->text_refs));
        CoopJournal* journal = coop_engine_journal();
        CoopDialogueView page;
        char speaker[COOP_JOURNAL_SPEAKER];
        /* Only a presented publication can reuse history. This also handles
         * a second rejoin, whose restored page has a new network revision. */
        for (unsigned i = recorded_native_revision == native_page.revision ? coop_journal_count(journal) : 0; i > 0; i--) {
            if (!coop_journal_page(journal, i - 1, &page, speaker)) return false;
            if (same_page(&page, &native_page)) {
                saved->journal_page = i;
                break;
            }
        }
    }
    /* A second drop can happen before the previous continuation was presented. */
    if (!saved->active && recovery.active) {
        *saved = recovery;
        saved->queued = 0;
    }
    for (unsigned i = 0; good && i < recovery.queued + start_count; i++) {
        if (saved->queued == STARTS) { good = false; break; }
        CoopDialogueStart* dest = &saved->starts[saved->queued++];
        if (i < recovery.queued) *dest = recovery.starts[i];
        else {
            const DialogueStart* source = &starts[i - recovery.queued];
            good = coop_object_id_from_engine(source->pc, &dest->pc) && coop_object_id_from_engine(source->npc, &dest->npc);
            dest->map = source->map; dest->script = source->script;
            dest->line = source->line; dest->page = source->number;
        }
    }
    saved->opened = recovery.opened;
    memcpy(saved->openings, recovery.openings, recovery.opened * sizeof(*saved->openings));
    saved->notice_count = recovery.notice_count;
    memcpy(saved->notices, recovery.notices, recovery.notice_count * sizeof(*saved->notices));
    return good && coop_dialogue_recovery_capture_notices(saved, network,
        coop_engine_journal(), notice_revision);
}
bool coop_engine_dialogue_preserve(void)
{
    if (!network || !coop_session_is_host(connection)) return true;
    CoopDialogueRecovery* saved = malloc(sizeof(*saved));
    if (!saved) return false;
    bool good = capture_recovery(saved);
    if (good) { recovery = *saved; start_count = 0; }
    free(saved);
    return good;
}
bool coop_engine_dialogue_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(recovery_path, NULL) || tig_file_remove(recovery_path) == 0;
    CoopDialogueRecovery* saved = malloc(sizeof(*saved));
    if (!saved) return false;
    bool good = capture_recovery(saved);
    size_t size;
    uint8_t* bytes = good ? coop_dialogue_recovery_encode(saved, &size) : NULL;
    free(saved);
    if (!bytes) return false;
    TigFile* file = tig_file_fopen(recovery_path, "wb");
    good = file && tig_file_fwrite(bytes, 1, size, file) == size;
    if (file && tig_file_fclose(file)) good = false;
    free(bytes);
    return good;
}
bool coop_engine_dialogue_load(void)
{
    coop_engine_dialogue_reset();
    if (!coop_campaign_active() || !tig_file_exists(recovery_path, NULL)) return true;
    TigFile* file = tig_file_fopen(recovery_path, "rb");
    if (!file) return false;
    int size = tig_file_filelength(file);
    uint8_t* bytes = size >= 20 && size <= COOP_DIALOGUE_RECOVERY_LIMIT ? malloc((size_t)size) : NULL;
    bool good = bytes && tig_file_fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (tig_file_fclose(file)) good = false;
    good = good && coop_dialogue_recovery_decode(&recovery, bytes, (size_t)size);
    if (good && recovery.active) coop_dialogue_recovery_admit(&recovery, &recovery.current);
    free(bytes);
    return good;
}
bool coop_engine_dialogue_request(int64_t pc, int64_t npc, int script, int line, int number)
{
    CoopSession* active_session = network ? connection : coop_runtime_session();
    if (active_session && coop_campaign_active() && coop_session_is_host(active_session)
        && coop_authority_can_simulate() && pc && npc && obj_handle_is_valid(pc) && obj_handle_is_valid(npc)) {
        /* Native dialogue rejects unavailable speakers before opening a page.
         * Apply that admission rule before retaining a request: an OFF NPC's
         * heartbeat must not interrupt every hour of rest with a conversation
         * that will immediately be discarded on restoration. Ignore temporary
         * animation/combat occupancy here so genuine queued continuations keep
         * their existing ordering and are checked again when dispatched. */
        int speak = ai_can_speak(npc, pc, true);
        if (critter_is_dead(pc) || critter_is_unconscious(pc)
            || speak == AI_SPEAK_CANNOT || speak == AI_SPEAK_DEAD || speak == AI_SPEAK_SLEEP_UNCONSCIOUS) return true;
    }
    if (!network) {
        /* Rest, loading and travel temporarily retire the action services.
         * Native scripts still belong to the shared campaign during that gap;
         * never let them open a host-only conversation. Save permanent identities
         * in the existing recovery sidecar until the next gameplay binds. */
        CoopSession* session = coop_runtime_session();
        if (!session || !coop_campaign_active()) return false;
        if (!coop_session_is_host(session) || !coop_authority_can_simulate()
            || coop_runtime_phase() == COOP_RUNTIME_FAILED) return true;
        if (coop_campaign_slot_for_object(pc) < 0) return true;
        CoopDialogueStart request = { .map = map_current_map(), .script = script, .line = line, .page = number };
        if (!npc || !obj_handle_is_valid(npc)
            || !coop_object_id_from_engine(obj_get_id(pc), &request.pc)
            || !coop_object_id_from_engine(obj_get_id(npc), &request.npc)) {
            coop_runtime_native_failure("A campaign conversation lost its speaker during the transition. Use Recover party.");
            return true;
        }
        for (unsigned i = 0; i < recovery.queued; i++) {
            const CoopDialogueStart* queued = &recovery.starts[i];
            if (queued->map == request.map && queued->script == script && queued->line == line && queued->page == number
                && !memcmp(queued->pc.bytes, request.pc.bytes, sizeof(request.pc.bytes))
                && !memcmp(queued->npc.bytes, request.npc.bytes, sizeof(request.npc.bytes))) return true;
        }
        if (!recovery.active && !recovery.queued) recovery.opened = 0;
        if (!coop_dialogue_recovery_admit(&recovery, &request)) return true;
        if (recovery.queued == STARTS) {
            coop_runtime_native_failure("Too many campaign conversations are waiting for the party. Use Recover party.");
            return true;
        }
        recovery.starts[recovery.queued++] = request;
        fprintf(stderr, "ARCMP retained transition dialogue count=%u script=%d line=%d entry=%d\n", recovery.queued, script, line, number);
        return true;
    }
    if (!coop_session_is_host(connection) || coop_campaign_slot_for_object(pc) < 0) return true;
    if (!npc || !obj_handle_is_valid(npc)) {
        failed = true;
        return true;
    }
    if (releasing) {
        releasing = false;
        dispatching = true;
        return false;
    }
    DialogueStart request = { .pc = obj_get_id(pc), .npc = obj_get_id(npc), .map = map_current_map(), .script = script, .line = line, .number = number };
    bool idle = !dispatching && !conversation_open && !start_count && !recovery.active && !recovery.queued && !recovery.notice_count && coop_dialogue_settled(network);
    if (idle) recovery.opened = 0;
    CoopDialogueStart opening = { .map = request.map, .script = script, .line = line, .page = number };
    if (!coop_object_id_from_engine(request.pc, &opening.pc) || !coop_object_id_from_engine(request.npc, &opening.npc)) {
        failed = true;
        return true;
    }
    /* Keep an opening claimed through the whole chain, including scripted
     * exchanges between NPCs. Removing it from the FIFO must not let its
     * heartbeat insert it again ahead of the next native continuation. */
    if (!coop_dialogue_recovery_admit(&recovery, &opening)) {
        fprintf(stderr, "ARCMP coalesced dialogue opening script=%d entry=%d\n", script, number);
        return true;
    }
    if (idle) {
        dispatching = true;
        return false;
    }
    /* Heartbeats may ask for the same encounter while a chooser is busy.
     * Keep the original FIFO entry and its continuation exactly once. */
    for (unsigned i = 0; i < recovery.queued; i++) {
        const CoopDialogueStart* saved = &recovery.starts[i];
        if (objid_is_equal(coop_object_id_to_engine(saved->pc), request.pc) && objid_is_equal(coop_object_id_to_engine(saved->npc), request.npc)
            && saved->map == request.map && saved->script == script && saved->line == line && saved->page == number) return true;
    }
    for (unsigned i = 0; i < start_count; i++)
        if (objid_is_equal(starts[i].pc, request.pc) && objid_is_equal(starts[i].npc, request.npc)
            && starts[i].map == request.map && starts[i].script == script && starts[i].line == line && starts[i].number == number) return true;
    if (start_count == STARTS) {
        failed = true;
        return true;
    }
    starts[start_count++] = request;
    fprintf(stderr, "ARCMP queued dialogue count=%u script=%d line=%d entry=%d\n", start_count, script, line, number);
    return true;
}
void coop_engine_dialogue_started(void) { dispatching = false; }
bool coop_engine_dialogue_poll(bool accepting)
{
    if (!network || !coop_session_is_host(connection)) return !failed;
    if (accepting && recovery.notice_count && coop_dialogue_settled(network)) {
        /* Keep a notice through delivery ACKs. If rejoin interrupts delivery,
         * its saved journal index prevents another transcript entry. */
        if (notice_revision) {
            memmove(recovery.notices, recovery.notices + 1, --recovery.notice_count * sizeof(*recovery.notices));
            notice_revision = 0;
        }
        if (recovery.notice_count) {
            const CoopDialogueNotice* notice = &recovery.notices[0];
            const CoopCampaignMember* member = NULL;
            for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
                const CoopCampaignMember* candidate = coop_campaign_member(i);
                if (!memcmp(candidate->object_id.d.g.data, notice->pc_guid, sizeof(notice->pc_guid))) member = candidate;
            }
            const CoopPlayer* players = coop_session_players(connection);
            unsigned owner = COOP_MAX_PLAYERS;
            for (unsigned i = 0; member && i < COOP_MAX_PLAYERS; i++)
                if (players[i].connected && !memcmp(players[i].identity, member->owner, COOP_IDENTITY_SIZE)) owner = i;
            if (owner == COOP_MAX_PLAYERS) { failed = true; return false; }
            if (!conversation) conversation++;
            CoopDialogueView view = { .conversation = conversation, .mode = COOP_DIALOGUE_FLOAT,
                .chooser = (uint8_t)owner, .npc = notice->npc, .speech = notice->speech };
            memcpy(view.pc_guid, notice->pc_guid, sizeof(view.pc_guid));
            snprintf(view.chooser_name, sizeof(view.chooser_name), "%s", players[owner].name);
            snprintf(view.reply, sizeof(view.reply), "%s", notice->text);
            view.text_refs[0]=notice->text_ref;
            snprintf(view.selected, sizeof(view.selected), "%s", notice->speaker);
            if (publish(&view)) notice_revision = view.revision;
            else failed = true;
            return !failed;
        }
    }
    if (!dispatching && !conversation_open && !start_count && !recovery.active && !recovery.queued && coop_dialogue_settled(network))
        recovery.opened = 0;
    if (accepting && !dispatching && !conversation_open && coop_dialogue_settled(network)) {
        if (recovery.active) {
            snprintf(selected, sizeof(selected), "%s", recovery.selected);
            coop_language_remember(selected,&recovery.text_refs[1]);
            selected_ref=recovery.text_refs[1];
            if (!dialog_ui_coop_restore(&recovery)) { failed = true; return false; }
            recovery.active = false;
            fprintf(stderr, "ARCMP restored native dialogue mode=%u script=%d page=%d without replaying entry effects\n", recovery.mode, recovery.current.script, recovery.last_reply);
            return !failed;
        }
        if (recovery.queued) {
            if (recovery.queued + start_count > STARTS) { failed = true; return false; }
            memmove(starts + recovery.queued, starts, start_count * sizeof(*starts));
            for (unsigned i = 0; i < recovery.queued; i++) {
                const CoopDialogueStart* s = &recovery.starts[i];
                starts[i] = (DialogueStart) { .pc = coop_object_id_to_engine(s->pc), .npc = coop_object_id_to_engine(s->npc),
                    .map = s->map, .script = s->script, .line = s->line, .number = s->page };
            }
            start_count += recovery.queued;
            recovery.queued = 0;
        }
    }
    if (!accepting || dispatching || conversation_open || !start_count || !coop_dialogue_settled(network)) return !failed;
    DialogueStart request = starts[0];
    memmove(starts, starts + 1, --start_count * sizeof(*starts));
    int64_t pc = obj_pool_perm_lookup(request.pc), npc = obj_pool_perm_lookup(request.npc);
    if (request.map != map_current_map() || !pc || !npc || coop_campaign_slot_for_object(pc) < 0
        || (obj_field_int32_get(npc, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) {
        /* The original caller no longer exists (or the party has left).
         * An obsolete optional callback must not fail the whole campaign. */
        fprintf(stderr, "ARCMP discarded unavailable queued dialogue script=%d entry=%d\n", request.script, request.number);
        return !failed;
    }
    fprintf(stderr, "ARCMP dispatch queued dialogue remaining=%u script=%d entry=%d\n", start_count, request.script, request.number);
    releasing = true;
    dialog_ui_start_dialog(pc, npc, request.script, request.line, request.number);
    dispatching = releasing = false;
    return !failed;
}
bool coop_engine_dialogue_float(int64_t npc, int64_t pc, const char* text, int speech)
{
    CoopSession* session = network ? connection : coop_runtime_session();
    if (!session || !coop_campaign_active()) return false;
    if (!coop_session_is_host(session) || !coop_authority_can_simulate() || coop_runtime_phase() == COOP_RUNTIME_FAILED) return true;
    if (coop_campaign_slot_for_object(pc) < 0) {
        /* Ambient callbacks (including newspaper headlines) pass the NPC as
         * their triggerer. Only a human in the original native vicinity may
         * hear them; a nearest-player search across loaded regions leaks
         * distant speech into the shared journal. Explicit human script
         * recipients still retain their original party announcement. */
        pc = OBJ_HANDLE_NULL;
        if (npc && obj_handle_is_valid(npc)) {
            ObjectList nearby;
            object_list_vicinity(npc, OBJ_TM_PC, &nearby);
            for (ObjectNode* node = nearby.head; node; node = node->next) {
                if (coop_campaign_slot_for_object(node->obj) < 0) continue;
                if (!pc || object_dist(npc, node->obj) < object_dist(npc, pc)) pc = node->obj;
            }
            object_list_destroy(&nearby);
        }
    }
    int member = coop_campaign_slot_for_object(pc);
    if (member < 0 || !npc || !text || !*text) return true;
    const CoopCampaignMember* owner = coop_campaign_member((unsigned)member);
    const CoopPlayer* players = coop_session_players(session);
    unsigned chooser = COOP_MAX_PLAYERS;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if (players[i].connected && !memcmp(players[i].identity, owner->owner, COOP_IDENTITY_SIZE)) chooser = i;
    if (chooser == COOP_MAX_PLAYERS) {
        if (network) failed = true;
        else coop_runtime_native_failure("A story message lost its owning player. Use Recover party.");
        return true;
    }
    if (!conversation) conversation++;
    CoopDialogueView view = { .conversation = conversation, .mode = COOP_DIALOGUE_FLOAT, .chooser = (uint8_t)chooser, .speech = speech };
    memcpy(view.pc_guid, owner->object_id.d.g.data, sizeof(view.pc_guid));
    if (!coop_object_id_from_engine(obj_get_id(npc), &view.npc)) {
        if (network) failed = true;
        else coop_runtime_native_failure("A story message lost its speaker. Use Recover party.");
        return true;
    }
    snprintf(view.chooser_name, sizeof(view.chooser_name), "%s", players[chooser].name);
    snprintf(view.selected, sizeof(view.selected), "%.127s", description_get(obj_field_int32_get(npc, OBJ_F_DESCRIPTION)));
    snprintf(view.reply, sizeof(view.reply), "%s", text);
    view.text_refs[0]=coop_language_reference(text);
    /* Fresh messages must not overtake the retained FIFO on a restored epoch. */
    if (network && !recovery.notice_count) {
        if (!publish(&view)) failed = true;
    } else {
        if (recovery.notice_count == STARTS) {
            coop_runtime_native_failure("Too many story messages are waiting for the party. Use Recover party.");
            return true;
        }
        CoopDialogueNotice* notice = &recovery.notices[recovery.notice_count++];
        *notice = (CoopDialogueNotice) { .npc = view.npc, .speech = view.speech };
        notice->text_ref=view.text_refs[0];
        memcpy(notice->pc_guid, view.pc_guid, sizeof(notice->pc_guid));
        snprintf(notice->speaker, sizeof(notice->speaker), "%.127s", view.selected);
        snprintf(notice->text, sizeof(notice->text), "%s", view.reply);
        fprintf(stderr, "ARCMP retained transition notice count=%u owner=%u\n", recovery.notice_count, chooser);
    }
    return true;
}
void coop_engine_dialogue_selected(const char* text,const CoopTextRef* reference)
{
    if (network) {snprintf(selected, sizeof(selected), "%s", text);selected_ref=reference?*reference:(CoopTextRef){0};}
}
void coop_engine_dialogue_publish(const DialogState* state, bool active)
{
    if (!network || !coop_session_is_host(connection)) return;
    int member = coop_campaign_slot_for_object(state->pc_obj);
    if (member < 0) { failed = true; return; }
    const CoopCampaignMember* pc = coop_campaign_member((unsigned)member);
    const CoopPlayer* players = coop_session_players(connection);
    unsigned chooser = COOP_MAX_PLAYERS;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if (players[i].connected && !memcmp(players[i].identity, pc->owner, COOP_IDENTITY_SIZE)) chooser = i;
    if (chooser == COOP_MAX_PLAYERS || (!conversation_open && conversation == UINT32_MAX)) { failed = true; return; }
    if (!conversation_open) conversation++;
    CoopDialogueView view = { .conversation = conversation, .active = active, .mode = active ? (uint32_t)state->field_17E8 : 0,
        .chooser = (uint8_t)chooser, .speech = state->speech_id,
        .option_count = active && state->field_17E8 == 0 ? (uint32_t)state->num_options : 0 };
    memcpy(view.pc_guid, pc->object_id.d.g.data, sizeof(view.pc_guid));
    if (!coop_object_id_from_engine(obj_get_id(state->npc_obj), &view.npc)) { failed = true; return; }
    snprintf(view.chooser_name, sizeof(view.chooser_name), "%s", players[chooser].name);
    if (active || state->field_17E8 == 4) snprintf(view.reply, sizeof(view.reply), "%s", state->reply);
    snprintf(view.selected, sizeof(view.selected), "%s", selected);
    selected[0] = 0;
    for (unsigned i = 0; i < view.option_count && i < COOP_DIALOGUE_OPTIONS; i++)
        snprintf(view.options[i], sizeof(view.options[i]), "%s", state->options[i]);
    view.text_refs[0]=state->text_refs[0];
    view.text_refs[1]=selected_ref;
    selected_ref=(CoopTextRef){0};
    for(unsigned i=0;i<view.option_count&&i<COOP_DIALOGUE_OPTIONS;i++)view.text_refs[i+2]=state->text_refs[i+1];
    if (!publish(&view)) {
        fprintf(stderr, "ARCMP native dialogue publication refused script=%d page=%d chooser=%u mode=%u options=%u speech=%d\n",
            state->script_num, state->field_17EC, chooser, view.mode, view.option_count, view.speech);
        failed = true;
    }
    conversation_open = active;
}
bool coop_engine_dialogue_present(void* context, const CoopDialogueView* view, bool chooser)
{
    (void)context;
    int64_t npc = obj_pool_perm_lookup(coop_object_id_to_engine(view->npc));
    if (view->active && !npc) return false;
    if (view->active) { coop_documents_ui_close(); coop_saves_ui_close(); }
    if (recorded_revision != view->revision) {
        if (view->mode == COOP_DIALOGUE_FLOAT)
            snprintf(last_speaker, sizeof(last_speaker), "%.127s", view->selected);
        else if (npc)
            snprintf(last_speaker, sizeof(last_speaker), "%s", description_get(obj_field_int32_get(npc, OBJ_F_DESCRIPTION)));
        if (!last_speaker[0]) snprintf(last_speaker, sizeof(last_speaker), "Conversation");
        bool retained = false;
        CoopDialogueNotice* notice = view->mode == COOP_DIALOGUE_FLOAT && recovery.notice_count ? &recovery.notices[0] : NULL;
        if (notice && coop_session_is_host(connection) && view->revision != notice_revision) notice = NULL;
        if (notice && (memcmp(notice->pc_guid, view->pc_guid, sizeof(view->pc_guid))
            || memcmp(notice->npc.bytes, view->npc.bytes, sizeof(view->npc.bytes)) || strcmp(notice->text, view->reply))) notice = NULL;
        if (notice && notice->journal_page) {
            CoopDialogueView previous;
            char speaker[COOP_JOURNAL_SPEAKER];
            retained = coop_journal_page(coop_engine_journal(), notice->journal_page - 1, &previous, speaker)
                && same_page(&previous, view);
        }
        if (view->active && recovery.journal_page) {
            CoopDialogueView previous;
            char speaker[COOP_JOURNAL_SPEAKER];
            retained = coop_journal_page(coop_engine_journal(), recovery.journal_page - 1, &previous, speaker)
                && same_page(&previous, view);
            /* Consume only this restoration. A later identical native page
             * must still be appended as a new conversation. */
            recovery.journal_page = 0;
        }
        if (!retained && !coop_journal_append(coop_engine_journal(), view, last_speaker)) {
            failed = true;
            return false;
        }
        if (notice) notice->journal_page = coop_journal_count(coop_engine_journal());
        /* Replicas consume their saved copy when presented. The host keeps
         * its copy until every ACK, so a new reconnect can replay delivery.
         * Leaving the replica head in place would mistake a second identical
         * native line for a replay of the first one. */
        if (notice && !coop_session_is_host(connection))
            memmove(recovery.notices, recovery.notices + 1, --recovery.notice_count * sizeof(*recovery.notices));
        recorded_revision = view->revision;
        if (view->active) recorded_native_revision = view->revision;
    }
    if (view->mode == COOP_DIALOGUE_FLOAT) {
        int64_t pc = OBJ_HANDLE_NULL;
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            const CoopCampaignMember* member = coop_campaign_member(i);
            if (member && !memcmp(member->object_id.d.g.data, view->pc_guid, sizeof(view->pc_guid))) pc = coop_campaign_object(i);
        }
        CoopDialogueView local=*view;coop_language_view(&local);
        dialog_ui_coop_float(npc, pc, local.reply, view->speech);
        return true;
    }
    return dialog_ui_coop_present(view, chooser);
}
bool coop_engine_dialogue_choose(void* context, unsigned option)
{
    (void)context;
    return coop_authority_can_simulate() && dialog_ui_coop_choose(option);
}
bool coop_engine_dialogue_submit(unsigned option)
{
    CoopGameplay* game = coop_runtime_gameplay();
    if (game && coop_gameplay_finishing(game)) return false;
    const CoopDialogueView* view = coop_dialogue_current(network);
    return view && coop_dialogue_choose(network, view->conversation, view->revision, option);
}
