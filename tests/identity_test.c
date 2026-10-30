#include "coop/identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                \
        }                                                           \
    } while (0)
int main(void)
{
    char path[80];
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(path, sizeof(path), "identity-test-%lu.bin", pid);
    remove(path);
    uint8_t first[COOP_IDENTITY_SIZE], second[COOP_IDENTITY_SIZE];
    CHECK(coop_identity_load_or_create(path, first));
    CHECK(coop_identity_valid(first));
    CHECK(coop_identity_load_or_create(path, second));
    CHECK(memcmp(first, second, sizeof(first)) == 0);
    CHECK(coop_identity_generate(second));
    CHECK(memcmp(first, second, sizeof(first)) != 0);
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite("broken", 1, 6, file) == 6);
    CHECK(fclose(file) == 0);
    memset(second, 0x42, sizeof(second));
    CHECK(!coop_identity_load_or_create(path, second));
    for (unsigned i = 0; i < sizeof(second); i++)
        CHECK(second[i] == 0x42);
    file = fopen(path, "rb");
    char bytes[7] = { 0 };
    CHECK(file && fread(bytes, 1, 7, file) == 6 && !strcmp(bytes, "broken"));
    fclose(file);
    remove(path);
    puts("PASS: generated, persisted, reloadable identities; invalid profiles retained unchanged");
    return 0;
}
