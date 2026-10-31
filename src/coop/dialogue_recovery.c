#include "coop/dialogue_recovery.h"
#include "coop/bootstrap.h"
#include "coop/journal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Cursor {
    uint8_t* bytes;
    size_t at, size;
    bool good, reading;
} Cursor;
static bool same_opening(const CoopDialogueStart* a, const CoopDialogueStart* b)
{
    return !memcmp(a->npc.bytes, b->npc.bytes, sizeof(a->npc.bytes))
        && a->map == b->map && a->script == b->script && a->line == b->line && a->page == b->page;
}
bool coop_dialogue_recovery_admit(CoopDialogueRecovery* state, const CoopDialogueStart* request)
{
    if (!state || !request || state->opened > COOP_DIALOGUE_STARTS) return false;
    if (request->line) return true;
    for (unsigned i = 0; i < state->opened; i++)
        if (same_opening(&state->openings[i], request)) return false;
    /* Long native chains remain bounded without refusing new encounters. */
    if (state->opened == COOP_DIALOGUE_STARTS)
        memmove(state->openings, state->openings + 1, --state->opened * sizeof(*state->openings));
    state->openings[state->opened++] = *request;
    return true;
}
bool coop_dialogue_recovery_capture_notices(CoopDialogueRecovery* state,
    const CoopDialogue* dialogue, const CoopJournal* journal, uint32_t retained_revision)
{
    if (!state || !dialogue || !journal || state->notice_count > COOP_DIALOGUE_STARTS) return false;
    for (unsigned i = 0; i < coop_dialogue_pending_count(dialogue); i++) {
        CoopDialogueView view;
        bool displayed;
        if (!coop_dialogue_pending_view(dialogue, i, &view, &displayed)) return false;
        if (view.mode != COOP_DIALOGUE_FLOAT || view.revision == retained_revision) continue;
        if (state->notice_count == COOP_DIALOGUE_STARTS) return false;
        CoopDialogueNotice* notice = &state->notices[state->notice_count++];
        *notice = (CoopDialogueNotice) { .npc = view.npc, .speech = view.speech };
        notice->text_ref = view.text_refs[0];
        memcpy(notice->pc_guid, view.pc_guid, sizeof(notice->pc_guid));
        snprintf(notice->speaker, sizeof(notice->speaker), "%.127s", view.selected);
        snprintf(notice->text, sizeof(notice->text), "%s", view.reply);
        if (displayed) {
            for (unsigned page = coop_journal_count(journal); page; page--) {
                CoopDialogueView previous;
                char speaker[COOP_JOURNAL_SPEAKER];
                if (!coop_journal_page(journal, page - 1, &previous, speaker)) return false;
                if (previous.revision == view.revision && previous.mode == COOP_DIALOGUE_FLOAT
                    && !memcmp(previous.pc_guid, view.pc_guid, sizeof(view.pc_guid))
                    && !memcmp(previous.npc.bytes, view.npc.bytes, sizeof(view.npc.bytes))
                    && previous.speech == view.speech && !strcmp(previous.reply, view.reply)
                    && !strcmp(previous.selected, view.selected)) {
                    notice->journal_page = page;
                    break;
                }
            }
        }
    }
    return true;
}
static void block(Cursor* c, void* value, size_t size)
{
    if (!c->good || size > c->size - c->at) {
        c->good = false;
        return;
    }
    if (c->reading)
        memcpy(value, c->bytes + c->at, size);
    else
        memcpy(c->bytes + c->at, value, size);
    c->at += size;
}
static void word(Cursor* c, uint32_t* value)
{
    uint8_t bytes[4];
    if (!c->reading) coop_object_write32(bytes, *value);
    block(c, bytes, sizeof(bytes));
    if (c->reading && c->good) *value = coop_object_read32(bytes);
}
static void number(Cursor* c, int32_t* value)
{
    uint32_t bits;
    memcpy(&bits, value, sizeof(bits));
    word(c, &bits);
    if (c->reading) memcpy(value, &bits, sizeof(bits));
}
static void string(Cursor* c, char* text, size_t capacity)
{
    uint32_t size = 0;
    if (!c->reading) {
        const char* end = memchr(text, 0, capacity);
        if (!end) {
            c->good = false;
            return;
        }
        size = (uint32_t)(end - text);
    }
    word(c, &size);
    if (size >= capacity) {
        c->good = false;
        return;
    }
    block(c, text, size);
    if (c->reading && c->good) {
        if (memchr(text, 0, size)) c->good = false;
        text[size] = 0;
    }
}
static void start(Cursor* c, CoopDialogueStart* s)
{
    block(c, s->pc.bytes, sizeof(s->pc.bytes));
    block(c, s->npc.bytes, sizeof(s->npc.bytes));
    number(c, &s->map);
    number(c, &s->script);
    number(c, &s->line);
    number(c, &s->page);
    c->good &= coop_object_id_valid(&s->pc, false) && s->pc.bytes[0] == 2
        && coop_object_id_valid(&s->npc, false) && (s->npc.bytes[0] == 2 || s->npc.bytes[0] == 3)
        && s->map >= 0 && s->script >= 0 && s->page >= 0;
}
static void text_reference(Cursor* c, CoopTextRef* ref)
{
    string(c, ref->path, sizeof(ref->path));
    number(c, &ref->key);
    uint32_t variant = ref->variant;
    word(c, &variant);
    if (variant > 2) {
        c->good = false;
        return;
    }
    ref->variant = (uint8_t)variant;
    c->good &= coop_language_ref_valid(ref);
}
static void transfer(Cursor* c, CoopDialogueRecovery* s, unsigned version)
{
    uint32_t active = s->active, queued = s->queued;
    word(c, &active);
    word(c, &queued);
    if (!c->good || active > 1 || queued > COOP_DIALOGUE_STARTS) {
        c->good = false;
        return;
    }
    s->active = active != 0;
    s->queued = queued;
    if (s->active) {
        start(c, &s->current);
        number(c, &s->state_script);
        number(c, &s->last_reply);
        number(c, &s->speech);
        number(c, &s->response);
        word(c, &s->seed);
        word(c, &s->options);
        if (s->options > COOP_DIALOGUE_OPTIONS) {
            c->good = false;
            return;
        }
        string(c, s->reply, sizeof(s->reply));
        for (unsigned i = 0; i < s->options; i++) {
            string(c, s->text[i], sizeof(s->text[i]));
            number(c, &s->target[i]);
            number(c, &s->argument[i]);
            number(c, &s->auxiliary[i]);
            string(c, s->actions[i], sizeof(s->actions[i]));
        }
        if (version >= 2) {
            word(c, &s->journal_page);
            string(c, s->selected, sizeof(s->selected));
            if (s->journal_page > COOP_JOURNAL_PAGES) c->good = false;
        }
        if (version >= 3) word(c, &s->mode);
        if (version >= 6)
            for (unsigned i = 0; i < COOP_DIALOGUE_OPTIONS + 2; i++)
                text_reference(c, &s->text_refs[i]);
        bool panel = s->mode == 3 || (s->mode >= 5 && s->mode <= 9);
        bool companion = s->mode == 3 && !s->current.script && !s->state_script;
        if ((!panel && s->mode) || (!companion && (s->state_script <= 0 || s->current.script <= 0))) c->good = false;
    }
    for (unsigned i = 0; i < s->queued; i++)
        start(c, &s->starts[i]);
    if (version >= 4) {
        uint32_t opened = s->opened;
        word(c, &opened);
        if (!c->good || opened > COOP_DIALOGUE_STARTS) {
            c->good = false;
            return;
        }
        s->opened = opened;
        for (unsigned i = 0; i < opened; i++) {
            start(c, &s->openings[i]);
            if (s->openings[i].line) c->good = false;
            for (unsigned j = 0; j < i; j++)
                if (same_opening(&s->openings[i], &s->openings[j])) c->good = false;
        }
    }
    if (version >= 5) {
        uint32_t count = s->notice_count;
        word(c, &count);
        if (!c->good || count > COOP_DIALOGUE_STARTS) {
            c->good = false;
            return;
        }
        s->notice_count = count;
        for (unsigned i = 0; i < count; i++) {
            CoopDialogueNotice* notice = &s->notices[i];
            block(c, notice->pc_guid, sizeof(notice->pc_guid));
            block(c, notice->npc.bytes, sizeof(notice->npc.bytes));
            number(c, &notice->speech);
            word(c, &notice->journal_page);
            string(c, notice->speaker, sizeof(notice->speaker));
            string(c, notice->text, sizeof(notice->text));
            if (version >= 6) text_reference(c, &notice->text_ref);
            CoopDialogueView view = { .conversation = 1, .revision = 1, .mode = COOP_DIALOGUE_FLOAT, .npc = notice->npc, .speech = notice->speech, .chooser_name = "Traveller" };
            memcpy(view.pc_guid, notice->pc_guid, sizeof(view.pc_guid));
            memcpy(view.reply, notice->text, sizeof(view.reply));
            memcpy(view.selected, notice->speaker, sizeof(notice->speaker));
            c->good &= coop_dialogue_view_valid(&view) && notice->speaker[0]
                && notice->journal_page <= COOP_JOURNAL_PAGES;
        }
    }
}
uint8_t* coop_dialogue_recovery_encode(const CoopDialogueRecovery* state, size_t* size)
{
    if (!state || !size) return NULL;
    uint8_t* bytes = calloc(1, COOP_DIALOGUE_RECOVERY_LIMIT);
    CoopDialogueRecovery* copy = malloc(sizeof(*copy));
    if (!bytes || !copy) {
        free(bytes);
        free(copy);
        return NULL;
    }
    *copy = *state;
    memcpy(bytes, "ACDLGR06", 8);
    Cursor c = { bytes, 8, COOP_DIALOGUE_RECOVERY_LIMIT - 4, true, false };
    transfer(&c, copy, 6);
    free(copy);
    if (!c.good) {
        free(bytes);
        return NULL;
    }
    coop_object_write32(bytes + c.at, coop_snapshot_crc(bytes, c.at));
    *size = c.at + 4;
    return bytes;
}
bool coop_dialogue_recovery_decode(CoopDialogueRecovery* state, const void* data, size_t size)
{
    if (!state || !data || size < 20 || size > COOP_DIALOGUE_RECOVERY_LIMIT) return false;
    const uint8_t* bytes = data;
    unsigned version = !memcmp(bytes, "ACDLGR06", 8) ? 6 : !memcmp(bytes, "ACDLGR05", 8) ? 5
        : !memcmp(bytes, "ACDLGR04", 8)                                                  ? 4
        : !memcmp(bytes, "ACDLGR03", 8)                                                  ? 3
        : !memcmp(bytes, "ACDLGR02", 8)                                                  ? 2
        : !memcmp(bytes, "ACDLGR01", 8)                                                  ? 1
                                                                                         : 0;
    if (!version || coop_object_read32(bytes + size - 4) != coop_snapshot_crc(bytes, size - 4)) return false;
    CoopDialogueRecovery* parsed = calloc(1, sizeof(*parsed));
    if (!parsed) return false;
    Cursor c = { (uint8_t*)data, 8, size - 4, true, true };
    transfer(&c, parsed, version);
    bool good = c.good && c.at == c.size;
    if (good) *state = *parsed;
    free(parsed);
    return good;
}
