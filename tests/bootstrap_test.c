#include "coop/bootstrap.h"
#include "coop/environment_state.h"
#include "coop/dialogue_recovery.h"
#include "coop/magic_state.h"
#include "coop/map_notes.h"
#include "coop/journal.h"
#include "coop/object_state.h"
#include "coop/snapshot.h"
#include "coop/play_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                \
        }                                                           \
    } while (0)
static CoopSession* sessions[4];
static CoopBootstrap* boots[4];
static uint64_t now;
static void pump(unsigned rounds)
{
    while (rounds--) {
        for (unsigned i = 0; i < 4; i++) {
            coop_session_poll(sessions[i], now);
            if (boots[i]) coop_bootstrap_poll(boots[i], now);
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
static void setup(void)
{
    /* Fresh receivers must also work after a long-running lobby/rejoin wait. */
    now = 60000;
    for (unsigned i = 0; i < 4; i++) {
        CoopConfig config = { 0 };
        snprintf(config.name, sizeof(config.name), "Snapshot %u", i);
        sessions[i] = coop_session_create(&config);
        CHECK(sessions[i]);
    }
    CHECK(coop_session_host(sessions[0], 0, now));
    for (unsigned i = 1; i < 4; i++)
        CHECK(coop_session_join(sessions[i], "127.0.0.1", coop_session_port(sessions[0]), now));
    pump(30);
    for (unsigned i = 0; i < 4; i++)
        CHECK(coop_session_character(sessions[i], 16067 + i, "Hero"));
    pump(20);
    for (unsigned i = 0; i < 4; i++)
        CHECK(coop_session_ready(sessions[i], true));
    pump(20);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_session_all_ready(sessions[i]));
        boots[i] = coop_bootstrap_create(sessions[i]);
        CHECK(boots[i]);
    }
}
static void cleanup(void)
{
    for (unsigned i = 0; i < 4; i++) {
        coop_bootstrap_destroy(boots[i]);
        boots[i] = NULL;
        coop_session_destroy(sessions[i]);
    }
}
static void put_le32(uint8_t* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(value >> (8 * i));
}
static void verify_journal_bundle(void)
{
    CoopJournal* journal = coop_journal_create();
    size_t length;
    uint8_t* bytes = coop_journal_encode(journal, &length);
    CHECK(bytes && length == 16);
    uint8_t index[256] = { 0 }, archive[19] = { 'a', 'b', 'c' };
    memcpy(archive + 3, bytes, length);
    const char* names[] = { "data.sav", "data2.sav", "arcmp-party.bin", "arcmp-conversations.bin" };
    size_t at = 0;
    for (unsigned i = 0; i < 4; i++) {
        put_le32(index + at, 0);
        put_le32(index + at + 4, (uint32_t)strlen(names[i]));
        memcpy(index + at + 8, names[i], strlen(names[i]));
        at += 8 + strlen(names[i]);
        put_le32(index + at, i == 3 ? (uint32_t)length : 1);
        at += 4;
    }
    put_le32(index + at, 3);
    at += 4;
    CoopSnapshotFiles files = { index, at, archive, sizeof(archive) }, parsed;
    size_t size;
    uint8_t* bundle = coop_snapshot_pack(&files, &size);
    CHECK(bundle && coop_snapshot_parse(bundle, size, &parsed));
    free(bundle);
    archive[18] ^= 1;
    CHECK(!coop_snapshot_pack(&files, &size));
    archive[18] ^= 1;
    coop_object_write32(archive + 7, COOP_JOURNAL_PAGES + 1);
    coop_object_write32(archive + 15, coop_snapshot_crc(archive + 3, 12));
    CHECK(!coop_snapshot_pack(&files, &size));
    free(bytes);
    coop_journal_destroy(journal);
}
static void verify_bundle(void)
{
    uint8_t index[256] = { 0 };
    const char* names[] = { "data.sav", "data2.sav", "arcmp-party.bin", "Shrouded Hills.tmf", "crash-site.tmn" };
    size_t at = 0;
    size_t fog_name = 0;
    for (unsigned i = 0; i < 5; i++) {
        put_le32(index + at, 0);
        put_le32(index + at + 4, (uint32_t)strlen(names[i]));
        memcpy(index + at + 8, names[i], strlen(names[i]));
        if (i == 3) fog_name = at + 8;
        at += 8 + strlen(names[i]);
        put_le32(index + at, 1);
        at += 4;
    }
    put_le32(index + at, 3);
    at += 4;
    CoopSnapshotFiles files = { index, at, (const uint8_t*)"abcde", 5 }, parsed;
    size_t size;
    uint8_t* package = coop_snapshot_pack(&files, &size);
    CHECK(package && coop_snapshot_parse(package, size, &parsed));
    CHECK(parsed.index_size == at && parsed.archive_size == 5 && !memcmp(parsed.archive, "abcde", 5));
    CHECK(!coop_snapshot_parse(package, size - 1, &parsed));
    package[16 + fog_name] = '/';
    CHECK(!coop_snapshot_parse(package, size, &parsed));
    package[16 + fog_name] = 'S';
    package[16 + fog_name + strlen(names[3]) - 1] = 'x';
    CHECK(!coop_snapshot_parse(package, size, &parsed));
    package[16 + fog_name + strlen(names[3]) - 1] = 'f';
    package[16 + 8] = '/';
    CHECK(!coop_snapshot_parse(package, size, &parsed));
    package[16 + 8] = 'd';
    package[16] = 2; // leave root directory
    CHECK(!coop_snapshot_parse(package, size, &parsed));
    package[16] = 0;
    package[16 + 8 + 8] = 99; // total data length exceeds archive
    CHECK(!coop_snapshot_parse(package, size, &parsed));
    package[16 + 8 + 8] = 1;
    /* Exercise index bounds/length/nesting under sanitizer instrumentation. */
    uint32_t random = 42;
    for (unsigned test = 0; test < 10000; test++) {
        size_t offset = 16 + (test % at);
        uint8_t saved = package[offset];
        random = random * 1664525u + 1013904223u;
        package[offset] = (uint8_t)(random >> 24);
        bool accepted = coop_snapshot_parse(package, size, &parsed);
        if (accepted) CHECK(parsed.archive + parsed.archive_size == package + size);
        package[offset] = saved;
    }
    free(package);
    /* The optional environment sidecar remains compatible with older bundles,
     * and is validated before any native archive is written or loaded. */
    at -= 4;
    size_t entry_start = at;
    const char* environment_name = "arcmp-environment.bin";
    put_le32(index + at, 0); put_le32(index + at + 4, 21); memcpy(index + at + 8, environment_name, 21); at += 29;
    put_le32(index + at, COOP_ENVIRONMENT_BYTES + 4); at += 4;
    put_le32(index + at, 3); at += 4;
    uint8_t archive[5 + COOP_ENVIRONMENT_BYTES + 4] = {'a','b','c','d','e'};
    CoopEnvironmentState environment = {.hour = 23, .outdoor = 0x123456};
    CHECK(coop_environment_encode(&environment, archive + 5));
    coop_object_write32(archive + 5 + COOP_ENVIRONMENT_BYTES, coop_snapshot_crc(archive + 5, COOP_ENVIRONMENT_BYTES));
    files = (CoopSnapshotFiles){index,at,archive,sizeof(archive)};
    package = coop_snapshot_pack(&files,&size); CHECK(package && coop_snapshot_parse(package,size,&parsed)); free(package);
    archive[sizeof(archive)-1] ^= 1; CHECK(!coop_snapshot_pack(&files,&size)); archive[sizeof(archive)-1] ^= 1;
    memcpy(index + at - 4,index + entry_start,33); put_le32(index + at - 4 + 29,0); put_le32(index + at - 4 + 33,3);
    files.index_size = at + 33; CHECK(!coop_snapshot_pack(&files,&size));
}
static void verify_dialogue_continuation(void)
{
    CoopDialogueRecovery* state = calloc(1, sizeof(*state));
    CoopDialogueRecovery* restored = calloc(1, sizeof(*restored));
    CHECK(state && restored);
    state->active = true;
    state->current = (CoopDialogueStart){ .pc = { .bytes = { 2, 1 } }, .npc = { .bytes = { 2, 2 } }, .map = 1, .script = 1324, .page = 3 };
    state->starts[0] = state->current; state->queued = 1;
    state->state_script = 1324; state->options = 1; state->journal_page = COOP_JOURNAL_PAGES;
    strcpy(state->reply, "A retained reply"); strcpy(state->selected, "What happened?");
    strcpy(state->text[0], "Continue"); strcpy(state->actions[0], "gf1 1"); state->target[0] = 10;
    CHECK(coop_dialogue_recovery_admit(state, &state->current));
    CoopDialogueStart opening = state->current;
    opening.pc.bytes[1] = 9;
    CHECK(!coop_dialogue_recovery_admit(state, &opening));
    opening.line = 8; opening.page = 100;
    CHECK(coop_dialogue_recovery_admit(state, &opening) && state->opened == 1);
    opening = state->current; opening.npc.bytes[1] = 3;
    CHECK(coop_dialogue_recovery_admit(state, &opening) && state->opened == 2);
    size_t size;
    uint8_t* bytes = coop_dialogue_recovery_encode(state, &size);
    CHECK(bytes && !memcmp(bytes, "ACDLGR05", 8) && coop_dialogue_recovery_decode(restored, bytes, size));
    CHECK(restored->journal_page == COOP_JOURNAL_PAGES && !strcmp(restored->selected, state->selected)
        && restored->queued == 1 && !strcmp(restored->actions[0], state->actions[0]));
    CHECK(restored->opened == 2 && !coop_dialogue_recovery_admit(restored, &state->current));
    /* Older readers' layouts end before the saved opening admissions. */
    size_t admission_size = 4 + state->opened * (2 * COOP_OBJECT_ID_BYTES + 16);
    size_t v4_size = size - 4;
    uint8_t* v4 = malloc(v4_size); CHECK(v4);
    memcpy(v4, bytes, v4_size - 4); memcpy(v4, "ACDLGR04", 8);
    coop_object_write32(v4 + v4_size - 4, coop_snapshot_crc(v4, v4_size - 4));
    CHECK(coop_dialogue_recovery_decode(restored, v4, v4_size) && !restored->notice_count && restored->opened == 2);
    free(v4);
    size_t v3_size = size - admission_size - 4;
    uint8_t* v3 = malloc(v3_size); CHECK(v3);
    memcpy(v3, bytes, v3_size - 4); memcpy(v3, "ACDLGR03", 8);
    coop_object_write32(v3 + v3_size - 4, coop_snapshot_crc(v3, v3_size - 4));
    CHECK(coop_dialogue_recovery_decode(restored, v3, v3_size) && !restored->opened && restored->mode == state->mode);
    /* Version1 put the queued callbacks directly after the active options. */
    size_t extension = 12 + strlen(state->selected), queue_size = 2 * COOP_OBJECT_ID_BYTES + 16;
    size_t prefix = v3_size - 4 - queue_size - extension, old_size = v3_size - extension;
    uint8_t* old = malloc(old_size); CHECK(old);
    memcpy(old, v3, prefix); memcpy(old, "ACDLGR01", 8);
    memcpy(old + prefix, v3 + prefix + extension, queue_size);
    coop_object_write32(old + old_size - 4, coop_snapshot_crc(old, old_size - 4));
    CHECK(coop_dialogue_recovery_decode(restored, old, old_size)
        && !restored->journal_page && !restored->selected[0] && restored->queued == 1 && restored->target[0] == 10);
    size_t v2_size = v3_size - 4, mode_at = v3_size - 4 - queue_size - 4;
    uint8_t* v2 = malloc(v2_size); CHECK(v2);
    memcpy(v2, v3, mode_at); memcpy(v2, "ACDLGR02", 8);
    memcpy(v2 + mode_at, v3 + mode_at + 4, queue_size);
    coop_object_write32(v2 + v2_size - 4, coop_snapshot_crc(v2, v2_size - 4));
    CHECK(coop_dialogue_recovery_decode(restored, v2, v2_size) && !restored->mode
        && restored->journal_page == COOP_JOURNAL_PAGES && !strcmp(restored->selected, state->selected));
    bytes[size - 1] ^= 1;
    CHECK(!coop_dialogue_recovery_decode(restored, bytes, size) && restored->journal_page == COOP_JOURNAL_PAGES && restored->target[0] == 10);
    state->notice_count = 2;
    state->notices[0] = (CoopDialogueNotice) { .pc_guid = { 1 }, .npc = { .bytes = { 2, 2 } }, .speech = -1,
        .journal_page = 7, .speaker = "Original speaker", .text = "A timed quest hint." };
    state->notices[1] = state->notices[0]; state->notices[1].pc_guid[0] = 2;
    size_t notices_size;
    uint8_t* notices = coop_dialogue_recovery_encode(state, &notices_size);
    CHECK(notices && coop_dialogue_recovery_decode(restored, notices, notices_size) && restored->notice_count == 2
        && restored->notices[1].pc_guid[0] == 2 && restored->notices[0].journal_page == 7
        && !strcmp(restored->notices[1].text, state->notices[1].text));
    free(notices);
    state->notices[0].speech = -2;
    CHECK(!coop_dialogue_recovery_encode(state, &notices_size));
    state->notice_count = COOP_DIALOGUE_STARTS + 1;
    CHECK(!coop_dialogue_recovery_encode(state, &notices_size));
    state->notice_count = 0;
    state->journal_page++;
    CHECK(!coop_dialogue_recovery_encode(state, &size));
    state->journal_page = 0;
    for (unsigned mode = 0; mode <= 10; mode++) {
        state->mode = mode;
        uint8_t* panel = coop_dialogue_recovery_encode(state, &size);
        bool valid = !mode || mode == 3 || (mode >= 5 && mode <= 9);
        CHECK((panel != NULL) == valid);
        if (valid) CHECK(coop_dialogue_recovery_decode(restored, panel, size) && restored->mode == mode);
        free(panel);
    }
    state->mode = 3; state->state_script = state->current.script = 0;
    uint8_t* companion = coop_dialogue_recovery_encode(state, &size);
    CHECK(companion && coop_dialogue_recovery_decode(restored, companion, size) && restored->mode == 3 && !restored->current.script);
    state->mode = 0; CHECK(!coop_dialogue_recovery_encode(state, &size));
    for (unsigned i = 0; i < COOP_DIALOGUE_STARTS + 5; i++) {
        opening.page = (int)i + 1000;
        CHECK(coop_dialogue_recovery_admit(state, &opening));
    }
    CHECK(state->opened == COOP_DIALOGUE_STARTS && !coop_dialogue_recovery_admit(state, &opening));
    opening = state->current;
    CHECK(coop_dialogue_recovery_admit(state, &opening));
    free(v3); free(companion); free(v2); free(old); free(bytes); free(restored); free(state);
}
static void verify_current_sidecars(void)
{
    CoopDialogueRecovery* dialogue = calloc(1, sizeof(*dialogue));
    CoopMapNotes* notes = calloc(1, sizeof(*notes));
    CoopMagicView* magic = calloc(1, sizeof(*magic));
    CHECK(dialogue && notes && magic);
    dialogue->queued = 1;
    dialogue->starts[0] = (CoopDialogueStart){ .pc = { .bytes = { 2, 1 } }, .npc = { .bytes = { 2, 2 } }, .map = 1, .script = 1000, .line = 1 };
    size_t dialogue_size, notes_size;
    uint8_t* dialogue_bytes = coop_dialogue_recovery_encode(dialogue, &dialogue_size); CHECK(dialogue_bytes);
    notes->serial = notes->revision = notes->count = 1;
    notes->records[0] = (CoopMapNote){ .id = 1, .revision = 1, .x = 100, .y = 200, .text = "Meet here" };
    uint8_t* notes_bytes = coop_map_notes_encode(notes, &notes_size); CHECK(notes_bytes);
    magic->choices[0] = (CoopMagicChoice){ .actor = { 1 }, .token = 1, .map = 1, .spell = 19, .charge = 1 };
    uint8_t magic_bytes[COOP_MAGIC_VIEW_HEADER + 4];
    CHECK(coop_magic_view_encode(magic, magic_bytes, COOP_MAGIC_VIEW_HEADER));
    coop_object_write32(magic_bytes + COOP_MAGIC_VIEW_HEADER, coop_snapshot_crc(magic_bytes, COOP_MAGIC_VIEW_HEADER));
    uint8_t transition[112] = {0}, traps[40] = {0};
    memcpy(transition, "ACTRANS1", 8); coop_object_write32(transition + 8, 1);
    transition[12] = 2; transition[13] = 1; coop_object_write32(transition + 32, 1); coop_object_write32(transition + 36, 6); coop_object_write32(transition + 48, 2);
    coop_object_write32(transition + 108, coop_snapshot_crc(transition, 108));
    memcpy(traps, "ACTRAPS1", 8); coop_object_write32(traps + 8, 1); traps[12] = 2; traps[13] = 3; coop_object_write32(traps + 29, 1); traps[33] = 2;
    coop_object_write32(traps + 36, coop_snapshot_crc(traps, 36));
    uint8_t environment[COOP_ENVIRONMENT_V2_BYTES + 4] = {0};
    /* Retained protocol36 checkpoints carry ACE2, which remains loadable. */
    memcpy(environment, "ACE2", 4); coop_object_write32(environment + COOP_ENVIRONMENT_V2_BYTES, coop_snapshot_crc(environment, COOP_ENVIRONMENT_V2_BYTES));
    const char* names[] = { "data.sav", "data2.sav", "arcmp-party.bin", "arcmp-dialogue.bin", "arcmp-transition.bin", "arcmp-spell-destinations.bin", "arcmp-map-notes.bin", "arcmp-traps.bin", "arcmp-environment.bin" };
    const uint8_t* contents[] = { (const uint8_t*)"a", (const uint8_t*)"b", (const uint8_t*)"c", dialogue_bytes, transition, magic_bytes, notes_bytes, traps, environment };
    size_t lengths[] = {1,1,1,dialogue_size,sizeof(transition),sizeof(magic_bytes),notes_size,sizeof(traps),sizeof(environment)};
    uint8_t index[1024] = {0}, archive[2048] = {0}; size_t at = 0, total = 0, offsets[9];
    for (unsigned i = 0; i < 9; i++) {
        put_le32(index + at, 0); put_le32(index + at + 4, (uint32_t)strlen(names[i])); memcpy(index + at + 8, names[i], strlen(names[i])); at += 8 + strlen(names[i]);
        put_le32(index + at, (uint32_t)lengths[i]); at += 4;
        offsets[i] = total; CHECK(total + lengths[i] <= sizeof(archive)); memcpy(archive + total, contents[i], lengths[i]); total += lengths[i];
    }
    put_le32(index + at, 3); at += 4;
    CoopSnapshotFiles files = {index,at,archive,total}, parsed; size_t size;
    uint8_t* package = coop_snapshot_pack(&files, &size);
    CHECK(package && coop_snapshot_parse(package, size, &parsed)); free(package);
    for (unsigned i = 3; i < 9; i++) {
        size_t end = offsets[i] + lengths[i] - 1; archive[end] ^= 1;
        CHECK(!coop_snapshot_pack(&files, &size)); archive[end] ^= 1;
    }
    /* A malformed transition still refuses after its CRC is recomputed. */
    size_t begin = offsets[4]; archive[begin + 84] = 1;
    coop_object_write32(archive + begin + 108, coop_snapshot_crc(archive + begin, 108));
    CHECK(!coop_snapshot_pack(&files, &size));
    free(dialogue_bytes); free(notes_bytes); free(dialogue); free(notes); free(magic);
}

static void verify_event_bundle(void)
{
    uint8_t index[256]={0},archive[3+16+COOP_PLAY_EVENT_BYTES]={'a','b','c'};
    uint8_t* events=archive+3; memcpy(events,"MPEV0001",8);
    coop_object_write32(events+8,1); coop_object_write32(events+12,2);
    coop_object_write32(events+16+4,1); events[16+24]=PLAY_COMBAT;
    memcpy(events+16+28,"A native combat event",22);
    const char* names[]={"data.sav","data2.sav","arcmp-party.bin","arcmp-events.bin"};
    size_t at=0;
    for (unsigned i=0;i<4;i++) {
        put_le32(index+at,0); put_le32(index+at+4,(uint32_t)strlen(names[i]));
        memcpy(index+at+8,names[i],strlen(names[i])); at+=8+strlen(names[i]);
        put_le32(index+at,i==3 ? (uint32_t)(sizeof(archive)-3) : 1); at+=4;
    }
    put_le32(index+at,3); at+=4;
    CoopSnapshotFiles files={index,at,archive,sizeof(archive)},parsed; size_t size;
    uint8_t* bundle=coop_snapshot_pack(&files,&size); CHECK(bundle && coop_snapshot_parse(bundle,size,&parsed)); free(bundle);
    events[16+24]=PLAY_EVENT_KINDS; CHECK(!coop_snapshot_pack(&files,&size)); events[16+24]=PLAY_COMBAT;
    coop_object_write32(events+12,3); CHECK(!coop_snapshot_pack(&files,&size)); coop_object_write32(events+12,2);
    coop_object_write32(events+16,1); CHECK(!coop_snapshot_pack(&files,&size)); coop_object_write32(events+16,0);
    memset(events+16+28,'x',192); CHECK(!coop_snapshot_pack(&files,&size));
}
int main(void)
{
    verify_event_bundle();
    verify_bundle();
    verify_journal_bundle();
    verify_dialogue_continuation();
    verify_current_sidecars();
    CHECK(coop_snapshot_crc("123456789", 9) == UINT32_C(0xCBF43926));
    size_t length = 600123;
    uint8_t* data = malloc(length);
    CHECK(data);
    for (size_t i = 0; i < length; i++)
        data[i] = (uint8_t)(i * 37 + (i >> 8));
    setup();
    CHECK(!coop_bootstrap_begin(boots[1], 1, data, length, now));
    CHECK(!coop_bootstrap_begin(boots[0], 1, data, COOP_SNAPSHOT_LIMIT + 1u, now));
    CHECK(coop_bootstrap_begin(boots[0], 42, data, length, now));
    /* Fill the bounded application queue without polling its receiver. A full
     * queue must yield/retry, retain sequence order and leave the peer alive. */
    for (unsigned i = 0; i < 200; i++)
        coop_bootstrap_poll(boots[0], now);
    CHECK(coop_session_phase(sessions[0]) == COOP_LOBBY);
    for (unsigned rounds = 0; rounds < 1000 && coop_bootstrap_phase(boots[3]) != COOP_BOOT_RECEIVED; rounds++)
        pump(1);
    for (unsigned i = 1; i < 4; i++) {
        size_t received;
        const uint8_t* copy = coop_bootstrap_data(boots[i], &received);
        CHECK(copy && received == length && memcmp(copy, data, length) == 0);
        CHECK(coop_bootstrap_phase(boots[i]) == COOP_BOOT_RECEIVED);
    }
    CHECK(coop_bootstrap_loaded(boots[1]));
    CHECK(coop_bootstrap_loaded(boots[2]));
    pump(30);
    CHECK(coop_bootstrap_phase(boots[0]) == COOP_BOOT_WAITING);
    CHECK(coop_bootstrap_phase(boots[1]) == COOP_BOOT_WAITING);
    CHECK(coop_bootstrap_phase(boots[3]) == COOP_BOOT_RECEIVED);
    CHECK(coop_bootstrap_loaded(boots[3]));
    pump(30);
    for (unsigned i = 0; i < 4; i++)
        CHECK(coop_bootstrap_phase(boots[i]) == COOP_BOOT_COMMITTED);
    cleanup();

    setup();
    CHECK(coop_bootstrap_begin(boots[0], 43, data, length, now));
    pump(3);
    coop_session_leave(sessions[2]);
    pump(30);
    CHECK(coop_bootstrap_phase(boots[0]) == COOP_BOOT_FAILED);
    CHECK(coop_bootstrap_phase(boots[1]) == COOP_BOOT_FAILED);
    CHECK(coop_bootstrap_phase(boots[3]) == COOP_BOOT_FAILED);
    cleanup();

    setup();
    CHECK(coop_bootstrap_begin(boots[0], 44, data, length, now));
    pump(100);
    CHECK(coop_bootstrap_phase(boots[1]) == COOP_BOOT_RECEIVED);
    coop_bootstrap_poll(boots[0], now + 30000);
    CHECK(coop_bootstrap_phase(boots[0]) == COOP_BOOT_FAILED);
    cleanup();

    setup();
    /* A client may not publish a host's snapshot or authorize another slot. */
    uint8_t forged[13] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 15 };
    CHECK(coop_session_send(sessions[1], 0, COOP_SNAPSHOT_OFFER, forged, sizeof(forged)));
    pump(30);
    CHECK(coop_session_phase(sessions[1]) == COOP_OFFLINE);
    CHECK(coop_bootstrap_phase(boots[0]) == COOP_BOOT_IDLE);
    CHECK(!coop_session_send(sessions[2], 3, COOP_SNAPSHOT_CHUNK, forged, sizeof(forged)));
    cleanup();
    setup();
    uint8_t offer[13] = { 0, 0, 0, 45, 0, 0, 0, 1, 0, 0, 0, 0, 15 };
    CHECK(!coop_bootstrap_message(boots[1], 2, COOP_SNAPSHOT_OFFER, offer, sizeof(offer)));
    offer[4] = 127; // oversized declaration rejected before allocation
    CHECK(!coop_bootstrap_message(boots[1], 0, COOP_SNAPSHOT_OFFER, offer, sizeof(offer)));
    offer[4] = 0;
    CHECK(coop_bootstrap_message(boots[1], 0, COOP_SNAPSHOT_OFFER, offer, sizeof(offer)));
    CHECK(!coop_bootstrap_message(boots[1], 0, COOP_SNAPSHOT_OFFER, offer, sizeof(offer)));
    uint8_t chunk[9] = { 0, 0, 0, 45, 0, 0, 0, 1, 42 };
    CHECK(!coop_bootstrap_message(boots[1], 0, COOP_SNAPSHOT_CHUNK, chunk, sizeof(chunk)));
    chunk[7] = 0;
    CHECK(!coop_bootstrap_message(boots[1], 0, COOP_SNAPSHOT_CHUNK, chunk, sizeof(chunk))); // bad CRC
    CHECK(coop_bootstrap_phase(boots[1]) == COOP_BOOT_FAILED);
    cleanup();
    free(data);
    puts("PASS: bounded snapshot transfer, delayed load barrier, departure/timeout cancellation and host authority");
    return 0;
}
