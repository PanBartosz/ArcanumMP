#include "coop/quickbar.h"
#include "coop/bootstrap.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
static bool slot_valid(const CoopQuickbarSlot* slot)
{
    if (slot->kind > 4 || !coop_object_id_valid(&slot->item, true)) return false;
    if (slot->kind == 1 || slot->kind == 4) return slot->item.bytes[0] == 2 && slot->prototype && slot->prototype <= INT32_MAX
        && (slot->kind == 4 ? slot->data < 4096 : !slot->data);
    if (slot->item.bytes[0] || slot->prototype) return false;
    return slot->kind == 0 ? !slot->data : slot->kind == 2 ? slot->data < 4 : slot->data < 4096;
}
bool coop_quickbar_encode(const CoopQuickbar* bar, uint8_t bytes[COOP_QUICKBAR_BYTES])
{
    if (!bar || !bytes || !coop_identity_valid(bar->owner) || !coop_identity_valid(bar->character)) return false;
    memset(bytes, 0, COOP_QUICKBAR_BYTES); memcpy(bytes, "ACKEYS01", 8);
    memcpy(bytes + 8, bar->owner, 16); memcpy(bytes + 24, bar->character, 16);
    for (unsigned i = 0; i < COOP_QUICKBAR_COUNT; i++) {
        const CoopQuickbarSlot* slot = &bar->slots[i];
        if (!slot_valid(slot)) return false;
        uint8_t* p = bytes + 40 + i * 29;
        p[0] = slot->kind; coop_object_write32(p + 4, slot->data); memcpy(p + 8, slot->item.bytes, 17);
        coop_object_write32(p + 25, slot->prototype);
    }
    coop_object_write32(bytes + COOP_QUICKBAR_BYTES - 4, coop_snapshot_crc(bytes, COOP_QUICKBAR_BYTES - 4));
    return true;
}
bool coop_quickbar_decode(CoopQuickbar* result, const void* data, size_t size)
{
    if (!result || !data || size != COOP_QUICKBAR_BYTES) return false;
    const uint8_t* bytes = data;
    if (memcmp(bytes, "ACKEYS01", 8) || coop_object_read32(bytes + size - 4) != coop_snapshot_crc(bytes, size - 4)) return false;
    CoopQuickbar bar = { 0 }; memcpy(bar.owner, bytes + 8, 16); memcpy(bar.character, bytes + 24, 16);
    if (!coop_identity_valid(bar.owner) || !coop_identity_valid(bar.character)) return false;
    for (unsigned i = 0; i < COOP_QUICKBAR_COUNT; i++) {
        const uint8_t* p = bytes + 40 + i * 29;
        CoopQuickbarSlot* slot = &bar.slots[i];
        slot->kind = p[0]; slot->data = coop_object_read32(p + 4); memcpy(slot->item.bytes, p + 8, 17);
        slot->prototype = coop_object_read32(p + 25);
        if (p[1] || p[2] || p[3] || !slot_valid(slot)) return false;
    }
    *result = bar;
    return true;
}
bool coop_quickbar_path(const CoopQuickbar* bar, char path[COOP_QUICKBAR_PATH])
{
    if (!bar || !path || !coop_identity_valid(bar->owner) || !coop_identity_valid(bar->character)) return false;
    strcpy(path, "arcmp-quickbar-");
    for (unsigned i = 0; i < 32; i++) {
        uint8_t value = i < 16 ? bar->owner[i] : bar->character[i - 16];
        snprintf(path + 14 + 2 * i, 3, "%02x", value);
    }
    strcpy(path + 78, ".bin");
    return true;
}
bool coop_quickbar_save(const CoopQuickbar* bar)
{
    char path[COOP_QUICKBAR_PATH], temporary[COOP_QUICKBAR_PATH + 40];
    uint8_t bytes[COOP_QUICKBAR_BYTES], random[COOP_IDENTITY_SIZE];
    if (!coop_quickbar_encode(bar, bytes) || !coop_quickbar_path(bar, path) || !coop_identity_generate(random)) return false;
    size_t length = strlen(path); memcpy(temporary, path, length); memcpy(temporary + length, ".tmp-", 5);
    for (unsigned i = 0; i < sizeof(random); i++) snprintf(temporary + length + 5 + 2 * i, 3, "%02x", random[i]);
    FILE* file = fopen(temporary, "wbx");
    if (!file) return false;
    bool good = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && fflush(file) == 0;
#ifdef _WIN32
    if (good && _commit(_fileno(file))) good = false;
#else
    if (good && fsync(fileno(file))) good = false;
#endif
    if (fclose(file)) good = false;
    if (good) {
#ifdef _WIN32
        good = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        good = rename(temporary, path) == 0;
#endif
    }
    if (!good) remove(temporary);
    return good;
}
bool coop_quickbar_load(CoopQuickbar* bar)
{
    char path[COOP_QUICKBAR_PATH];
    if (!coop_quickbar_path(bar, path)) return false;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    uint8_t bytes[COOP_QUICKBAR_BYTES]; CoopQuickbar loaded;
    bool good = fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) good = false;
    if (!good || !coop_quickbar_decode(&loaded, bytes, sizeof(bytes)) || memcmp(loaded.owner, bar->owner, 16) || memcmp(loaded.character, bar->character, 16)) return false;
    *bar = loaded;
    return true;
}
