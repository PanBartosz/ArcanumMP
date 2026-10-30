#include "coop/checkpoint.h"
#include "coop/snapshot.h"
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
static void le32(uint8_t* p, uint32_t n) { for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8*i)); }
static void write_bytes(const char* path, const uint8_t* bytes, size_t n)
{
    FILE* f = fopen(path, "wb"); CHECK(f); CHECK(fwrite(bytes, 1, n, f) == n && !fclose(f));
}
static void unchanged_load(const char* path, const uint8_t content[32], const uint8_t owner[16], CoopCheckpointStatus expected)
{
    CoopCheckpointInfo info, before; memset(&info, 0x45, sizeof(info)); before = info;
    uint8_t* bytes = (uint8_t*)(uintptr_t)0x1234; size_t n = 987;
    CHECK(coop_checkpoint_load(path, content, owner, &info, &bytes, &n) == expected);
    CHECK(!memcmp(&info, &before, sizeof(info)) && bytes == (uint8_t*)(uintptr_t)0x1234 && n == 987);
}
int main(void)
{
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    char dir[80], path[120], interrupted[160]; snprintf(dir, sizeof(dir), "checkpoint-test-%lu", pid); CHECK(!make_directory(dir));
    snprintf(path, sizeof(path), "%s/party.acsave", dir); snprintf(interrupted, sizeof(interrupted), "%s.tmp-interrupted", path);
    uint8_t index[120] = { 0 }, content[32] = { 11 }, other[32] = { 12 };
    const char* names[] = { "data.sav", "data2.sav", "arcmp-party.bin" }; size_t at = 0;
    for (unsigned i = 0; i < 3; i++) {
        le32(index + at, 0); le32(index + at + 4, (uint32_t)strlen(names[i])); memcpy(index + at + 8, names[i], strlen(names[i])); at += 8 + strlen(names[i]);
        le32(index + at, 1); at += 4;
    }
    le32(index + at, 3); at += 4;
    CoopSnapshotFiles files = { index, at, (const uint8_t*)"abc", 3 }; size_t size = 0;
    uint8_t* snapshot = coop_snapshot_pack(&files, &size); CHECK(snapshot);
    CoopCheckpointInfo info = { .members = 15, .epoch = 7, .map = 1, .description = "Party save" }, loaded;
    memcpy(info.content, content, 32); for (unsigned i = 0; i < 4; i++) info.owners[i][0] = (uint8_t)(i + 1);
    unchanged_load(path, content, info.owners[0], COOP_CHECKPOINT_MISSING);
    CHECK(coop_checkpoint_save(path, &info, snapshot, size) == COOP_CHECKPOINT_OK);
    uint8_t* restored = NULL; size_t restored_size = 0;
    CHECK(coop_checkpoint_load(path, content, info.owners[3], &loaded, &restored, &restored_size) == COOP_CHECKPOINT_OK);
    CHECK(restored_size == size && !memcmp(restored, snapshot, size) && loaded.epoch == 7); free(restored);
    write_bytes(interrupted, snapshot, size / 2);
    CHECK(coop_checkpoint_load(path, content, info.owners[0], &loaded, &restored, &restored_size) == COOP_CHECKPOINT_OK && loaded.epoch == 7); free(restored);
    unchanged_load(path, other, info.owners[0], COOP_CHECKPOINT_CONTENT);
    unchanged_load(path, content, other, COOP_CHECKPOINT_OWNER);
    info.epoch = 8; CHECK(coop_checkpoint_save(path, &info, snapshot, size) == COOP_CHECKPOINT_OK);
    info.owners[1][0] = 1; CHECK(coop_checkpoint_save(path, &info, snapshot, size) == COOP_CHECKPOINT_INVALID); info.owners[1][0] = 2;
    CHECK(coop_checkpoint_save(dir, &info, snapshot, size) == COOP_CHECKPOINT_IO);
    CHECK(coop_checkpoint_load(path, content, info.owners[2], &loaded, &restored, &restored_size) == COOP_CHECKPOINT_OK && loaded.epoch == 8); free(restored);
    size_t length = COOP_CHECKPOINT_HEADER + size; uint8_t* bytes = malloc(length + 1); CHECK(bytes);
    FILE* f = fopen(path, "rb"); CHECK(f && fread(bytes, 1, length, f) == length && !fclose(f)); bytes[length] = 0;
    for (size_t i = 0; i < length; i++) {
        bytes[i] ^= 0x40; write_bytes(path, bytes, length); unchanged_load(path, content, info.owners[0], COOP_CHECKPOINT_INVALID); bytes[i] ^= 0x40;
    }
    const size_t truncated[] = { 0, COOP_CHECKPOINT_HEADER - 1, length - 1, length + 1 };
    for (unsigned i = 0; i < 4; i++) { write_bytes(path, bytes, truncated[i]); unchanged_load(path, content, info.owners[0], COOP_CHECKPOINT_INVALID); }
    coop_object_write32(bytes + 48, UINT32_MAX); coop_object_write32(bytes + 192, coop_snapshot_crc(bytes, 192));
    write_bytes(path, bytes, length); unchanged_load(path, content, info.owners[0], COOP_CHECKPOINT_INVALID);
    free(bytes); free(snapshot); CHECK(!remove(path) && !remove(interrupted) && !remove_directory(dir));
    puts("PASS: atomic checkpoint replacement, interrupted staging, failed rename, content/profile bounds and corruption"); return 0;
}
