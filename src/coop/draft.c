#include "coop/draft.h"
#include "coop/content_identity.h"
#include "coop/object_state.h"
#include "coop/bootstrap.h"
#include <errno.h>
#include <fcntl.h>
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

static bool write_record(const char* path, const uint8_t* bytes, size_t size)
{
    uint8_t random[COOP_IDENTITY_SIZE];
    if (!path || !*path || !bytes || !size || !coop_identity_generate(random)) return false;
    size_t length = strlen(path);
    if (length > 3600) return false;
    char* temporary = malloc(length + 39);
    if (!temporary) return false;
    memcpy(temporary, path, length); memcpy(temporary + length, ".tmp-", 5);
    for (unsigned i = 0; i < sizeof(random); i++) snprintf(temporary + length + 5 + i * 2, 3, "%02x", random[i]);
#ifdef _WIN32
    int fd = _open(temporary, _O_WRONLY | _O_BINARY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
#else
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd < 0) { free(temporary); return false; }
    bool good = true;
    size_t written = 0;
    while (written < size) {
        int n = (int)file_write(fd, bytes + written, (unsigned)(size - written));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { good = false; break; }
        written += (size_t)n;
    }
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
    return good;
}
bool coop_draft_save(const char* path, const uint8_t content[32], const uint8_t owner[16], const CoopCharacter* c)
{
    if (!content || !owner || !coop_identity_valid(owner)) return false;
    uint8_t bytes[COOP_DRAFT_BYTES] = { 0 };
    if (!coop_character_encode(c, bytes + 56)) return false;
    memcpy(bytes, "ACDRAFT2", 8);
    memcpy(bytes + 8, content, 32);
    memcpy(bytes + 40, owner, 16);
    coop_object_write32(bytes + sizeof(bytes) - 4, coop_snapshot_crc(bytes, sizeof(bytes) - 4));
    return write_record(path, bytes, sizeof(bytes));
}
static CoopDraftStatus read_record(const char* path, const uint8_t content[32], const uint8_t* owner, CoopCharacter* result)
{
    if (!path || !content || !result || (owner && !coop_identity_valid(owner))) return COOP_DRAFT_INVALID;
    FILE* file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? COOP_DRAFT_MISSING : COOP_DRAFT_IO;
    uint8_t bytes[COOP_DRAFT_BYTES + 1];
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    bool good = !ferror(file);
    if (fclose(file)) return COOP_DRAFT_IO;
    bool legacy = count == 172 && !memcmp(bytes, "ACDRAFT1", 8);
    bool draft = count == COOP_DRAFT_BYTES && !memcmp(bytes, "ACDRAFT2", 8);
    bool share = count == COOP_DRAFT_SHARE_BYTES && !memcmp(bytes, "ACBUILD1", 8);
    if (!good || (!legacy && !draft && (!share || owner)) || count < 44
        || coop_object_read32(bytes + count - 4) != coop_snapshot_crc(bytes, count - 4)) return COOP_DRAFT_INVALID;
    if (!coop_content_matches(content, bytes + 8)) return COOP_DRAFT_CONTENT;
    if (owner && memcmp(bytes + 40, owner, 16)) return COOP_DRAFT_PROFILE;
    CoopCharacter c;
    size_t offset = share ? 40 : 56;
    if (!coop_character_decode(&c, bytes + offset, count - offset - 4)) return COOP_DRAFT_INVALID;
    *result = c;
    return COOP_DRAFT_OK;
}
CoopDraftStatus coop_draft_load(const char* path, const uint8_t content[32], const uint8_t owner[16], CoopCharacter* result)
{
    if (!owner) return COOP_DRAFT_INVALID;
    return read_record(path, content, owner, result);
}
bool coop_draft_slot_path(char* out, size_t size, const char* canonical, unsigned slot)
{
    if (!out || !size || !canonical || slot > COOP_DRAFT_SLOTS) return false;
    int n = slot ? snprintf(out, size, "%s.slot-%02u", canonical, slot) : snprintf(out, size, "%s", canonical);
    return n > 0 && (size_t)n < size;
}
bool coop_draft_remember(const char* canonical, const uint8_t content[32], const uint8_t owner[16], const CoopCharacter* c, unsigned* chosen)
{
    if (!coop_character_valid(c)) return false;
    unsigned empty = 0;
    for (unsigned slot = 1; slot <= COOP_DRAFT_SLOTS; slot++) {
        char path[512];
        CoopCharacter old;
        if (!coop_draft_slot_path(path, sizeof(path), canonical, slot)) return false;
        CoopDraftStatus status = coop_draft_load(path, content, owner, &old);
        if (status == COOP_DRAFT_MISSING && !empty) empty = slot;
        if (status == COOP_DRAFT_OK && !strcmp(old.name, c->name) && old.race == c->race && old.gender == c->gender && old.background == c->background) {
            if (!coop_draft_save(path, content, owner, c)) return false;
            if (chosen) *chosen = slot;
            return true;
        }
    }
    char path[512];
    if (!empty || !coop_draft_slot_path(path, sizeof(path), canonical, empty) || !coop_draft_save(path, content, owner, c)) return false;
    if (chosen) *chosen = empty;
    return true;
}
bool coop_draft_export(const char* path, const uint8_t content[32], const CoopCharacter* c)
{
    uint8_t bytes[COOP_DRAFT_SHARE_BYTES] = { 0 };
    if (!content || !coop_character_encode(c, bytes + 40)) return false;
    memcpy(bytes, "ACBUILD1", 8);
    memcpy(bytes + 8, content, 32);
    coop_object_write32(bytes + sizeof(bytes) - 4, coop_snapshot_crc(bytes, sizeof(bytes) - 4));
    return write_record(path, bytes, sizeof(bytes));
}
CoopDraftStatus coop_draft_import(const char* path, const uint8_t content[32], CoopCharacter* result)
{
    return read_record(path, content, NULL, result);
}
bool coop_draft_delete(const char* canonical, unsigned slot)
{
    char path[512], trash[544];
    if (!slot || !coop_draft_slot_path(path, sizeof(path), canonical, slot)) return false;
    snprintf(trash, sizeof(trash), "%s.deleted", path);
#ifdef _WIN32
    return MoveFileExA(path, trash, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(path, trash) == 0;
#endif
}
