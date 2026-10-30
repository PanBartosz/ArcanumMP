#include "coop/snapshot.h"
#include "coop/bootstrap.h"
#include "coop/documents.h"
#include "coop/dialogue_recovery.h"
#include "coop/environment_state.h"
#include "coop/journal.h"
#include "coop/magic_state.h"
#include "coop/map_notes.h"
#include "coop/play_events.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t le32(const uint8_t* p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
static void put_be32(uint8_t* p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (24 - i * 8));
}
static bool fade_valid(const uint8_t* p)
{
    uint32_t bits = be32(p + 12); float duration; memcpy(&duration, &bits, sizeof(duration));
    return !(be32(p) & ~1u) && be32(p + 8) <= 1024 && isfinite(duration) && duration >= 0 && duration <= 60;
}
static bool extra_sidecar_valid(unsigned kind, const uint8_t* p, size_t size)
{
    if (kind==2048) return coop_play_events_file_valid(p,size);
    if (kind == 64) {
        CoopDialogueRecovery* state = malloc(sizeof(*state));
        bool good = state && coop_dialogue_recovery_decode(state, p, size); free(state); return good;
    }
    if (kind == 128) {
        if (size != 112 || memcmp(p, "ACTRANS1", 8) || be32(p + 8) > 1 || be32(p + 108) != coop_snapshot_crc(p, 108)) return false;
        if (!be32(p + 8)) { for (unsigned i = 12; i < 108; i++) if (p[i]) return false; return true; }
        CoopObjectId actor; memcpy(actor.bytes, p + 12, sizeof(actor.bytes));
        uint32_t flags = be32(p + 36);
        return coop_object_id_valid(&actor, false) && actor.bytes[0] == 2 && !p[29] && !p[30] && !p[31]
            && be32(p + 32) <= INT32_MAX && be32(p + 48) <= INT32_MAX && !(flags & ~0x37fu)
            && (!(flags & 2) || fade_valid(p + 76)) && (!(flags & 4) || fade_valid(p + 92));
    }
    if (kind == 256) {
        CoopMagicView* view = malloc(sizeof(*view));
        bool good = view && size == COOP_MAGIC_VIEW_HEADER + 4
            && be32(p + size - 4) == coop_snapshot_crc(p, size - 4)
            && coop_magic_view_decode(p, size - 4, view) && !view->count;
        free(view); return good;
    }
    if (kind == 512) {
        CoopMapNotes* notes = malloc(sizeof(*notes));
        bool good = notes && coop_map_notes_decode(notes, p, size); free(notes); return good;
    }
    if (kind == 1024) {
        if (size < 16 || memcmp(p, "ACTRAPS1", 8) || be32(p + 8) > 65536
            || size != 16 + (size_t)be32(p + 8) * 24 || be32(p + size - 4) != coop_snapshot_crc(p, size - 4)) return false;
        uint32_t previous_map = 0; CoopObjectId previous = { 0 };
        for (uint32_t i = 0; i < be32(p + 8); i++) {
            const uint8_t* record = p + 12 + (size_t)i * 24;
            CoopObjectId id; memcpy(id.bytes, record, sizeof(id.bytes)); uint32_t map = be32(record + 17);
            if (!coop_object_id_valid(&id, false) || (id.bytes[0] != 2 && id.bytes[0] != 3)
                || map > INT32_MAX || record[21] >= COOP_MAX_PLAYERS || record[22] || record[23]
                || (id.bytes[0] == 3 && be32(id.bytes + 13) != map)
                || (i && (map < previous_map || (map == previous_map && memcmp(previous.bytes, id.bytes, sizeof(id.bytes)) >= 0)))) return false;
            previous = id; previous_map = map;
        }
        return true;
    }
    return false;
}
static bool safe_name(const uint8_t* name, size_t size)
{
    if (!size || size > 200 || name[size - 1] == '.' || name[size - 1] == ' ') return false;
    char stem[5] = { 0 };
    unsigned stem_size = 0;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = name[i];
        if (c < 32 || c > 126 || strchr("/\\:*?\"<>|", c)) return false;
        if (c == '.') break;
        if (stem_size < 4) stem[stem_size++] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    if (!strcmp(stem, "CON") || !strcmp(stem, "PRN") || !strcmp(stem, "AUX") || !strcmp(stem, "NUL")) return false;
    if (stem_size == 4 && (!memcmp(stem, "COM", 3) || !memcmp(stem, "LPT", 3)) && stem[3] >= '1' && stem[3] <= '9') return false;
    /* Check the entire name, including the extension. */
    for (size_t i = 0; i < size; i++)
        if (name[i] < 32 || name[i] > 126 || strchr("/\\:*?\"<>|", name[i])) return false;
    return true;
}
static bool valid_index(const uint8_t* index, size_t size, const uint8_t* archive, size_t archive_size)
{
    size_t at = 0, data_size = 0, path_size = 0;
    size_t lengths[32];
    unsigned depth = 0, required = 0;
    while (at + 4 <= size) {
        uint32_t type = le32(index + at);
        at += 4;
        if (type == 3) return at == size && depth == 0 && data_size == archive_size && (required & 7) == 7;
        if (type == 2) {
            if (!depth) return false;
            path_size -= lengths[--depth];
            continue;
        }
        if (type > 1 || at + 4 > size) return false;
        size_t bytes = le32(index + at);
        at += 4;
        if (bytes > size - at || !safe_name(index + at, bytes) || path_size + bytes > 220) return false;
        if (type == 1) {
            if (depth >= 32 || (depth == 0 && (bytes != 4 || memcmp(index + at, "maps", 4)))) return false;
            lengths[depth++] = bytes + 1;
            path_size += bytes + 1;
        }
        bool environment = false, journal = false, documents = false;
        unsigned extra = 0;
        if (type == 0 && depth == 0) {
            unsigned flag = bytes == 8 && !memcmp(index + at, "data.sav", 8)                                              ? 1
                : bytes == 9 && !memcmp(index + at, "data2.sav", 9)                                                       ? 2
                : bytes == 15 && !memcmp(index + at, "arcmp-party.bin", 15)                                               ? 4
                : bytes == 21 && !memcmp(index + at, "arcmp-environment.bin", 21)                                         ? 8
                : bytes == sizeof("arcmp-conversations.bin") - 1 && !memcmp(index + at, "arcmp-conversations.bin", bytes) ? 16
                : bytes == sizeof("arcmp-documents.bin") - 1 && !memcmp(index + at, "arcmp-documents.bin", bytes)         ? 32
                : bytes == sizeof("arcmp-dialogue.bin") - 1 && !memcmp(index + at, "arcmp-dialogue.bin", bytes)           ? 64
                : bytes == sizeof("arcmp-transition.bin") - 1 && !memcmp(index + at, "arcmp-transition.bin", bytes)       ? 128
                : bytes == sizeof("arcmp-spell-destinations.bin") - 1 && !memcmp(index + at, "arcmp-spell-destinations.bin", bytes) ? 256
                : bytes == sizeof("arcmp-map-notes.bin") - 1 && !memcmp(index + at, "arcmp-map-notes.bin", bytes)         ? 512
                : bytes == sizeof("arcmp-traps.bin") - 1 && !memcmp(index + at, "arcmp-traps.bin", bytes)                 ? 1024
                : bytes == sizeof("arcmp-events.bin") - 1 && !memcmp(index + at, "arcmp-events.bin", bytes)               ? 2048
                                                                                                                          : 0;
            /* Native town-map exploration/notes are saved beside data.sav, including
             * town names with spaces. safe_name still confines extraction to
             * the current save directory. These files appear after travel. */
            bool town_map = bytes > 4 && (!memcmp(index + at + bytes - 4, ".tmf", 4)
                || !memcmp(index + at + bytes - 4, ".tmn", 4));
            if ((!flag && !town_map) || (required & flag)) return false;
            required |= flag;
            environment = flag == 8;
            journal = flag == 16;
            documents = flag == 32;
            if (flag >= 64) extra = flag;
        }
        at += bytes;
        if (type == 0) {
            if (at + 4 > size) return false;
            size_t length = le32(index + at);
            at += 4;
            if (length > archive_size - data_size) return false;
            if (environment) {
                CoopEnvironmentState state;
                if (length < 4
                    || !coop_environment_decode(archive + data_size, length - 4, &state)
                    || be32(archive + data_size + length - 4) != coop_snapshot_crc(archive + data_size, length - 4)) return false;
            }
            if (journal) {
                CoopJournal* transcript = coop_journal_create();
                bool good = transcript && coop_journal_decode(transcript, archive + data_size, length);
                coop_journal_destroy(transcript);
                if (!good) return false;
            }
            if (documents) {
                CoopDocuments catalogue;
                if (!coop_documents_decode(&catalogue, archive + data_size, length)) return false;
            }
            if (extra && !extra_sidecar_valid(extra, archive + data_size, length)) return false;
            data_size += length;
        }
    }
    return false;
}
bool coop_snapshot_parse(const void* bundle, size_t size, CoopSnapshotFiles* files)
{
    const uint8_t* p = bundle;
    if (!p || !files || size < 16 || size > COOP_SNAPSHOT_LIMIT || memcmp(p, "ACSNAP1\0", 8)) return false;
    size_t index_size = be32(p + 8), archive_size = be32(p + 12);
    if (!index_size || !archive_size || index_size > size - 16 || archive_size != size - 16 - index_size
        || !valid_index(p + 16, index_size, p + 16 + index_size, archive_size)) return false;
    *files = (CoopSnapshotFiles) { p + 16, index_size, p + 16 + index_size, archive_size };
    return true;
}
uint8_t* coop_snapshot_pack(const CoopSnapshotFiles* files, size_t* size)
{
    if (!files || !size || !files->index || !files->archive || !files->index_size || !files->archive_size
        || files->index_size > COOP_SNAPSHOT_LIMIT - 16 || files->archive_size > COOP_SNAPSHOT_LIMIT - 16 - files->index_size
        || !valid_index(files->index, files->index_size, files->archive, files->archive_size)) return NULL;
    size_t length = 16 + files->index_size + files->archive_size;
    uint8_t* bytes = malloc(length);
    if (!bytes) return NULL;
    memcpy(bytes, "ACSNAP1\0", 8);
    put_be32(bytes + 8, (uint32_t)files->index_size);
    put_be32(bytes + 12, (uint32_t)files->archive_size);
    memcpy(bytes + 16, files->index, files->index_size);
    memcpy(bytes + 16 + files->index_size, files->archive, files->archive_size);
    *size = length;
    return bytes;
}
