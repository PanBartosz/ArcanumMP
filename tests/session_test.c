#include "coop/session.h"
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

static uint64_t now = 100;
static void pump(CoopSession** sessions, size_t count)
{
    for (int round = 0; round < 40; round++) {
        for (size_t i = 0; i < count; i++)
            coop_session_poll(sessions[i], now);
        now++;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay = { 0, 1000000 };
        nanosleep(&delay, NULL);
#endif
    }
}

typedef struct CallbackSession {
    CoopSession** session;
    unsigned action, seen;
    uint16_t replacement_port;
} CallbackSession;

static bool lifecycle_message(void* context, uint8_t sender, uint8_t type, const uint8_t* payload, size_t size)
{
    (void)sender;
    CallbackSession* c = context;
    CHECK(type == 95 && size == 40 && (payload[0] == 1 || payload[0] == 2));
    CHECK(!(c->seen & (1u << payload[0])));
    c->seen |= 1u << payload[0];
    uint8_t original[40]; memcpy(original, payload, sizeof(original));
    if (payload[0] == 1) {
        if (c->action == 0) {
            coop_session_poll(*c->session, now + 100);
            CHECK(c->seen == 6);
        } else if (c->action == 1) {
            coop_session_leave(*c->session);
        } else if (c->action == 2) {
            CHECK(coop_session_host(*c->session, 0, now + 100));
            c->replacement_port = coop_session_port(*c->session);
        } else {
            coop_session_destroy(*c->session);
            *c->session = NULL;
        }
    }
    CHECK(!memcmp(original, payload, sizeof(original)));
    /* A retired callback's rejection must not close a replacement connection. */
    return c->action == 0;
}

static void callback_lifecycle(void)
{
    for (unsigned target = 0; target < 2; target++) for (unsigned action = 0; action < 4; action++) {
        CoopConfig config = { .name = "Callback lifecycle", .content_id = { 17 } };
        CoopSession* pair[] = { coop_session_create(&config), coop_session_create(&config) };
        CHECK(pair[0] && pair[1] && coop_session_host(pair[0], 0, now));
        CHECK(coop_session_join(pair[1], "127.0.0.1", coop_session_port(pair[0]), now));
        pump(pair, 2);
        CHECK(coop_session_phase(pair[1]) == COOP_LOBBY);
        CallbackSession c = { .session = &pair[target], .action = action };
        coop_session_set_handler(pair[target], lifecycle_message, &c);
        uint8_t first[40], second[40]; memset(first, 1, sizeof(first)); memset(second, 2, sizeof(second));
        CHECK(coop_session_send(pair[1 - target], (uint8_t)target, 95, first, sizeof(first)));
        CHECK(coop_session_send(pair[1 - target], (uint8_t)target, 95, second, sizeof(second)));
        coop_session_poll(pair[1 - target], now);
        pump(pair, 2);
        CHECK(c.seen == (action == 0 ? 6u : 2u));
        if (action == 0) {
            CHECK(coop_session_phase(pair[target]) == COOP_LOBBY);
            CHECK(coop_session_chat(pair[target], "Continued after nested dispatch."));
        } else if (action == 1) {
            CHECK(coop_session_phase(pair[target]) == COOP_OFFLINE);
        } else if (action == 2) {
            CHECK(coop_session_phase(pair[target]) == COOP_LOBBY && coop_session_is_host(pair[target]));
            CHECK(c.replacement_port && coop_session_port(pair[target]) == c.replacement_port);
        } else CHECK(pair[target] == NULL);
        coop_session_destroy(pair[0]); coop_session_destroy(pair[1]);
    }
}

static unsigned terminal_calls;
static bool abort_from_handler(void* context, uint8_t sender, uint8_t type, const uint8_t* payload, size_t size)
{
    CHECK(sender == 1 && type == 95 && !size);
    CHECK(++terminal_calls == 1);
    (void)payload;
    coop_session_abort(context, "Native snapshot restore failed. Recover the saved party.");
    return false; /* The terminal reason must survive a callback rejection. */
}

static void verify_terminal_failures(CoopSession** sessions)
{
    static const char reason[] = "Native snapshot restore failed. Recover the saved party.";
    for (unsigned scenario = 0; scenario < 6; scenario++) {
        CHECK(coop_session_host(sessions[0], 0, now));
        uint16_t port = coop_session_port(sessions[0]);
        for (unsigned i = 1; i < 4; i++) CHECK(coop_session_join(sessions[i], "127.0.0.1", port, now));
        pump(sessions, 4);
        if (scenario != 3) coop_session_lock(sessions[0], true);
        pump(sessions, 4);
        if (scenario == 2) {
            coop_session_set_handler(sessions[0], abort_from_handler, sessions[0]);
            CHECK(coop_session_send(sessions[1], 0, 95, NULL, 0));
            CHECK(coop_session_send(sessions[1], 0, 95, NULL, 0));
        } else coop_session_abort(sessions[scenario == 0 || scenario >= 4 ? 0 : 1], reason);
        if (scenario >= 4) {
            /* Writing the notice is not enough: hold one receiver's ACK. */
            pump(sessions, 3);
            CHECK(coop_session_abort_pending(sessions[0]));
            CHECK(coop_session_phase(sessions[3]) == COOP_LOBBY);
            coop_session_abort(sessions[0], "A later failure must not replace the first cause.");
            CHECK(!strcmp(coop_session_status(sessions[0]), reason));
            if (scenario == 5) {
                now += 5001;
                coop_session_poll(sessions[0], now);
                CHECK(!coop_session_abort_pending(sessions[0]));
                CHECK(!strcmp(coop_session_status(sessions[0]), reason));
                for (unsigned i = 0; i < 4; i++) coop_session_leave(sessions[i]);
                continue;
            }
        }
        pump(sessions, 4);
        for (unsigned i = 0; i < 4; i++) {
            bool failed = scenario != 3 || i == 1;
            CHECK((coop_session_phase(sessions[i]) == COOP_OFFLINE) == failed);
            if (failed) {
                CHECK(coop_session_failed(sessions[i]) && !coop_session_abort_pending(sessions[i]));
                CHECK(!coop_session_can_retry(sessions[i]));
                CHECK(strstr(coop_session_status(sessions[i]), reason));
            }
        }
        coop_session_set_handler(sessions[0], NULL, NULL);
        for (unsigned i = 0; i < 4; i++) coop_session_leave(sessions[i]);
    }
}

int main(void)
{
    CoopConfig config = { .name = "Host", .content_id = { 42 } };
    CoopSession* sessions[6];
    for (int i = 0; i < 6; i++) {
        snprintf(config.name, sizeof(config.name), "Player %d", i);
        sessions[i] = coop_session_create(&config);
        CHECK(sessions[i] != NULL);
    }
    CHECK(coop_session_host(sessions[0], 0, now));
    uint16_t port = coop_session_port(sessions[0]);
    CHECK(port != 0);
    CHECK(!coop_session_ready(sessions[0], true));
    CHECK(!coop_session_all_ready(sessions[0]));
    for (int i = 1; i < 4; i++)
        CHECK(coop_session_join(sessions[i], "127.0.0.1", port, now));
    pump(sessions, 6);
    for (int i = 0; i < 4; i++) {
        CHECK(coop_session_phase(sessions[i]) == COOP_LOBBY);
        CHECK(coop_session_local_slot(sessions[i]) == i);
        for (int j = 0; j < 4; j++)
            CHECK(coop_session_players(sessions[i])[j].connected);
        CHECK(coop_session_character(sessions[i], (uint32_t)(16067 + i), "Campaign traveller"));
    }
    pump(sessions, 6);
    for (int i = 0; i < 4; i++)
        CHECK(coop_session_ready(sessions[i], true));
    pump(sessions, 6);
    for (int i = 0; i < 4; i++)
        CHECK(coop_session_all_ready(sessions[i]));
    CHECK(coop_session_join(sessions[4], "127.0.0.1", port, now));
    pump(sessions, 6);
    CHECK(coop_session_phase(sessions[4]) == COOP_OFFLINE);
    CHECK(strstr(coop_session_status(sessions[4]), "full") != NULL);
    for (int i = 0; i < 4; i++)
        CHECK(coop_session_chat(sessions[i], "The zeppelin awaits."));
    CHECK(!coop_session_chat(sessions[0], "bad\nmessage"));
    CHECK(!coop_session_chat(sessions[0], "   "));
    CHECK(!coop_session_character(sessions[0], 999, "Invalid"));
    pump(sessions, 6);
    for (int i = 0; i < 4; i++) {
        CHECK(coop_session_chat_count(sessions[i]) == 4);
        const CoopChat* chat = coop_session_chat_at(sessions[i], 0);
        CHECK(chat != NULL && strcmp(chat->text, "The zeppelin awaits.") == 0);
    }
    CoopCharacter custom = { .race = 0, .gender = 1, .portrait = 1, .unspent = 5, .name = "Custom traveller" };
    for (unsigned i = 0; i < 8; i++) custom.stats[i] = 8;
    CHECK(coop_session_custom_character(sessions[2], &custom));
    pump(sessions, 6);
    for (unsigned i = 0; i < 4; i++) {
        const CoopPlayer* pc = &coop_session_players(sessions[i])[2];
        CHECK(pc->custom && pc->prototype == 16066 && !strcmp(pc->character, custom.name));
        CHECK(pc->build.stats[7] == 8 && pc->build.unspent == 5 && !pc->ready);
    }
    CHECK(coop_session_custom_character(sessions[0], &custom));
    pump(sessions, 6);
    CHECK(coop_session_players(sessions[3])[0].custom);
    CHECK(coop_session_character(sessions[0], 16067, "Host preset"));
    pump(sessions, 6);
    CHECK(!coop_session_players(sessions[3])[0].custom);
    CHECK(coop_session_character(sessions[2], 16072, "Another traveller"));
    pump(sessions, 6);
    for (int i = 0; i < 4; i++) {
        CHECK(!coop_session_all_ready(sessions[i]));
        for (int j = 0; j < 4; j++)
            CHECK(!coop_session_players(sessions[i])[j].ready);
    }
    coop_session_leave(sessions[2]);
    pump(sessions, 6);
    CHECK(!coop_session_players(sessions[0])[2].connected);
    uint32_t before_lock = coop_session_revision(sessions[0]);
    coop_session_lock(sessions[0], true);
    CHECK(!coop_session_character(sessions[0], 16067, "Locked"));
    CHECK(!coop_session_custom_character(sessions[0], &custom));
    CHECK(!coop_session_ready(sessions[0], true));
    CHECK(coop_session_join(sessions[4], "127.0.0.1", port, now));
    pump(sessions, 6);
    CHECK(coop_session_phase(sessions[4]) == COOP_OFFLINE);
    CHECK(strstr(coop_session_status(sessions[4]), "campaign has started") != NULL);
    CHECK(coop_session_revision(sessions[0]) == before_lock + 1);
    CHECK(coop_session_chat(sessions[0], "Campaign chat remains available."));
    pump(sessions, 6);
    CHECK(coop_session_chat_count(sessions[1]) == 5);
    coop_session_lock(sessions[0], false);
    CHECK(coop_session_join(sessions[4], "127.0.0.1", port, now));
    pump(sessions, 6);
    CHECK(coop_session_local_slot(sessions[4]) == 2);
    CHECK(coop_session_players(sessions[1])[2].prototype == 0);
    CHECK(coop_session_chat_count(sessions[4]) == 5);
    CoopConfig duplicate = { .name = "Duplicate profile" };
    memcpy(duplicate.content_id, config.content_id, sizeof(duplicate.content_id));
    memcpy(duplicate.identity, coop_session_players(sessions[0])[1].identity, COOP_IDENTITY_SIZE);
    coop_session_destroy(sessions[5]);
    sessions[5] = coop_session_create(&duplicate);
    CHECK(coop_session_join(sessions[5], "127.0.0.1", port, now));
    pump(sessions, 6);
    CHECK(coop_session_phase(sessions[5]) == COOP_OFFLINE);
    CHECK(strstr(coop_session_status(sessions[5]), "already connected") != NULL);
    CoopConfig mismatch = { .name = "Mismatch", .content_id = { 43 } };
    coop_session_destroy(sessions[5]);
    sessions[5] = coop_session_create(&mismatch);
    CHECK(coop_session_join(sessions[5], "127.0.0.1", port, now));
    pump(sessions, 6);
    CHECK(coop_session_phase(sessions[5]) == COOP_OFFLINE);
    CHECK(strstr(coop_session_status(sessions[5]), "content differs") != NULL);
    CHECK(!coop_session_join(sessions[5], "not an address", port, now));
    CHECK(!coop_session_join(sessions[5], "127.0.0.1", 0, now));
    uint8_t removed_owner[COOP_IDENTITY_SIZE], wrong_owner[COOP_IDENTITY_SIZE] = {1};
    memcpy(removed_owner, coop_session_players(sessions[0])[3].identity, sizeof(removed_owner));
    CHECK(!coop_session_remove(sessions[1], 3, removed_owner));
    CHECK(!coop_session_remove(sessions[0], 0, removed_owner));
    CHECK(!coop_session_remove(sessions[0], 3, wrong_owner));
    coop_session_lock(sessions[0], true);
    CHECK(!coop_session_remove(sessions[0], 3, removed_owner));
    coop_session_lock(sessions[0], false);
    CHECK(coop_session_remove(sessions[0], 3, removed_owner));
    CHECK(!coop_session_all_ready(sessions[0]));
    pump(sessions, 6);
    CHECK(coop_session_phase(sessions[3]) == COOP_OFFLINE && !coop_session_can_retry(sessions[3]));
    CHECK(strstr(coop_session_status(sessions[3]), "removed") && !coop_session_players(sessions[0])[3].connected);
    CHECK(!coop_session_remove(sessions[0], 3, removed_owner));
    CHECK(coop_session_join(sessions[3], "127.0.0.1", port, now));
    pump(sessions, 6);
    CHECK(coop_session_phase(sessions[3]) == COOP_LOBBY && coop_session_local_slot(sessions[3]) == 3);
    coop_session_leave(sessions[0]);
    pump(sessions, 6);
    for (int i = 1; i < 5; i++)
        CHECK(coop_session_phase(sessions[i]) == COOP_OFFLINE);
    verify_terminal_failures(sessions);
    for (int i = 0; i < 6; i++)
        coop_session_destroy(sessions[i]);
    memset(config.name, 'a', sizeof(config.name));
    CHECK(coop_session_create(&config) == NULL);
    callback_lifecycle();
    puts("PASS: four players, readiness invalidation, chat, admission, host loss, nested dispatch and callback leave/replace/destroy");
    return 0;
}
