#include "coop/dialogue.h"
#include "coop/dialogue_recovery.h"
#include "coop/bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static CoopSession* sessions[4];
static CoopDialogue* dialogues[4];
static unsigned seen[4], choices;
static bool hold_last;
static uint64_t now = 100;
static CoopDialogueView fixture(void)
{
    CoopDialogueView v = { .conversation = 7, .revision = 1, .active = true,
        .chooser = 1, .speech = -1, .option_count = 5 };
    v.npc.bytes[0] = 2;
    v.npc.bytes[1] = 1;
    v.pc_guid[0] = 2;
    strcpy(v.chooser_name, "Traveller");
    memset(v.reply, 'R', sizeof(v.reply) - 1);
    memset(v.selected, 'S', sizeof(v.selected) - 1);
    for (unsigned i = 0; i < v.option_count; i++) memset(v.options[i], 'A' + i, sizeof(v.options[i]) - 1);
    return v;
}
static bool present(void* context, const CoopDialogueView* v, bool chooser)
{
    unsigned i = (unsigned)(uintptr_t)context;
    if (hold_last && i == 3) return false;
    CHECK(chooser == (i == 1) && v->revision == seen[i] + 1);
    seen[i]++;
    return true;
}
static bool choose(void* context, unsigned option)
{
    CHECK((uintptr_t)context == 0 && (option == 2 || option == COOP_DIALOGUE_CANCEL));
    choices++;
    CoopDialogueView v = fixture();
    if (option == COOP_DIALOGUE_CANCEL) {
        v.conversation = coop_dialogue_current(dialogues[0])->conversation;
        v.active = false;
        v.option_count = 0;
    }
    strcpy(v.reply, "Next page\nHost actions ran once.");
    CHECK(coop_dialogue_publish(dialogues[0], &v));
    return true;
}
static bool message(void* context, uint8_t sender, uint8_t type, const uint8_t* p, size_t n)
{
    return coop_dialogue_message(context, sender, type, p, n);
}
static void pump(unsigned rounds)
{
    while (rounds--) {
        for (unsigned i = 0; i < 4; i++) {
            coop_session_poll(sessions[i], now);
            if (dialogues[i]) CHECK(coop_dialogue_poll(dialogues[i], now));
        }
        now++;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay = { 0, 1000000 };
        nanosleep(&delay, NULL);
#endif
    }
}
int main(void)
{
    CoopDialogueView v = fixture(), decoded;
    size_t n;
    uint8_t* bytes = coop_dialogue_view_encode(&v, &n);
    CHECK(bytes && n > 7000 && coop_dialogue_view_decode(bytes, n, &decoded));
    CHECK(!memcmp(bytes, "ACD1\0\0\0\7\0\0\0\1\0\0\0\1", 16));
    CHECK(!strcmp(v.reply, decoded.reply) && !strcmp(v.options[4], decoded.options[4]));
    memset(&decoded, 0x42, sizeof(decoded));
    CoopDialogueView retained = decoded;
    CHECK(!coop_dialogue_view_decode(bytes, n - 1, &decoded) && !memcmp(&decoded, &retained, sizeof(decoded)));
    uint32_t random = 42;
    for (unsigned i = 0; i < 10000; i++) {
        random = random * 1664525u + 1013904223u;
        size_t at = random % n;
        uint8_t saved = bytes[at];
        bytes[at] ^= (uint8_t)(random >> 24);
        if (coop_dialogue_view_decode(bytes, n, &decoded)) {
            size_t m;
            uint8_t* encoded = coop_dialogue_view_encode(&decoded, &m);
            CHECK(encoded && m == n && !memcmp(bytes, encoded, n));
            free(encoded);
        }
        bytes[at] = saved;
    }
    free(bytes);
    for (unsigned i = 0; i < 4; i++) {
        CoopConfig config = { 0 };
        snprintf(config.name, sizeof(config.name), "Dialogue %u", i);
        sessions[i] = coop_session_create(&config);
        CHECK(sessions[i]);
    }
    CHECK(coop_session_host(sessions[0], 0, now));
    for (unsigned i = 1; i < 4; i++) CHECK(coop_session_join(sessions[i], "127.0.0.1", coop_session_port(sessions[0]), now));
    pump(60);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_session_phase(sessions[i]) == COOP_LOBBY);
        dialogues[i] = coop_dialogue_create(sessions[i], 42, now, present, choose, (void*)(uintptr_t)i);
        CHECK(dialogues[i]);
        coop_session_set_handler(sessions[i], message, dialogues[i]);
    }
    hold_last = true;
    CHECK(coop_dialogue_publish(dialogues[0], &v));
    /* Fill queues without polling sockets; publication remains immutable. */
    for (unsigned i = 0; i < 100; i++) CHECK(coop_dialogue_poll(dialogues[0], now));
    pump(100);
    CHECK(seen[0] == 1 && seen[1] == 1 && seen[2] == 1 && seen[3] == 0);
    CHECK(!coop_dialogue_choose(dialogues[2], 7, 1, 2));
    CHECK(coop_dialogue_choose(dialogues[1], 7, 1, 2));
    pump(30);
    CHECK(choices == 0 && !coop_dialogue_settled(dialogues[0]));
    hold_last = false;
    pump(100);
    CHECK(choices == 1 && coop_dialogue_accepted(dialogues[0]) == 1);
    for (unsigned i = 0; i < 4; i++) CHECK(seen[i] == 2 && coop_dialogue_current(dialogues[i])->revision == 2);
    CHECK(!coop_dialogue_choose(dialogues[1], 7, 1, 2));
    /* Floating quest hints use ordered ACKs without replacing the chooser's
     * active native options or their expected page revision. */
    CoopDialogueView hint = v;
    hint.active = false;
    hint.mode = COOP_DIALOGUE_FLOAT;
    hint.option_count = 0;
    hold_last = true;
    CHECK(coop_dialogue_publish(dialogues[0], &hint));
    bool displayed;
    CHECK(coop_dialogue_pending_count(dialogues[0]) == 1);
    CHECK(coop_dialogue_pending_view(dialogues[0], 0, &decoded, &displayed));
    CHECK(!displayed && decoded.mode == COOP_DIALOGUE_FLOAT && decoded.revision == 3);
    CHECK(!strcmp(decoded.reply, hint.reply) && !memcmp(decoded.pc_guid, hint.pc_guid, 16));
    CHECK(!coop_dialogue_pending_view(dialogues[0], 1, &decoded, &displayed));
    CHECK(!coop_dialogue_pending_count(dialogues[1]));
    pump(30);
    CHECK(!coop_dialogue_settled(dialogues[0]));
    CHECK(coop_dialogue_pending_view(dialogues[0], 0, &decoded, &displayed) && displayed);
    CHECK(decoded.revision == 3 && decoded.mode == COOP_DIALOGUE_FLOAT);
    /* Reconnection snapshots retain delivery, not just the active chooser page.
     * An identical later hint is a distinct event with no journal entry yet. */
    CoopJournal* journal = coop_journal_create();
    CHECK(journal && coop_journal_append(journal, &decoded, "Speaker"));
    CoopDialogueRecovery recovery = { 0 }, restored;
    CHECK(coop_dialogue_recovery_capture_notices(&recovery, dialogues[0], journal, 0));
    CHECK(recovery.notice_count == 1 && recovery.notices[0].journal_page == 1);
    CHECK(coop_dialogue_recovery_capture_notices(&recovery, dialogues[0], journal, 3));
    CHECK(recovery.notice_count == 1); /* Its replay is already retained. */
    CoopDialogue* pending = coop_dialogue_create(sessions[0], 43, now, NULL, NULL, NULL);
    CHECK(pending && coop_dialogue_publish(pending, &hint));
    CHECK(coop_dialogue_publish(pending, &hint));
    CHECK(coop_dialogue_recovery_capture_notices(&recovery, pending, journal, 0));
    CHECK(recovery.notice_count == 3 && !recovery.notices[1].journal_page && !recovery.notices[2].journal_page);
    bytes = coop_dialogue_recovery_encode(&recovery, &n);
    CHECK(bytes && coop_dialogue_recovery_decode(&restored, bytes, n));
    CHECK(restored.notice_count == 3 && !strcmp(restored.notices[0].text, hint.reply));
    free(bytes);
    coop_dialogue_destroy(pending);
    coop_journal_destroy(journal);
    hold_last = false;
    pump(100);
    CHECK(!coop_dialogue_pending_count(dialogues[0]));
    for (unsigned i = 0; i < 4; i++)
        CHECK(seen[i] == 3 && coop_dialogue_current(dialogues[i])->active && coop_dialogue_current(dialogues[i])->revision == 2);
    /* Closing and immediately reopening must deliver both pages in order. */
    v.active = false;
    v.option_count = 0;
    CHECK(coop_dialogue_publish(dialogues[0], &v));
    v.active = true;
    v.option_count = 5;
    v.conversation = 8;
    CHECK(coop_dialogue_publish(dialogues[0], &v));
    pump(100);
    for (unsigned i = 0; i < 4; i++)
        CHECK(seen[i] == 5 && coop_dialogue_current(dialogues[i])->conversation == 8);
    CHECK(!coop_dialogue_choose(dialogues[2], 8, 5, COOP_DIALOGUE_CANCEL));
    CHECK(coop_dialogue_choose(dialogues[1], 8, 5, COOP_DIALOGUE_CANCEL));
    pump(100);
    CHECK(choices == 2 && coop_dialogue_accepted(dialogues[0]) == 2);
    for (unsigned i = 0; i < 4; i++)
        CHECK(seen[i] == 6 && !coop_dialogue_current(dialogues[i])->active);
    uint8_t request[20];
    coop_object_write32(request, 42);
    coop_object_write32(request + 4, 1);
    coop_object_write32(request + 8, 8);
    coop_object_write32(request + 12, 4);
    coop_object_write32(request + 16, 2);
    /* A forged observer request passes syntax but never executes an action. */
    CHECK(coop_dialogue_message(dialogues[0], 2, COOP_DIALOGUE_CHOICE, request, sizeof(request)));
    CHECK(coop_dialogue_poll(dialogues[0], now));
    CHECK(choices == 2 && coop_dialogue_rejected(dialogues[0]) == 1);
    CHECK(!coop_dialogue_message(dialogues[0], 2, COOP_DIALOGUE_CHOICE, request, sizeof(request)));
    coop_object_write32(request + 4, 3);
    coop_object_write32(request + 12, 1); // Stale owner page.
    CHECK(coop_dialogue_message(dialogues[0], 1, COOP_DIALOGUE_CHOICE, request, sizeof(request)));
    CHECK(coop_dialogue_poll(dialogues[0], now));
    coop_object_write32(request + 4, 4);
    coop_object_write32(request + 12, 4);
    coop_object_write32(request + 16, 5); // Invalid option on current owner page.
    CHECK(coop_dialogue_message(dialogues[0], 1, COOP_DIALOGUE_CHOICE, request, sizeof(request)));
    CHECK(coop_dialogue_poll(dialogues[0], now));
    CHECK(choices == 2 && coop_dialogue_rejected(dialogues[0]) == 3);
    /* A wizard/long lobby can precede the first published page and first poll.
     * Its deadline starts at service creation, not process uptime zero. */
    CoopDialogue* invalid = coop_dialogue_create(sessions[0], 99, 120000, NULL, choose, NULL);
    CHECK(invalid);
    for (unsigned i = 0; i < 32; i++) CHECK(coop_dialogue_publish(invalid, &v));
    CHECK(!coop_dialogue_publish(invalid, &v));
    uint8_t begin[16] = { 0 };
    coop_object_write32(begin, 42);
    coop_object_write32(begin + 4, 3);
    coop_object_write32(begin + 8, COOP_DIALOGUE_LIMIT + 1);
    CHECK(!coop_dialogue_message(dialogues[1], 0, COOP_DIALOGUE_BEGIN, begin, sizeof(begin)));
    CHECK(!coop_dialogue_message(dialogues[1], 2, COOP_DIALOGUE_BEGIN, begin, sizeof(begin)));
    CHECK(coop_dialogue_poll(invalid, 120000));
    CHECK(!coop_dialogue_poll(invalid, 150001));
    coop_dialogue_destroy(invalid);
    invalid = coop_dialogue_create(sessions[3], 99, now, NULL, NULL, NULL);
    CHECK(invalid);
    v.revision = 1;
    bytes = coop_dialogue_view_encode(&v, &n);
    CHECK(bytes);
    coop_object_write32(begin, 99);
    coop_object_write32(begin + 4, 1);
    coop_object_write32(begin + 8, (uint32_t)n);
    coop_object_write32(begin + 12, coop_snapshot_crc(bytes, n) ^ 1);
    CHECK(coop_dialogue_message(invalid, 0, COOP_DIALOGUE_BEGIN, begin, sizeof(begin)));
    uint8_t chunk[COOP_MESSAGE_CAPACITY];
    coop_object_write32(chunk, 99);
    coop_object_write32(chunk + 4, 1);
    for (size_t at = 0; at < n;) {
        size_t count = n - at;
        if (count > sizeof(chunk) - 12) count = sizeof(chunk) - 12;
        coop_object_write32(chunk + 8, (uint32_t)at);
        memcpy(chunk + 12, bytes + at, count);
        CHECK(coop_dialogue_message(invalid, 0, COOP_DIALOGUE_CHUNK, chunk, count + 12));
        at += count;
    }
    CHECK(!coop_dialogue_poll(invalid, now));
    CHECK(!coop_dialogue_current(invalid)->revision);
    free(bytes);
    coop_dialogue_destroy(invalid);
    /* Forged requests' RESULT frames are intentionally not socket-flushed: the
     * normal clients did not submit those requests. */
    for (unsigned i = 0; i < 4; i++) {
        coop_session_set_handler(sessions[i], NULL, NULL);
        coop_dialogue_destroy(dialogues[i]);
        coop_session_destroy(sessions[i]);
    }
    puts("PASS: canonical dialogue text corpus, four-session chooser/observers, delayed presentation ACK, exactly-once actions and bounded validation");
    return 0;
}
