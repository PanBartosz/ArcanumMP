#include "coop/campaign.h"
#include "coop/authority.h"
#include "coop/engine_character.h"
#include "coop/starting_shop.h"
#include "coop/lobby_ui.h"
#include "game/item.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/obj_private.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include <stdio.h>
#include <string.h>

static CoopCampaignMember members[COOP_MAX_PLAYERS];
static unsigned member_count;
static const char manifest_path[] = "Save\\Current\\arcmp-party.bin";
enum { HEADER_SIZE = 16,
    MEMBER_SIZE = 69,
    MANIFEST_SIZE = HEADER_SIZE + COOP_MAX_PLAYERS * MEMBER_SIZE + 4 };

static uint32_t read_u32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void write_u32(uint8_t* p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}

void coop_campaign_reset(void)
{
    memset(members, 0, sizeof(members));
    member_count = 0;
}
bool coop_campaign_active(void) { return member_count != 0; }
unsigned coop_campaign_count(void) { return member_count; }
unsigned coop_campaign_next_slot(unsigned start)
{
    for (unsigned i = start; i < COOP_MAX_PLAYERS; i++)
        if (members[i].occupied) return i;
    return COOP_MAX_PLAYERS;
}
const CoopCampaignMember* coop_campaign_member(unsigned slot)
{
    return slot < COOP_MAX_PLAYERS && members[slot].occupied ? &members[slot] : NULL;
}
int64_t coop_campaign_object(unsigned slot)
{
    const CoopCampaignMember* member = coop_campaign_member(slot);
    return member ? obj_pool_perm_lookup(member->object_id) : OBJ_HANDLE_NULL;
}
int64_t coop_campaign_nearest_player(int64_t object)
{
    if (coop_authority_get() != COOP_AUTHORITY_HOST || !coop_campaign_active())
        return player_get_local_pc_obj();
    int64_t nearest = OBJ_HANDLE_NULL, distance = INT64_MAX;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t pc = coop_campaign_object(i);
        if (!pc || (obj_field_int32_get(pc, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
        int64_t candidate = object_dist(object, pc);
        if (candidate >= 0 && candidate < distance) {
            distance = candidate;
            nearest = pc;
        }
    }
    return nearest;
}
int coop_campaign_slot_for_object(int64_t object)
{
    if (!object || !member_count || !obj_handle_is_valid(object)) return -1;
    ObjectID id = obj_get_id(object);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (members[i].occupied && objid_is_equal(members[i].object_id, id)) return (int)i;
    }
    return -1;
}
int coop_campaign_slot_for_owner(const uint8_t owner[COOP_IDENTITY_SIZE])
{
    if (!coop_identity_valid(owner)) return -1;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (members[i].occupied && memcmp(members[i].owner, owner, COOP_IDENTITY_SIZE) == 0) return (int)i;
    }
    return -1;
}

bool coop_campaign_selections_valid(const CoopPlayer players[COOP_MAX_PLAYERS])
{
    coop_lobby_release_previews();
    if (coop_campaign_active() || !players[0].connected || !player_get_local_pc_obj()) return false;
    unsigned count = 0;
    /* Validate every selection before replacing the menu's placeholder PC. */
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (!players[i].connected) continue;
        if (!players[i].ready || !coop_identity_valid(players[i].identity)
            || !memchr(players[i].character, 0, COOP_NAME_SIZE) || !players[i].character[0]
            || players[i].prototype < 16000 || players[i].prototype > 16999) return false;
        int64_t prototype = sub_4685A0((int)players[i].prototype);
        if (!prototype || obj_field_int32_get(prototype, OBJ_F_TYPE) != OBJ_TYPE_PC) return false;
        if (players[i].custom && (players[i].prototype != 16066 || !coop_character_valid(&players[i].build)
            || strcmp(players[i].build.name, players[i].character) || !coop_engine_character_validate(&players[i].build))) return false;
        for (unsigned j = 0; j < i; j++) {
            if (players[j].connected && memcmp(players[i].identity, players[j].identity, COOP_IDENTITY_SIZE) == 0) return false;
        }
        count++;
    }
    return count >= 2;
}
bool coop_campaign_begin(const CoopPlayer players[COOP_MAX_PLAYERS])
{
    if (!coop_campaign_selections_valid(players)) return false;
    unsigned count = 0;
    int64_t location = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (!players[i].connected) continue;
        count++;
        PlayerCreateInfo info;
        player_create_info_init(&info);
        info.basic_prototype = (int)players[i].prototype;
        info.loc = location;
        info.flags = i == 0 ? 0 : PLAYER_CREATE_INFO_NETWORK;
        if (!player_obj_create_player(&info)) return false;
        if (players[i].custom) {
            if (!coop_engine_character_apply(info.obj, &players[i].build)) return false;
            if (!coop_starting_apply(info.obj, &players[i].build)) return false;
        }
        obj_field_string_set(info.obj, OBJ_F_PC_PLAYER_NAME, players[i].character);
        if (!item_wield_get(info.obj, ITEM_INV_LOC_WEAPON)) item_wield_best(info.obj, ITEM_INV_LOC_WEAPON, OBJ_HANDLE_NULL);
        if (!item_wield_get(info.obj, ITEM_INV_LOC_ARMOR)) item_wield_best(info.obj, ITEM_INV_LOC_ARMOR, OBJ_HANDLE_NULL);
        members[i].occupied = true;
        members[i].object_id = obj_get_id(info.obj);
        members[i].prototype = players[i].prototype;
        memcpy(members[i].owner, players[i].identity, COOP_IDENTITY_SIZE);
        memcpy(members[i].character, players[i].character, COOP_NAME_SIZE);
    }
    member_count = count;
    return coop_campaign_validate();
}

bool coop_campaign_validate(void)
{
    if (!coop_campaign_active()) return true;
    if (member_count < 2 || !members[0].occupied) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (!members[i].occupied) continue;
        int64_t obj = coop_campaign_object(i);
        if (!obj || !player_is_pc_obj(obj) || members[i].object_id.type != OID_TYPE_GUID) return false;
        for (unsigned j = 0; j < i; j++) {
            if (members[j].occupied && (objid_is_equal(members[i].object_id, members[j].object_id) || memcmp(members[i].owner, members[j].owner, COOP_IDENTITY_SIZE) == 0)) return false;
        }
    }
    return coop_campaign_slot_for_object(player_get_local_pc_obj()) >= 0;
}

bool coop_campaign_save(void)
{
    if (!coop_campaign_active()) {
        if (tig_file_exists(manifest_path, NULL) && tig_file_remove(manifest_path) != 0) return false;
        return true;
    }
    if (!coop_campaign_validate()) return false;
    uint8_t bytes[MANIFEST_SIZE] = { 0 };
    memcpy(bytes, "ACPARTY", 7);
    write_u32(bytes + 8, 1);
    write_u32(bytes + 12, member_count);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        uint8_t* p = bytes + HEADER_SIZE + i * MEMBER_SIZE;
        const CoopCampaignMember* m = &members[i];
        if (!m->occupied) continue;
        p[0] = 1;
        memcpy(p + 1, m->owner, 16);
        memcpy(p + 17, m->object_id.d.g.data, 16);
        write_u32(p + 33, m->prototype);
        memcpy(p + 37, m->character, COOP_NAME_SIZE);
    }
    write_u32(bytes + MANIFEST_SIZE - 4, (uint32_t)crc32(0, bytes, MANIFEST_SIZE - 4));
    TigFile* stream = tig_file_fopen(manifest_path, "wb");
    if (!stream) return false;
    bool good = tig_file_fwrite(bytes, 1, sizeof(bytes), stream) == sizeof(bytes);
    if (tig_file_fclose(stream) != 0) good = false;
    return good;
}

bool coop_campaign_load(void)
{
    if (!tig_file_exists(manifest_path, NULL)) {
        coop_campaign_reset();
        return true;
    }
    uint8_t bytes[MANIFEST_SIZE];
    TigFile* stream = tig_file_fopen(manifest_path, "rb");
    if (!stream) return false;
    bool good = tig_file_fread(bytes, 1, sizeof(bytes), stream) == sizeof(bytes)
        && tig_file_fgetc(stream) == EOF && !tig_file_ferror(stream);
    tig_file_fclose(stream);
    if (!good || memcmp(bytes, "ACPARTY\0", 8) || read_u32(bytes + 8) != 1
        || read_u32(bytes + MANIFEST_SIZE - 4) != (uint32_t)crc32(0, bytes, MANIFEST_SIZE - 4)) return false;
    CoopCampaignMember loaded[COOP_MAX_PLAYERS] = { 0 };
    unsigned count = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const uint8_t* p = bytes + HEADER_SIZE + i * MEMBER_SIZE;
        if (p[0] > 1) return false;
        if (!p[0]) continue;
        CoopCampaignMember* m = &loaded[i];
        m->occupied = true;
        memcpy(m->owner, p + 1, 16);
        m->object_id.type = OID_TYPE_GUID;
        memcpy(m->object_id.d.g.data, p + 17, 16);
        m->prototype = read_u32(p + 33);
        memcpy(m->character, p + 37, COOP_NAME_SIZE);
        if (!coop_identity_valid(m->owner) || !coop_identity_valid(m->object_id.d.g.data)
            || m->prototype < 16000 || m->prototype > 16999 || !m->character[0]
            || !memchr(m->character, 0, COOP_NAME_SIZE)) return false;
        for (unsigned j = 0; j < i; j++) {
            if (loaded[j].occupied && (objid_is_equal(m->object_id, loaded[j].object_id) || memcmp(m->owner, loaded[j].owner, COOP_IDENTITY_SIZE) == 0)) return false;
        }
        count++;
    }
    if (!loaded[0].occupied || count < 2 || count != read_u32(bytes + 12)) return false;
    memcpy(members, loaded, sizeof(members));
    member_count = count;
    return true;
}
