#include "coop/session.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static uint64_t clock_ms(void) { return GetTickCount64(); }
static void delay(void) { Sleep(5); }
#else
#include <time.h>
static uint64_t clock_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void delay(void)
{
    struct timespec t = { 0, 5000000 };
    nanosleep(&t, NULL);
}
#endif

static void json_string(const char* text)
{
    putchar('"');
    for (; *text; text++) {
        if (*text == '"' || *text == '\\') putchar('\\');
        putchar(*text);
    }
    putchar('"');
}

int main(int argc, char** argv)
{
    bool host = argc >= 2 && strcmp(argv[1], "host") == 0;
    bool join = argc >= 2 && strcmp(argv[1], "join") == 0;
    int shift = host ? 0 : 1;
    if ((!host && !join) || argc < 5 + shift) {
        fputs("Usage: arcmp-net host PORT NAME CONTENT_SHA256 [SECONDS] [exercise]\n"
              "       arcmp-net join IPV4 PORT NAME CONTENT_SHA256 [SECONDS] [exercise]\n",
            stderr);
        return 2;
    }
    char* end;
    unsigned long port = strtoul(argv[2 + shift], &end, 10);
    if (*end || port > 65535) return 2;
    CoopConfig config = { 0 };
    if (strlen(argv[3 + shift]) >= sizeof(config.name)) return 2;
    strcpy(config.name, argv[3 + shift]);
    const char* hex = argv[4 + shift];
    if (strlen(hex) != 64) return 2;
    for (size_t i = 0; i < 32; i++) {
        char pair[3] = { hex[2 * i], hex[2 * i + 1], 0 };
        unsigned long byte = strtoul(pair, &end, 16);
        if (*end) return 2;
        config.content_id[i] = (uint8_t)byte;
    }
    unsigned duration = argc > 5 + shift ? (unsigned)atoi(argv[5 + shift]) : 30;
    bool exercise = argc > 6 + shift && strcmp(argv[6 + shift], "exercise") == 0;
    CoopSession* s = coop_session_create(&config);
    if (!s) return 2;
    uint64_t start = clock_ms();
    bool success = host ? coop_session_host(s, (uint16_t)port, start)
                        : coop_session_join(s, argv[2], (uint16_t)port, start);
    uint32_t revision = UINT32_MAX;
    CoopPhase phase = COOP_OFFLINE;
    uint64_t chats = 0;
    bool selected = false, readied = false, greeted = false;
    do {
        coop_session_poll(s, clock_ms());
        CoopPhase next_phase = coop_session_phase(s);
        if (revision != coop_session_revision(s) || next_phase != phase) {
            revision = coop_session_revision(s);
            phase = next_phase;
            printf("{\"event\":\"state\",\"phase\":%d,\"port\":%u,\"slot\":%u,\"revision\":%u,\"all_ready\":%s,\"status\":",
                phase, coop_session_port(s), coop_session_local_slot(s), revision,
                coop_session_all_ready(s) ? "true" : "false");
            json_string(coop_session_status(s));
            fputs(",\"players\":[", stdout);
            const CoopPlayer* players = coop_session_players(s);
            for (int i = 0; i < COOP_MAX_PLAYERS; i++) {
                if (i) putchar(',');
                printf("{\"online\":%s,\"ready\":%s,\"prototype\":%u,\"name\":",
                    players[i].connected ? "true" : "false", players[i].ready ? "true" : "false", players[i].prototype);
                json_string(players[i].name);
                putchar('}');
            }
            puts("]}");
            fflush(stdout);
        }
        while (chats < coop_session_chat_count(s)) {
            const CoopChat* chat = coop_session_chat_at(s, chats++);
            if (!chat) continue;
            printf("{\"event\":\"chat\",\"slot\":%u,\"name\":", chat->slot);
            json_string(chat->name);
            fputs(",\"text\":", stdout);
            json_string(chat->text);
            puts("}");
            fflush(stdout);
        }
        if (exercise && phase == COOP_LOBBY) {
            uint8_t slot = coop_session_local_slot(s);
            const CoopPlayer* player = &coop_session_players(s)[slot];
            if (!selected) selected = coop_session_character(s, 16067 + slot, config.name);
            if (selected && player->prototype != 0 && !player->ready && (!readied || clock_ms() - start > 1000))
                readied = coop_session_ready(s, true);
            if (player->ready && !greeted) {
                char message[100];
                snprintf(message, sizeof(message), "Greetings from %s", config.name);
                greeted = coop_session_chat(s, message);
            }
        }
        if (!success || phase == COOP_OFFLINE) break;
        delay();
    } while (clock_ms() - start < (uint64_t)duration * 1000);
    coop_session_destroy(s);
    return success ? 0 : 1;
}
