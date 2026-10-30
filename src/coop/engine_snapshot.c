#include "coop/engine_snapshot.h"
#include "game/combat.h"
#include "coop/authority.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/snapshot.h"
#include "game/gamelib.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/player.h"
#include "game/scroll.h"
#include "ui/mainmenu_ui.h"
#include "ui/intgame.h"
#include <stdlib.h>

static const char slot[] = "arcmp-bootstrap";
static const char index_path[] = "Save\\arcmp-bootstrap.tfai";
static const char archive_path[] = "Save\\arcmp-bootstrap.tfaf";

static uint8_t* read_file(const char* path, size_t* size)
{
    TigFile* file = tig_file_fopen(path, "rb");
    if (!file) return NULL;
    int length = tig_file_filelength(file);
    uint8_t* bytes = length > 0 && (unsigned)length < COOP_SNAPSHOT_LIMIT ? malloc((size_t)length) : NULL;
    bool good = bytes && tig_file_fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (tig_file_fclose(file) != 0) good = false;
    if (!good) {
        free(bytes);
        return NULL;
    }
    *size = (size_t)length;
    return bytes;
}
uint8_t* coop_engine_snapshot_capture(size_t* size)
{
    if (!coop_campaign_active() || !gamelib_save(slot, "Network bootstrap")) return NULL;
    CoopSnapshotFiles files = { 0 };
    uint8_t* index = read_file(index_path, &files.index_size);
    uint8_t* archive = read_file(archive_path, &files.archive_size);
    files.index = index;
    files.archive = archive;
    uint8_t* bundle = coop_snapshot_pack(&files, size);
    free(index);
    free(archive);
    return bundle;
}
static bool write_file(const char* path, const void* bytes, size_t size)
{
    TigFile* file = tig_file_fopen(path, "wb");
    if (!file) return false;
    bool good = tig_file_fwrite(bytes, 1, size, file) == size;
    if (tig_file_fclose(file) != 0) good = false;
    return good;
}
static bool restore(const void* data, size_t size, const uint8_t owner[COOP_IDENTITY_SIZE], bool host)
{
    CoopSnapshotFiles files;
    if (!coop_identity_valid(owner) || !coop_snapshot_parse(data, size, &files)) return false;
    if (!write_file(index_path, files.index, files.index_size) || !write_file(archive_path, files.archive, files.archive_size)) return false;
    coop_authority_set(host ? COOP_AUTHORITY_HOST : COOP_AUTHORITY_REPLICA);
    mainmenu_ui_reset();
    if (!gamelib_load(slot)) return false;
    int member = coop_campaign_slot_for_owner(owner);
    if (member < 0) return false;
    int64_t pc = coop_campaign_object((unsigned)member);
    if (!pc || !sub_40DAF0(pc)) return false;
    intgame_counters_refresh();
    combat_campaign_refresh_presentation();
    int64_t location = obj_field_int64_get(pc, OBJ_F_LOCATION);
    scroll_set_center(location);
    location_origin_set(location);
    return coop_campaign_validate();
}
bool coop_engine_snapshot_restore(const void* data, size_t size, const uint8_t owner[COOP_IDENTITY_SIZE]) { return restore(data, size, owner, false); }
bool coop_engine_snapshot_restore_host(const void* data, size_t size, const uint8_t owner[COOP_IDENTITY_SIZE]) { return restore(data, size, owner, true); }
