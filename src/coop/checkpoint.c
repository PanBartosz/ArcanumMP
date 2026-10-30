#include "coop/checkpoint.h"
#include "coop/bootstrap.h"
#include "coop/object_state.h"
#include "coop/snapshot.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#define file_write _write
#define file_sync _commit
#define file_close _close
#else
#include <unistd.h>
#define file_write write
#define file_sync fsync
#define file_close close
#endif
static bool info_valid(const CoopCheckpointInfo* info)
{
    if (!info || !info->epoch || info->map > INT32_MAX || !info->members || (info->members & ~15u)) return false;
    bool terminated = false;
    for (unsigned i = 0; i < sizeof(info->description); i++) {
        unsigned char c = (unsigned char)info->description[i];
        if (!c) terminated = true;
        else if (terminated || c < 32 || c > 126) return false;
    }
    if (!terminated) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        bool present = (info->members & (1u << i)) != 0;
        if (coop_identity_valid(info->owners[i]) != present) return false;
        if (present) for (unsigned j = 0; j < i; j++)
            if ((info->members & (1u << j)) && !memcmp(info->owners[i], info->owners[j], COOP_IDENTITY_SIZE)) return false;
    }
    return true;
}
static bool header_encode(const CoopCheckpointInfo* info, const void* snapshot, size_t size, uint8_t out[COOP_CHECKPOINT_HEADER])
{
    CoopSnapshotFiles files;
    if (!info_valid(info) || !coop_snapshot_parse(snapshot, size, &files)) return false;
    memset(out, 0, COOP_CHECKPOINT_HEADER);
    memcpy(out, "ACCOOP1", 7); memcpy(out + 8, info->content, 32);
    coop_object_write32(out + 40, info->epoch); coop_object_write32(out + 44, info->map);
    coop_object_write32(out + 48, (uint32_t)size); coop_object_write32(out + 52, coop_snapshot_crc(snapshot, size));
    out[56] = info->members; memcpy(out + 64, info->owners, 64); memcpy(out + 128, info->description, 64);
    coop_object_write32(out + 192, coop_snapshot_crc(out, 192));
    return true;
}
static bool header_decode(const uint8_t header[COOP_CHECKPOINT_HEADER], CoopCheckpointInfo* info, uint32_t* length)
{
    *info = (CoopCheckpointInfo) { 0 };
    memcpy(info->content, header + 8, 32); info->epoch = coop_object_read32(header + 40); info->map = coop_object_read32(header + 44);
    info->members = header[56]; memcpy(info->owners, header + 64, 64); memcpy(info->description, header + 128, 64);
    *length = coop_object_read32(header + 48);
    if (memcmp(header, "ACCOOP1\0", 8) || coop_object_read32(header + 192) != coop_snapshot_crc(header, 192)
        || !info_valid(info) || *length < 16 || *length > COOP_SNAPSHOT_LIMIT) return false;
    for (unsigned i = 57; i < 64; i++) if (header[i]) return false;
    return true;
}
CoopCheckpointStatus coop_checkpoint_inspect(const char* path, CoopCheckpointInfo* result, uint32_t* fingerprint, uint32_t* payload_size, uint64_t* modified)
{
    if (!path || !result || !fingerprint || !payload_size || !modified) return COOP_CHECKPOINT_INVALID;
    FILE* file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? COOP_CHECKPOINT_MISSING : COOP_CHECKPOINT_IO;
    uint8_t header[COOP_CHECKPOINT_HEADER];
    CoopCheckpointInfo info; uint32_t length = 0;
    struct stat details;
    bool good = fread(header, 1, sizeof(header), file) == sizeof(header) && header_decode(header, &info, &length)
        && fstat(fileno(file), &details) == 0 && (details.st_mode & S_IFMT) == S_IFREG
        && (uint64_t)details.st_size == COOP_CHECKPOINT_HEADER + (uint64_t)length;
    if (fclose(file)) return COOP_CHECKPOINT_IO;
    if (!good) return COOP_CHECKPOINT_INVALID;
    *result = info; *fingerprint = coop_object_read32(header + 192); *payload_size = length;
    *modified = details.st_mtime > 0 ? (uint64_t)details.st_mtime : 0;
    return COOP_CHECKPOINT_OK;
}
static bool write_all(int fd, const void* data, size_t size)
{
    const uint8_t* p = data;
    while (size) {
        unsigned chunk = size > 1048576 ? 1048576 : (unsigned)size;
        int n = (int)file_write(fd, p, chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n; size -= (size_t)n;
    }
    return true;
}
CoopCheckpointStatus coop_checkpoint_save(const char* path, const CoopCheckpointInfo* info, const void* snapshot, size_t size)
{
    uint8_t header[COOP_CHECKPOINT_HEADER], random[COOP_IDENTITY_SIZE];
    if (!path || !*path || strlen(path) > 3000 || !header_encode(info, snapshot, size, header)) return COOP_CHECKPOINT_INVALID;
    if (!coop_identity_generate(random)) return COOP_CHECKPOINT_IO;
    size_t length = strlen(path);
    char* temporary = malloc(length + 39);
    if (!temporary) return COOP_CHECKPOINT_IO;
    memcpy(temporary, path, length); memcpy(temporary + length, ".tmp-", 5);
    for (unsigned i = 0; i < sizeof(random); i++) snprintf(temporary + length + 5 + 2 * i, 3, "%02x", random[i]);
#ifdef _WIN32
    int fd = _open(temporary, _O_WRONLY | _O_BINARY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
#else
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd < 0) { free(temporary); return COOP_CHECKPOINT_IO; }
    bool good = write_all(fd, header, sizeof(header)) && write_all(fd, snapshot, size);
    if (good && file_sync(fd)) good = false;
    if (file_close(fd)) good = false;
    if (good) {
#ifdef _WIN32
        good = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        good = rename(temporary, path) == 0;
#endif
    }
    if (!good) remove(temporary);
    free(temporary);
    if (!good) return COOP_CHECKPOINT_IO;
#ifndef _WIN32
    char* parent = malloc(length + 2);
    if (!parent) return COOP_CHECKPOINT_COMMITTED_UNSYNCED;
    memcpy(parent, path, length + 1);
    char* slash = strrchr(parent, '/');
    if (!slash) strcpy(parent, "."); else if (slash == parent) slash[1] = 0; else *slash = 0;
    int directory = open(parent, O_RDONLY | O_DIRECTORY);
    free(parent);
    bool durable = directory >= 0 && fsync(directory) == 0;
    if (directory >= 0 && close(directory)) durable = false;
    if (!durable) return COOP_CHECKPOINT_COMMITTED_UNSYNCED;
#endif
    return COOP_CHECKPOINT_OK;
}
static CoopCheckpointStatus load(const char* path, const uint8_t content[32], const uint8_t owner[COOP_IDENTITY_SIZE], bool checked, uint32_t fingerprint, CoopCheckpointInfo* result, uint8_t** snapshot, size_t* size)
{
    if (!path || !content || !owner || !coop_identity_valid(owner) || !result || !snapshot || !size) return COOP_CHECKPOINT_INVALID;
    FILE* file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? COOP_CHECKPOINT_MISSING : COOP_CHECKPOINT_IO;
    uint8_t header[COOP_CHECKPOINT_HEADER];
    bool good = fread(header, 1, sizeof(header), file) == sizeof(header);
    CoopCheckpointInfo info = { 0 }; uint32_t length = 0;
    if (good) {
        good = header_decode(header, &info, &length) && (!checked || coop_object_read32(header + 192) == fingerprint);
    }
    if (!good) { fclose(file); return COOP_CHECKPOINT_INVALID; }
    if (memcmp(info.content, content, 32)) { fclose(file); return COOP_CHECKPOINT_CONTENT; }
    bool member = false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if ((info.members & (1u << i)) && !memcmp(info.owners[i], owner, COOP_IDENTITY_SIZE)) member = true;
    if (!member) { fclose(file); return COOP_CHECKPOINT_OWNER; }
    uint8_t* bytes = malloc(length);
    if (!bytes) { fclose(file); return COOP_CHECKPOINT_IO; }
    good = fread(bytes, 1, length, file) == length && fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) { free(bytes); return COOP_CHECKPOINT_IO; }
    CoopSnapshotFiles parsed;
    if (!good || coop_object_read32(header + 52) != coop_snapshot_crc(bytes, length) || !coop_snapshot_parse(bytes, length, &parsed)) {
        free(bytes); return COOP_CHECKPOINT_INVALID;
    }
    *result = info; *snapshot = bytes; *size = length;
    return COOP_CHECKPOINT_OK;
}
CoopCheckpointStatus coop_checkpoint_load(const char* path, const uint8_t content[32], const uint8_t owner[COOP_IDENTITY_SIZE], CoopCheckpointInfo* info, uint8_t** snapshot, size_t* size)
{
    return load(path, content, owner, false, 0, info, snapshot, size);
}
CoopCheckpointStatus coop_checkpoint_load_checked(const char* path, const uint8_t content[32], const uint8_t owner[COOP_IDENTITY_SIZE], uint32_t fingerprint, CoopCheckpointInfo* info, uint8_t** snapshot, size_t* size)
{
    return load(path, content, owner, true, fingerprint, info, snapshot, size);
}
