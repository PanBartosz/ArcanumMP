#include "coop/draft.h"
#include "coop/bootstrap.h"
#include "coop/object_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define make_directory(p) _mkdir(p)
#define remove_directory _rmdir
#else
#include <unistd.h>
#define make_directory(p) mkdir(p, 0700)
#define remove_directory rmdir
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void write_bytes(const char* path, const uint8_t* bytes, size_t length)
{
    FILE* file = fopen(path, "wb"); CHECK(file);
    CHECK(fwrite(bytes, 1, length, file) == length); CHECK(!fclose(file));
}
static void compare(const CoopCharacter* a, const CoopCharacter* b)
{
    uint8_t first[COOP_CHARACTER_BYTES], second[COOP_CHARACTER_BYTES];
    CHECK(coop_character_encode(a, first) && coop_character_encode(b, second)); CHECK(!memcmp(first, second, sizeof(first)));
}
int main(void)
{
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    char directory[80], path[120], interrupted[128];
    snprintf(directory, sizeof(directory), "draft-test-%lu", pid); CHECK(!make_directory(directory));
    snprintf(path, sizeof(path), "%s/character.draft", directory);
    snprintf(interrupted, sizeof(interrupted), "%s.tmp-interrupted", path);
    uint8_t content[32] = { 42 }, owner[16] = { 1 }, other[32] = { 43 };
    CoopCharacter c = { .race = 0, .gender = 1, .portrait = 1, .unspent = 5, .name = "First draft" }, loaded;
    for (unsigned i = 0; i < 8; i++) c.stats[i] = 8;
    CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_MISSING);
    CHECK(coop_draft_save(path, content, owner, &c));
    CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_OK); compare(&c, &loaded);
    uint8_t bytes[COOP_DRAFT_BYTES + 1] = { 0 };
    FILE* file = fopen(path, "rb"); CHECK(file && fread(bytes, 1, sizeof(bytes), file) == COOP_DRAFT_BYTES); CHECK(!fclose(file));
    CHECK(!memcmp(bytes, "ACDRAFT2", 8) && !memcmp(bytes + 8, content, 32) && !memcmp(bytes + 40, owner, 16));
    CHECK(!memcmp(bytes + 56, "ACH2", 4));
    CHECK(coop_draft_load(path, other, owner, &loaded) == COOP_DRAFT_CONTENT); compare(&c, &loaded);
    CHECK(coop_draft_load(path, content, other, &loaded) == COOP_DRAFT_PROFILE); compare(&c, &loaded);
    for (unsigned index = 0; index < COOP_DRAFT_BYTES; index++) {
        bytes[index] ^= 0x40;
        write_bytes(path, bytes, COOP_DRAFT_BYTES);
        CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_INVALID); compare(&c, &loaded);
        bytes[index] ^= 0x40;
    }
    bytes[0] = 'X';
    coop_object_write32(bytes + COOP_DRAFT_BYTES - 4, coop_snapshot_crc(bytes, COOP_DRAFT_BYTES - 4));
    write_bytes(path, bytes, COOP_DRAFT_BYTES);
    CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_INVALID); compare(&c, &loaded);
    bytes[0] = 'A';
    coop_object_write32(bytes + COOP_DRAFT_BYTES - 4, coop_snapshot_crc(bytes, COOP_DRAFT_BYTES - 4));
    for (unsigned length = 0; length < COOP_DRAFT_BYTES; length++) {
        write_bytes(path, bytes, length); CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_INVALID); compare(&c, &loaded);
    }
    write_bytes(path, bytes, sizeof(bytes)); CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_INVALID);
    bytes[74] = bytes[75] = 0;
    coop_object_write32(bytes + COOP_DRAFT_BYTES - 4, coop_snapshot_crc(bytes, COOP_DRAFT_BYTES - 4));
    write_bytes(path, bytes, COOP_DRAFT_BYTES); CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_INVALID);
    CHECK(coop_draft_save(path, content, owner, &c));
    write_bytes(interrupted, (const uint8_t*)"incomplete", 10);
    CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_OK); compare(&c, &loaded);
    CoopCharacter invalid = c; invalid.stats[0] = 0;
    CHECK(!coop_draft_save(path, content, owner, &invalid));
    CHECK(!coop_draft_save(directory, content, owner, &c));
    CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_OK); compare(&c, &loaded);
    strcpy(c.name, "Replacement draft"); c.hp = 1; c.unspent = 4;
    CHECK(coop_draft_save(path, content, owner, &c));
    CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_OK); compare(&c, &loaded);
#ifndef _WIN32
    struct stat info; CHECK(!stat(path, &info) && (info.st_mode & 0777) == 0600);
#endif
    uint8_t legacy[172] = { 0 };
    memcpy(legacy, "ACDRAFT1", 8);
    memcpy(legacy + 8, content, 32);
    memcpy(legacy + 40, owner, 16);
    uint8_t recipe[COOP_CHARACTER_BYTES];
    CHECK(coop_character_encode(&c, recipe));
    memcpy(recipe, "ACH1", 4);
    memcpy(legacy + 56, recipe, 112);
    coop_object_write32(legacy + 168, coop_snapshot_crc(legacy, 168));
    write_bytes(path, legacy, sizeof(legacy));
    CHECK(coop_draft_load(path, content, owner, &loaded) == COOP_DRAFT_OK);
    compare(&c, &loaded);
    char shared[160];
    snprintf(shared, sizeof(shared), "%s/share.build", directory);
    CHECK(coop_draft_export(shared, content, &c));
    CHECK(coop_draft_import(shared, content, &loaded) == COOP_DRAFT_OK);
    compare(&c, &loaded);
    CHECK(coop_draft_import(shared, other, &loaded) == COOP_DRAFT_CONTENT);
    compare(&c, &loaded);
    CHECK(coop_draft_load(shared, content, owner, &loaded) == COOP_DRAFT_INVALID);
    unsigned chosen = 99;
    for (unsigned slot = 1; slot <= COOP_DRAFT_SLOTS; slot++) {
        snprintf(c.name, sizeof(c.name), "Library %u", slot);
        CHECK(coop_draft_remember(path, content, owner, &c, &chosen) && chosen == slot);
    }
    strcpy(c.name, "Overflow");
    CHECK(!coop_draft_remember(path, content, owner, &c, &chosen));
    strcpy(c.name, "Library 3");
    c.hp = 2;
    CHECK(coop_draft_remember(path, content, owner, &c, &chosen) && chosen == 3);
    CHECK(coop_draft_delete(path, 3));
    CHECK(coop_draft_remember(path, content, owner, &c, &chosen) && chosen == 3);
    CHECK(!coop_draft_delete(path, 0));
    for (unsigned slot = 1; slot <= COOP_DRAFT_SLOTS; slot++) {
        char slot_path[160];
        CHECK(coop_draft_slot_path(slot_path, sizeof(slot_path), path, slot));
        CHECK(!remove(slot_path));
    }
    char removed[180];
    snprintf(removed, sizeof(removed), "%s.slot-03.deleted", path);
    CHECK(!remove(removed));
    CHECK(!remove(shared));
    CHECK(!remove(path)); CHECK(!remove(interrupted)); CHECK(!remove_directory(directory));
    puts("draft files: atomic replacement, interruption/failure preservation, context, shape and truncation checks passed");
    return 0;
}
