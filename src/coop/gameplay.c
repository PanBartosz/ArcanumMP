#include "coop/gameplay.h"
#include "coop/modern.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_documents.h"
#include "coop/engine_magic.h"
#include "coop/engine_object.h"
#include "coop/engine_sector.h"
#include "coop/reconnect.h"
#include "coop/runtime.h"
#include "coop/world_net.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/gamelib.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "ui/charedit_ui.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/iso.h"
#include "ui/schematic_ui.h"
#include "ui/spell_ui.h"
#include "ui/written_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GAME_HELLO = 21,
    GAME_START,
    GAME_MOVE,
    GAME_RESULT,
    GAME_POSES,
    GAME_STOP,
    GAME_FINISH,
    GAME_FINISHED,
    GAME_NPC_POSES,
    GAME_TALK = 35,
    GAME_HALT = 41,
    POSE_BYTES = 48,
    POSES_SIZE = 13 + COOP_MAX_PLAYERS * POSE_BYTES };
enum { NPC_POSE_BYTES = 53, NPC_POSES_PER_PACKET = (COOP_MESSAGE_CAPACITY - 13) / NPC_POSE_BYTES };
enum { NPC_POSE_CACHE = 4096, NPC_POSE_PACKETS_PER_TICK = 4 };
typedef struct NpcPose {
    int64_t object;
    uint64_t seen_at, refreshed_at;
    uint8_t pending;
    uint8_t bytes[NPC_POSE_BYTES];
} NpcPose;
typedef struct GameplayPeer {
    bool present;
    bool hello;
    bool started;
    uint8_t owner[COOP_IDENTITY_SIZE];
    int member;
    uint32_t sequence;
    uint32_t result;
    bool accepted;
    bool final_pose_sent;
    bool finish_sent;
    bool finished;
    bool quiesce_sent, quiesced;
    unsigned commands;
    uint64_t window;
} GameplayPeer;
struct CoopGameplay {
    CoopSession* session;
    bool host;
    bool hello_sent;
    bool running;
    bool bindings_ready;
    bool failed;
    bool finishing;
    bool draining_native;
    bool final_revision_started;
    bool quiesce_received, quiesce_ack_sent, final_received, world_sealing;
    bool finished;
    bool paused;
    bool retired;
    unsigned references;
    uint32_t epoch;
    uint32_t next_epoch;
    int map;
    uint64_t now;
    uint64_t action_now, action_real;
    uint64_t opened;
    uint64_t last_sent;
    uint64_t last_pose_at;
    uint64_t finish_at;
    uint32_t revision;
    uint32_t received_revision;
    uint32_t npc_revision, received_npc_revision, cursor_pose, npc_cursor;
    unsigned combat_engaged;
    NpcPose npc_poses[NPC_POSE_CACHE];
    uint32_t roster_revision;
    uint32_t command_sequence;
    uint32_t last_result;
    bool last_accepted;
    unsigned accepted;
    unsigned rejected;
    GameplayPeer peers[COOP_MAX_PLAYERS];
    const char* error;
    CoopWorldNetwork* world;
    CoopSectorStream* sectors;
    CoopDialogue* dialogue;
    CoopItems* items;
    CoopBattle* battle;
    CoopMagic* magic;
    CoopSkills* skills;
    CoopProgression* progression;
    CoopServices* services;
    CoopFollowers* followers;
    CoopNotes* notes;
    CoopEngineAudio* audio;
    int talker;
    AnimID talk_goal;
    ObjectID talk_actor, talk_target;
    uint32_t talk_generation;
};
static uint32_t read_u32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void write_u32(uint8_t* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(value >> (24 - i * 8));
}
static bool fail(CoopGameplay* g, const char* error)
{
    if (g->retired) return false;
    if (!g->failed && g->host) {
        uint8_t message[4];
        write_u32(message, g->epoch);
        for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++)
            if (g->peers[i].present) coop_session_send(g->session, i, GAME_STOP, message, 4);
    }
    g->failed = true;
    g->running = false;
    coop_engine_audio_quiet(g->audio);
    g->error = error;
    return false;
}
static bool message(void* context, uint8_t sender, uint8_t type, const uint8_t* p, size_t size);
static bool linked(const CoopGameplay* g)
{
    if (!g->host) return g->hello_sent;
    for (unsigned i=1;i<COOP_MAX_PLAYERS;i++) if (g->peers[i].present && !g->peers[i].hello) return false;
    return true;
}
static int64_t command_actor(CoopGameplay* g, uint8_t sender, const uint8_t* p);
static int64_t command_owned_actor(CoopGameplay* g, uint8_t sender, const uint8_t* p);
static int64_t battle_actor(void* context, uint8_t sender, const uint8_t* p)
{
    CoopGameplay* g = context;
    if (g->running && !g->failed && !g->finishing && !g->paused && !g->draining_native
        && !coop_runtime_entry_pending() && (p[28] == COOP_COMBAT_POLICY || p[28] == COOP_COMBAT_IMMORTAL)) {
        const GameplayPeer* peer = &g->peers[sender];
        const CoopCampaignMember* member = coop_campaign_member((unsigned)peer->member);
        return peer->hello && (!sender || peer->started) && member
                && read_u32(p + 8) == (uint32_t)g->map && map_current_map() == g->map
                && !memcmp(member->object_id.d.g.data, p + 12, 16)
            ? coop_campaign_object((unsigned)peer->member) : OBJ_HANDLE_NULL;
    }
    return g->running && !g->failed && !g->finishing ? command_owned_actor(g, sender, p) : OBJ_HANDLE_NULL;
}
static int64_t item_actor(void* context, uint8_t sender, const uint8_t* p)
{
    CoopGameplay* g = context;
    const CoopDialogueView* view = coop_dialogue_current(g->dialogue);
    bool allowed = !view->active || (view->mode == 3 && view->chooser == sender && p[28] == COOP_ITEM_ARRANGE);
    return g->running && !g->failed && !g->finishing && allowed ? command_actor(g, sender, p) : OBJ_HANDLE_NULL;
}
static int64_t progression_actor(void* context, uint8_t sender, const uint8_t* p)
{
    CoopGameplay* g = context;
    if (!g->running || g->failed) return OBJ_HANDLE_NULL;
    if (p[28] != COOP_PROGRESS_CLOSE && p[28] != COOP_PROGRESS_FATE && p[28] != COOP_PROGRESS_OPTIONS) return command_owned_actor(g, sender, p);
    if (p[28] != COOP_PROGRESS_CLOSE && (g->finishing || g->paused || coop_runtime_entry_pending())) return OBJ_HANDLE_NULL;
    /* Closing relinquishes edit permission. Native fate healing also permits
     * an unconscious living PC. Validate those owned identities independently
     * of ordinary active-actor actions. */
    const GameplayPeer* peer = &g->peers[sender];
    const CoopCampaignMember* member = coop_campaign_member((unsigned)peer->member);
    return peer->hello && (!sender || peer->started) && member && read_u32(p + 8) == (uint32_t)g->map
        && !memcmp(member->object_id.d.g.data, p + 12, 16) ? coop_campaign_object((unsigned)peer->member) : OBJ_HANDLE_NULL;
}
static bool choose_dialogue(void* context, unsigned option)
{
    CoopGameplay* g = context;
    return !g->failed && !g->finishing && !g->paused && coop_engine_dialogue_choose(context, option);
}
CoopGameplay* coop_gameplay_create(CoopSession* session, uint32_t epoch)
{
    if (!session || !epoch || !coop_session_all_ready(session) || !coop_campaign_validate() || !coop_campaign_active()) return NULL;
    unsigned connected = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        connected += coop_session_players(session)[i].connected;
    if (connected != coop_campaign_count()) return NULL;
    CoopGameplay* g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->session = session;
    g->talker = -1;
    g->host = coop_session_is_host(session);
    g->action_now = g->action_real = SDL_GetTicks();
    g->epoch = epoch;
    g->map = map_current_map();
    g->roster_revision = coop_session_revision(session);
    g->error = "";
    const CoopPlayer* players = coop_session_players(session);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        GameplayPeer* peer = &g->peers[i];
        peer->present = players[i].connected;
        peer->member = -1;
        if (g->host || i == coop_session_local_slot(session)) {
            memcpy(peer->owner, players[i].identity, COOP_IDENTITY_SIZE);
            if (peer->present) peer->member = coop_campaign_slot_for_owner(peer->owner);
            if (peer->present && peer->member < 0) {
                free(g);
                return NULL;
            }
        }
    }
    g->peers[0].hello = g->host;
    g->bindings_ready = g->host;
    g->sectors = coop_sector_stream_create();
    g->world = g->sectors ? coop_world_net_create(session, epoch) : NULL;
    if (!g->world) {
        coop_sector_stream_destroy(g->sectors);
        free(g);
        return NULL;
    }
    g->dialogue = coop_dialogue_create(session, epoch, SDL_GetTicks(), coop_engine_dialogue_present, choose_dialogue, g);
    if (!g->dialogue || !coop_engine_dialogue_bind(g->dialogue, session)) {
        coop_dialogue_destroy(g->dialogue);
        coop_world_net_destroy(g->world);
        coop_sector_stream_destroy(g->sectors);
        free(g);
        return NULL;
    }
    g->items = coop_items_create(session, g->world, epoch, item_actor, g);
    g->battle = coop_battle_create(session, g->world, epoch, SDL_GetTicks(), battle_actor, g);
    g->magic = coop_magic_create(session, g->world, epoch, SDL_GetTicks(), battle_actor, g);
    g->skills = coop_skills_create(session, g->world, g->items, epoch, SDL_GetTicks(), battle_actor, g);
    g->progression = coop_progression_create(session, g->world, epoch, SDL_GetTicks(), progression_actor, g);
    g->services = coop_services_create(session, g->world, g->dialogue, epoch, SDL_GetTicks(), battle_actor, g);
    g->followers = coop_followers_create(session, g->world, epoch, battle_actor, g);
    g->notes = coop_notes_create(session, g->world, epoch, g->action_now, battle_actor, g);
    g->audio = coop_engine_audio_create(session, epoch, (uint32_t)g->map);
    if (!g->items || !g->battle || !g->magic || !g->skills || !g->progression || !g->services || !g->followers || !g->notes || !g->audio) {
        coop_notes_destroy(g->notes);
        coop_engine_audio_destroy(g->audio);
        coop_items_destroy(g->items);
        coop_battle_destroy(g->battle);
        coop_magic_destroy(g->magic);
        coop_skills_destroy(g->skills);
        coop_progression_destroy(g->progression);
        coop_services_destroy(g->services);
        coop_followers_destroy(g->followers);
        coop_engine_dialogue_unbind();
        coop_dialogue_destroy(g->dialogue);
        coop_world_net_destroy(g->world);
        coop_sector_stream_destroy(g->sectors);
        free(g);
        return NULL;
    }
    coop_authority_set(g->host ? COOP_AUTHORITY_HOST : COOP_AUTHORITY_REPLICA);
    coop_engine_magic_reset();
    coop_session_set_handler(session, message, g);
    return g;
}
static void gameplay_free(CoopGameplay* g)
{
    coop_notes_destroy(g->notes);
    coop_items_destroy(g->items);
    coop_battle_destroy(g->battle);
    coop_magic_destroy(g->magic);
    coop_skills_destroy(g->skills);
    coop_progression_destroy(g->progression);
    coop_services_destroy(g->services);
    coop_followers_destroy(g->followers);
    coop_dialogue_destroy(g->dialogue);
    coop_world_net_destroy(g->world);
    free(g);
}
void coop_gameplay_retain(CoopGameplay* g) { if (g) g->references++; }
void coop_gameplay_release(CoopGameplay* g)
{
    if (!g || !g->references) return;
    if (!--g->references && g->retired) gameplay_free(g);
}
void coop_gameplay_destroy(CoopGameplay* g)
{
    if (!g || g->retired) return;
    g->retired = true;
    g->running = false;
    g->finishing = true;
    coop_world_net_retire(g->world);
    coop_dialogue_retire(g->dialogue);
    /* Retire native bindings now, before a replacement epoch binds its own
     * UI/tickets/sector pins. Deferred freeing must only release allocations. */
    charedit_coop_stop();
    coop_session_set_handler(g->session, NULL, NULL);
    coop_engine_audio_destroy(g->audio); g->audio = NULL;
    inven_ui_coop_stop(); schematic_ui_coop_stop();
    coop_items_retire(g->items); coop_magic_retire(g->magic);
    coop_engine_magic_reset(); written_ui_coop_stop(); coop_engine_dialogue_unbind();
    coop_authority_set(COOP_AUTHORITY_LOCAL);
    coop_sector_stream_destroy(g->sectors); g->sectors = NULL;
    if (!g->references) gameplay_free(g);
}
static bool command_begin(CoopGameplay* g, uint8_t sender, const uint8_t* p)
{
    GameplayPeer* peer = &g->peers[sender];
    if (!g->running || !peer->hello || (sender && !peer->started)
        || read_u32(p) != g->epoch) return false;
    uint32_t sequence = read_u32(p + 4);
    if (!sequence || sequence <= peer->sequence) return false;
    peer->sequence = sequence;
    if (g->now - peer->window >= 1000) { peer->window = g->now; peer->commands = 0; }
    return ++peer->commands <= 30;
}
static int64_t command_owned_actor(CoopGameplay* g, uint8_t sender, const uint8_t* p)
{
    GameplayPeer* peer = &g->peers[sender];
    const CoopCampaignMember* member = coop_campaign_member((unsigned)peer->member);
    int64_t pc = coop_campaign_object((unsigned)peer->member);
    /* Valid intentions can cross the host's transition announcement. Drain
     * them as ordinary refusals without applying them or dropping the party. */
    return !g->paused && !g->finishing && !g->draining_native && !coop_runtime_entry_pending() && peer->hello && (!sender || peer->started) && member && pc && !memcmp(member->object_id.d.g.data, p + 12, 16)
            && read_u32(p + 8) == (uint32_t)g->map && map_current_map() == g->map && critter_is_active(pc)
            && !(obj_field_int32_get(pc, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
            && !(obj_field_int32_get(pc, OBJ_F_CRITTER_FLAGS) & (OCF_PARALYZED | OCF_STUNNED))
            && !(obj_field_int32_get(pc, OBJ_F_SPELL_FLAGS) & OSF_STONED)
        ? pc
        : OBJ_HANDLE_NULL;
}
static int64_t command_actor(CoopGameplay* g, uint8_t sender, const uint8_t* p)
{
    int64_t pc = command_owned_actor(g, sender, p);
    return pc && (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == pc) ? pc : OBJ_HANDLE_NULL;
}
static bool command_result(CoopGameplay* g, uint8_t sender, bool accepted, const char* action)
{
    GameplayPeer* peer = &g->peers[sender];
    peer->result = peer->sequence;
    peer->accepted = accepted;
    if (accepted) g->accepted++; else g->rejected++;
    if (!sender) { g->last_result = peer->sequence; g->last_accepted = accepted; }
    fprintf(stderr, "ARCMP command action=%s sender=%u sequence=%u accepted=%d\n", action, sender, peer->sequence, accepted);
    return true;
}
static bool command_location(const uint8_t* p, int64_t* location)
{
    uint32_t x = read_u32(p + 28), y = read_u32(p + 32);
    int64_t max_x, max_y;
    location_limits_get(&max_x, &max_y);
    *location = location_make(x, y);
    return x < (uint64_t)max_x && y < (uint64_t)max_y;
}
static bool execute_move(CoopGameplay* g, uint8_t sender, const uint8_t* p, size_t size)
{
    if (size < 41 || p[36] > 1) return false;
    unsigned count = size == 41 ? 1 : p[41];
    if (count < 1 || count > COOP_MOVE_WAYPOINTS
        || size != (count == 1 ? 41u : 42u + (count - 1) * 8u)
        || !command_begin(g, sender, p)) return false;
    int64_t pc = command_actor(g, sender, p), targets[COOP_MOVE_WAYPOINTS];
    bool accepted = command_location(p, &targets[0]) && pc && !coop_dialogue_current(g->dialogue)->active && read_u32(p + 37) == combat_turn_generation_get();
    int64_t max_x, max_y;
    location_limits_get(&max_x, &max_y);
    for (unsigned i = 1; i < count; i++) {
        uint32_t x = read_u32(p + 42 + (i - 1) * 8), y = read_u32(p + 46 + (i - 1) * 8);
        targets[i] = location_make(x, y);
        accepted &= x < (uint64_t)max_x && y < (uint64_t)max_y && targets[i] != targets[i - 1];
    }
    /* Validate every waypoint before replacing the current action. Native
     * subgoals retain route order and save/load behavior; no client movement
     * scheduler may continue issuing old clicks after combat or a transition. */
    if (accepted) accepted = sub_424070(pc, PRIORITY_3, false, false);
    if (accepted) accepted = p[36] ? anim_goal_run_to_tile(pc, targets[0]) : anim_goal_walk_to_tile(pc, targets[0]);
    for (unsigned i = 1; accepted && i < count; i++) {
        accepted = p[36] ? sub_434030(pc, targets[i]) : anim_goal_queue_walk_to_tile(pc, targets[i]);
        if (!accepted) sub_424070(pc, PRIORITY_3, false, false);
    }
    return command_result(g, sender, accepted, "move");
}
static bool execute_halt(CoopGameplay* g, uint8_t sender, const uint8_t* p, size_t size)
{
    if (size != 40 || !command_begin(g, sender, p)) return false;
    int64_t pc = command_actor(g, sender, p), target;
    bool accepted = command_location(p, &target) && pc && !coop_dialogue_current(g->dialogue)->active && read_u32(p + 36) == combat_turn_generation_get();
    if (accepted) accepted = sub_424070(pc, PRIORITY_3, false, false);
    if (accepted) anim_goal_rotate(pc, location_rot(obj_field_int64_get(pc, OBJ_F_LOCATION), target));
    return command_result(g, sender, accepted, "halt");
}
static bool execute_talk(CoopGameplay* g, uint8_t sender, const uint8_t* p, size_t size)
{
    if (size != 49 || !command_begin(g, sender, p)) return false;
    int64_t pc = command_actor(g, sender, p);
    CoopObjectId target;
    memcpy(target.bytes, p + 28, sizeof(target.bytes));
    bool accepted = pc && read_u32(p + 45) == combat_turn_generation_get() && g->talker < 0 && coop_engine_dialogue_can_start(pc)
        && coop_object_id_valid(&target, false) && (target.bytes[0] == 2 || target.bytes[0] == 3);
    if (accepted && target.bytes[0] == 3) {
        int64_t max_x, max_y;
        location_limits_get(&max_x, &max_y);
        accepted = coop_object_read32(target.bytes + 1) < (uint64_t)max_x
            && coop_object_read32(target.bytes + 5) < (uint64_t)max_y
            && coop_object_read32(target.bytes + 13) == (uint32_t)g->map;
    }
    int64_t npc = accepted ? obj_pool_perm_lookup(coop_object_id_to_engine(target)) : OBJ_HANDLE_NULL;
    accepted = accepted && npc && obj_field_int32_get(npc, OBJ_F_TYPE) == OBJ_TYPE_NPC
        && !(obj_field_int32_get(npc, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
        && (critter_is_dead(npc)
                ? (obj_field_int32_get(npc, OBJ_F_SPELL_FLAGS) & OSF_SPOKEN_WITH_DEAD) != 0
                : !critter_is_unconscious(npc));
    if (accepted) {
        AnimGoalData goal;
        AnimID id;
        accepted = sub_44D500(&goal, pc, AG_TALK) && sub_424070(pc, PRIORITY_3, false, false);
        if (accepted) {
            goal.params[AGDATA_TARGET_OBJ].obj = npc;
            accepted = anim_goal_add(&goal, &id);
            if (accepted) {
                g->talker = g->peers[sender].member;
                g->talk_goal = id;
                g->talk_actor = obj_get_id(pc);
                g->talk_target = obj_get_id(npc);
                g->talk_generation = read_u32(p + 45);
            }
        }
    }
    if (!accepted) fprintf(stderr, "ARCMP talk refused actor=%d paused=%d generation=%u expected=%u talker=%d dialogue-ready=%d target=%lld\n", pc != 0, g->paused, read_u32(p + 45), combat_turn_generation_get(), g->talker, pc && coop_engine_dialogue_can_start(pc), (long long)npc);
    return command_result(g, sender, accepted, "talk");
}
bool coop_gameplay_native_talk_allowed(CoopGameplay* g, int64_t actor, int64_t target, const AnimID* goal)
{
    if (!g || !g->host || !goal) return false;
    if (g->talker < 0 || !anim_id_is_equal(&g->talk_goal, goal)) return true;
    bool allowed = objid_is_equal(g->talk_actor, obj_get_id(actor)) && objid_is_equal(g->talk_target, obj_get_id(target))
        && g->talk_generation == combat_turn_generation_get() && critter_is_active(actor)
        && (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == actor)
        && !(obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT));
    if (!allowed) fprintf(stderr, "ARCMP talk approach retired generation=%u expected=%u\n", combat_turn_generation_get(), g->talk_generation);
    return allowed;
}
static void replica_visual_field_set(int64_t object, int field, int value)
{
    if (obj_field_int32_get(object,field) == value) return;
    /* A duplicate position/art sample may correctly skip movement redraw.
     * Visibility, size and status changes still need their old/new footprints
     * restored even when that actor is standing completely still. */
    TigRect before, after;
    object_get_rect(object,7,&before);
    gamelib_invalidate_rect(&before);
    object_invalidate_rect(&before);
    obj_field_int32_set(object,field,value);
    object_get_rect(object,7,&after);
    gamelib_invalidate_rect(&after);
    object_invalidate_rect(&after);
}
static bool receive_poses(CoopGameplay* g, const uint8_t* p, size_t size)
{
    if (!g->running || size != POSES_SIZE || read_u32(p + 4) <= g->received_revision || read_u32(p + 8) != (uint32_t)g->map || p[12] != coop_campaign_count()) return false;
    int64_t max_x, max_y;
    location_limits_get(&max_x, &max_y);
    /* Validate the whole frame before mutating any object. */
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        const uint8_t* pose = p + 13 + i * POSE_BYTES;
        if (!member) {
            for (unsigned j = 0; j < POSE_BYTES; j++)
                if (pose[j]) return false;
            continue;
        }
        if (!coop_campaign_object(i) || memcmp(member->object_id.d.g.data, pose, 16)
            || read_u32(pose + 16) >= (uint64_t)max_x || read_u32(pose + 20) >= (uint64_t)max_y
            || (int32_t)read_u32(pose + 24) < -1024 || (int32_t)read_u32(pose + 24) > 1024
            || (int32_t)read_u32(pose + 28) < -1024 || (int32_t)read_u32(pose + 28) > 1024) return false;
    }
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        int64_t obj = coop_campaign_object(i);
        if (!obj) continue;
        const uint8_t* pose = p + 13 + i * POSE_BYTES;
        object_replica_pose(obj, location_make(read_u32(pose + 16), read_u32(pose + 20)),
            (int32_t)read_u32(pose + 24), (int32_t)read_u32(pose + 28), read_u32(pose + 32));
        const unsigned local_flags = OF_TEXT | OF_TEXT_FLOATER | OF_HAS_OVERLAYS | OF_HAS_UNDERLAYS;
        unsigned flags = ((unsigned)read_u32(pose + 36) & ~local_flags)
            | ((unsigned)obj_field_int32_get(obj, OBJ_F_FLAGS) & local_flags);
        replica_visual_field_set(obj, OBJ_F_FLAGS, (int)flags);
        obj_field_int32_set(obj, OBJ_F_HP_DAMAGE, (int32_t)read_u32(pose + 40));
        obj_field_int32_set(obj, OBJ_F_CRITTER_FATIGUE_DAMAGE, (int32_t)read_u32(pose + 44));
    }
    g->received_revision = read_u32(p + 4);
    g->last_pose_at = g->now;
    /* Pose packets only change appearance and vitals. Rebuilding the rotating
     * controls 20 times per second erased client spell/skill descriptions and
     * disrupted hover/press state. Ability controls refresh on actual edits. */
    intgame_draw_bar(INTGAME_BAR_HEALTH);
    intgame_draw_bar(INTGAME_BAR_FATIGUE);
    return true;
}
static bool receive_npc_poses(CoopGameplay* g, const uint8_t* p, size_t size)
{
    if (!g->running || size < 13 || !p[12] || p[12] > NPC_POSES_PER_PACKET
        || size != 13u + p[12] * NPC_POSE_BYTES || read_u32(p + 8) != (uint32_t)g->map
        || read_u32(p + 4) <= g->received_npc_revision) return false;
    int64_t max_x, max_y; location_limits_get(&max_x, &max_y);
    int64_t objects[NPC_POSES_PER_PACKET];
    for (unsigned i = 0; i < p[12]; i++) {
        const uint8_t* pose = p + 13 + i * NPC_POSE_BYTES;
        CoopObjectId id; memcpy(id.bytes, pose, sizeof(id.bytes));
        if (!coop_object_id_valid(&id, false) || (id.bytes[0] != 2 && id.bytes[0] != 3)
            || read_u32(pose + 17) >= (uint64_t)max_x || read_u32(pose + 21) >= (uint64_t)max_y
            || (int32_t)read_u32(pose + 25) < -1024 || (int32_t)read_u32(pose + 25) > 1024
            || (int32_t)read_u32(pose + 29) < -1024 || (int32_t)read_u32(pose + 29) > 1024) return false;
        objects[i] = obj_pool_perm_lookup(coop_object_id_to_engine(id));
        if (objects[i] && obj_field_int32_get(objects[i], OBJ_F_TYPE) != OBJ_TYPE_NPC) return false;
    }
    for (unsigned i = 0; i < p[12]; i++) {
        int64_t obj = objects[i]; if (!obj) continue; /* Its creation batch may still be in transit. */
        const uint8_t* pose = p + 13 + i * NPC_POSE_BYTES;
        object_replica_pose(obj, location_make(read_u32(pose + 17), read_u32(pose + 21)),
            (int32_t)read_u32(pose + 25), (int32_t)read_u32(pose + 29), read_u32(pose + 33));
        const unsigned local = OF_TEXT | OF_TEXT_FLOATER | OF_HAS_OVERLAYS | OF_HAS_UNDERLAYS;
        replica_visual_field_set(obj, OBJ_F_FLAGS, (read_u32(pose + 37) & ~local) | (obj_field_int32_get(obj, OBJ_F_FLAGS) & local));
        obj_field_int32_set(obj, OBJ_F_HP_DAMAGE, (int32_t)read_u32(pose + 41));
        obj_field_int32_set(obj, OBJ_F_CRITTER_FATIGUE_DAMAGE, (int32_t)read_u32(pose + 45));
        replica_visual_field_set(obj, OBJ_F_CRITTER_FLAGS, (int32_t)read_u32(pose + 49));
    }
    g->received_npc_revision = read_u32(p + 4);
    return true;
}
static void send_npc_poses_full(CoopGameplay* g)
{
    uint8_t bytes[COOP_MESSAGE_CAPACITY] = { 0 };
    write_u32(bytes, g->epoch); write_u32(bytes + 8, (uint32_t)g->map);
    int64_t obj; int iterator; unsigned count = 0;
    bool more = obj_inst_first(&obj, &iterator);
    while (more) {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC
            && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT | OF_INVENTORY))) {
            bool nearby = false;
            for (unsigned slot = 0; slot < COOP_MAX_PLAYERS; slot++) {
                int64_t pc = coop_campaign_object(slot);
                nearby |= pc && object_dist(pc, obj) <= 64;
            }
            CoopObjectId id;
            if (nearby && coop_object_id_from_engine(obj_get_id(obj), &id)) {
                uint8_t* pose = bytes + 13 + count++ * NPC_POSE_BYTES;
                memcpy(pose, id.bytes, sizeof(id.bytes));
                int64_t loc = obj_field_int64_get(obj, OBJ_F_LOCATION);
                write_u32(pose + 17, (uint32_t)location_get_x(loc)); write_u32(pose + 21, (uint32_t)location_get_y(loc));
                write_u32(pose + 25, obj_field_int32_get(obj, OBJ_F_OFFSET_X)); write_u32(pose + 29, obj_field_int32_get(obj, OBJ_F_OFFSET_Y));
                write_u32(pose + 33, obj_field_int32_get(obj, OBJ_F_CURRENT_AID)); write_u32(pose + 37, obj_field_int32_get(obj, OBJ_F_FLAGS));
                write_u32(pose + 41, obj_field_int32_get(obj, OBJ_F_HP_DAMAGE)); write_u32(pose + 45, obj_field_int32_get(obj, OBJ_F_CRITTER_FATIGUE_DAMAGE));
                write_u32(pose + 49, obj_field_int32_get(obj, OBJ_F_CRITTER_FLAGS));
            }
        }
        more = obj_inst_next(&obj, &iterator);
        if (count && (count == NPC_POSES_PER_PACKET || !more)) {
            if (g->npc_revision == UINT32_MAX) { fail(g, "NPC presentation sequence exhausted."); return; }
            write_u32(bytes + 4, ++g->npc_revision); bytes[12] = (uint8_t)count;
            for (unsigned slot = 1; slot < COOP_MAX_PLAYERS; slot++)
                if (g->peers[slot].present && g->peers[slot].started)
                    coop_session_send(g->session, (uint8_t)slot, GAME_NPC_POSES, bytes, 13 + count * NPC_POSE_BYTES);
            count = 0;
        }
    }
}
static NpcPose* npc_pose_entry(CoopGameplay* g, int64_t object)
{
    uint64_t key = (uint64_t)object;
    unsigned start = (unsigned)(key ^ (key >> 32) ^ (key >> 13)) % NPC_POSE_CACHE;
    NpcPose* available = NULL;
    for (unsigned i = 0; i < NPC_POSE_CACHE; i++) {
        NpcPose* entry = &g->npc_poses[(start + i) % NPC_POSE_CACHE];
        if (entry->object == object) return entry;
        if (!entry->object) { if (!available) available = entry; break; }
        if (!available && g->now - entry->seen_at > 2000) available = entry;
    }
    if (available) *available = (NpcPose){ .object = object };
    return available;
}
static bool send_npc_batch(CoopGameplay* g, uint8_t slot, uint8_t* bytes, NpcPose** entries, unsigned count)
{
    if (g->npc_revision == UINT32_MAX) { fail(g, "NPC presentation sequence exhausted."); return false; }
    write_u32(bytes + 4, ++g->npc_revision);
    bytes[12] = (uint8_t)count;
    if (!coop_session_send(g->session, slot, GAME_NPC_POSES, bytes, 13 + count * NPC_POSE_BYTES)) return false;
    for (unsigned i = 0; i < count; i++) entries[i]->pending &= (uint8_t)~(1u << slot);
    return true;
}
static void send_npc_poses(CoopGameplay* g)
{
    uint8_t recipients = 0;
    int64_t party[COOP_MAX_PLAYERS];
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        party[i] = coop_campaign_object(i);
        if (i && g->peers[i].present && g->peers[i].started) recipients |= (uint8_t)(1u << i);
    }
    if (!recipients) return;
    int64_t object; int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC
            || (obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT | OF_INVENTORY))) continue;
        bool nearby = false;
        for (unsigned i = 0; i < COOP_MAX_PLAYERS && !nearby; i++)
            nearby = party[i] && object_dist(party[i], object) <= 64;
        CoopObjectId id;
        if (!nearby || !coop_object_id_from_engine(obj_get_id(object), &id)) continue;
        NpcPose* entry = npc_pose_entry(g, object);
        /* Very large modded crowds retain the original full-stream behavior;
         * the cache is an optimization, never an actor visibility limit. */
        if (!entry) { send_npc_poses_full(g); return; }
        uint8_t pose[NPC_POSE_BYTES];
        memcpy(pose, id.bytes, sizeof(id.bytes));
        int64_t loc = obj_field_int64_get(object, OBJ_F_LOCATION);
        write_u32(pose + 17, (uint32_t)location_get_x(loc)); write_u32(pose + 21, (uint32_t)location_get_y(loc));
        write_u32(pose + 25, obj_field_int32_get(object, OBJ_F_OFFSET_X)); write_u32(pose + 29, obj_field_int32_get(object, OBJ_F_OFFSET_Y));
        write_u32(pose + 33, obj_field_int32_get(object, OBJ_F_CURRENT_AID)); write_u32(pose + 37, obj_field_int32_get(object, OBJ_F_FLAGS));
        write_u32(pose + 41, obj_field_int32_get(object, OBJ_F_HP_DAMAGE)); write_u32(pose + 45, obj_field_int32_get(object, OBJ_F_CRITTER_FATIGUE_DAMAGE));
        write_u32(pose + 49, obj_field_int32_get(object, OBJ_F_CRITTER_FLAGS));
        if (!entry->refreshed_at || memcmp(entry->bytes, pose, sizeof(pose))) {
            memcpy(entry->bytes, pose, sizeof(pose)); entry->pending = recipients; entry->refreshed_at = g->now;
        } else if (g->now - entry->refreshed_at >= 1000) {
            entry->pending |= recipients; entry->refreshed_at = g->now;
        }
        entry->seen_at = g->now;
    } while (obj_inst_next(&object, &iterator));

    /* Preserve unsent changes per peer, send only the newest sample after
     * backpressure, and rotate crowded scenes so late actors cannot starve. */
    for (uint8_t slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        if (!(recipients & (1u << slot))) continue;
        uint8_t bytes[COOP_MESSAGE_CAPACITY] = { 0 };
        write_u32(bytes, g->epoch); write_u32(bytes + 8, (uint32_t)g->map);
        NpcPose* entries[NPC_POSES_PER_PACKET];
        unsigned count = 0, packets = 0;
        for (unsigned i = 0; i < NPC_POSE_CACHE; i++) {
            NpcPose* entry = &g->npc_poses[(g->npc_cursor + i) % NPC_POSE_CACHE];
            if (!entry->object || entry->seen_at != g->now || !(entry->pending & (1u << slot))) continue;
            entries[count] = entry;
            memcpy(bytes + 13 + count++ * NPC_POSE_BYTES, entry->bytes, NPC_POSE_BYTES);
            if (count == NPC_POSES_PER_PACKET) {
                if (!send_npc_batch(g, slot, bytes, entries, count)) { count = 0; break; }
                count = 0;
                if (++packets == NPC_POSE_PACKETS_PER_TICK) break;
            }
        }
        if (count) send_npc_batch(g, slot, bytes, entries, count);
    }
    g->npc_cursor = (g->npc_cursor + NPC_POSES_PER_PACKET * NPC_POSE_PACKETS_PER_TICK) % NPC_POSE_CACHE;
}
static void refresh_combat_presentation(CoopGameplay* g)
{
    if (g->host && !combat_turn_based_is_active() && !g->paused && !g->finishing) {
        bool danger = false; int64_t obj; int iterator;
        if (obj_inst_first(&obj, &iterator)) do {
            if (!combat_is_party_enemy(obj) || critter_is_unconscious(obj)) continue;
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                int64_t pc = coop_campaign_object(i);
                if (pc && object_dist(pc, obj) <= 32) danger = true;
            }
        } while (obj_inst_next(&obj, &iterator));
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            int64_t pc = coop_campaign_object(i); if (!pc) continue;
            if (danger && combat_critter_is_combat_mode_active(pc)) g->combat_engaged |= 1u << i;
            if (!danger && (g->combat_engaged & (1u << i)) && combat_can_exit_combat_mode(pc)
                && !anim_is_attacking(pc, 0, OBJ_HANDLE_NULL)) {
                combat_critter_deactivate_combat_mode(pc); g->combat_engaged &= ~(1u << i);
            }
        }
    }
    int64_t local = player_get_local_pc_obj();
    uint32_t pose = local ? (uint32_t)obj_field_int32_get(local, OBJ_F_CURRENT_AID) : 0;
    /* Only weapon/stance changes affect the base cursor; frame changes do not. */
    pose = tig_art_critter_id_weapon_get(pose) | (local && combat_critter_is_combat_mode_active(local) ? 256u : 0);
    if (pose != g->cursor_pose) { g->cursor_pose = pose; intgame_refresh_cursor(); }
}
static bool handle_message(void* context, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    CoopGameplay* g = context;
    if (type == COOP_SAVE_QUERY || type == COOP_SAVE_PAGE) return coop_runtime_save_message(sender, type, p, size);
    if (type == COOP_RECONNECT_VIEW || type == COOP_RECONNECT_ACK) return coop_runtime_reconnect_message(sender,type,p,size);
    if (coop_session_revision(g->session) != g->roster_revision)
        return type >= 16 && type <= COOP_GAME_QUIESCED;
    if (type == COOP_PRESENT_VIEW || type == COOP_PRESENT_ACK) return coop_runtime_present_message(sender, type, p, size);
    if (type >= COOP_CONTROL_REQUEST && type <= COOP_CONTROL_ACK) return coop_runtime_control_message(sender, type, p, size);
    if (size < 4 || read_u32(p) != g->epoch || g->failed) return false;
    if (type >= COOP_PLAY_EVENT && type <= COOP_PLAY_MAP_PING)
        return coop_play_network_message(g->session,g->epoch,sender,
            coop_gameplay_member_for_connection(g,sender),type,p,size);
    if (type == COOP_GAME_QUIESCE) {
        if (g->host || sender || size != 8 || !g->running) return false;
        uint32_t next = read_u32(p + 4);
        if (next && (g->epoch == UINT32_MAX || next != g->epoch + 1)) return false;
        if (g->quiesce_received) return g->next_epoch == next;
        g->next_epoch = next; g->finishing = g->quiesce_received = true;
        object_replica_pose_reset();
        fprintf(stderr, "ARCMP input quiesce epoch=%u next=%u\n", g->epoch, next);
        return true;
    }
    if (type == COOP_GAME_QUIESCED) {
        if (!g->host || !sender || sender >= COOP_MAX_PLAYERS || size != 8 || !g->finishing
            || !g->peers[sender].quiesce_sent || read_u32(p + 4) != g->next_epoch) return false;
        /* TCP delivers this only after that peer's earlier action frames. */
        if (!g->peers[sender].quiesced) fprintf(stderr, "ARCMP input quiesced sender=%u epoch=%u next=%u\n", sender, g->epoch, g->next_epoch);
        g->peers[sender].quiesced = true; return true;
    }
    if (type == COOP_COMBAT_REQUEST || type == COOP_COMBAT_RESULT) return g->running && coop_battle_message(g->battle, sender, type, p, size);
    if (type == COOP_MAGIC_REQUEST || type == COOP_MAGIC_RESULT || type == COOP_WRITTEN_VIEW || type == COOP_WRITTEN_ACK) return g->running && coop_magic_message(g->magic, sender, type, p, size);
    if (type >= COOP_SKILL_REQUEST && type <= COOP_SKILL_FEEDBACK_ACK) return g->running && coop_skills_message(g->skills, sender, type, p, size);
    if (type == COOP_PROGRESSION_REQUEST || type == COOP_PROGRESSION_RESULT) return g->running && coop_progression_message(g->progression, sender, type, p, size);
    if (type == COOP_SERVICE_REQUEST || type == COOP_SERVICE_RESULT) return g->running && coop_services_message(g->services, sender, type, p, size);
    if (type == COOP_FOLLOWER_REQUEST || type == COOP_FOLLOWER_RESULT) return g->running && coop_followers_message(g->followers, sender, type, p, size);
    if (type == COOP_NOTE_REQUEST || type == COOP_NOTE_RESULT) return g->running && coop_notes_message(g->notes, sender, type, p, size);
    if (type == COOP_AUDIO_BATCH || type == COOP_AUDIO_ACK) return g->running && coop_engine_audio_message(g->audio, sender, type, p, size);
    if (type >= COOP_ITEM_REQUEST && type <= COOP_LOOT_ACCESS) return g->running && coop_items_message(g->items, sender, type, p, size);
    if (type >= COOP_WORLD_BEGIN && type <= COOP_WORLD_ACK)
        return linked(g) && coop_world_net_message(g->world, sender, type, p, size);
    if (type >= COOP_DIALOGUE_BEGIN && type <= COOP_DIALOGUE_RESULT)
        return g->running && coop_dialogue_message(g->dialogue, sender, type, p, size);
    if (g->host) {
        if (!sender || sender >= COOP_MAX_PLAYERS || !g->peers[sender].present) return false;
        GameplayPeer* peer = &g->peers[sender];
        if (type == GAME_HELLO) {
            const CoopCampaignMember* member = coop_campaign_member((unsigned)peer->member);
            if (size != 20 || peer->hello || !member || memcmp(p + 4, member->object_id.d.g.data, 16)) return false;
            peer->hello = true;
            return true;
        }
        if (type == GAME_MOVE) return execute_move(g, sender, p, size);
        if (type == GAME_TALK) return execute_talk(g, sender, p, size);
        if (type == GAME_HALT) return execute_halt(g, sender, p, size);
        if (type == GAME_FINISHED && g->finishing && size == 8 && read_u32(p + 4) == g->revision && peer->finish_sent && !peer->finished) {
            peer->finished = true;
            return true;
        }
        return false;
    }
    if (sender != 0) return false;
    if (type == GAME_START && size == 8 && g->hello_sent && !g->running && coop_world_net_initialized(g->world)) {
        unsigned members = 0;
        for (unsigned slot = 0; slot < COOP_MAX_PLAYERS; slot++) {
            unsigned member = p[4 + slot];
            if (!g->peers[slot].present) { if (member != COOP_NO_SLOT) return false; continue; }
            if (member >= COOP_MAX_PLAYERS || (members & (1u << member)) || !coop_campaign_member(member)
                || !coop_campaign_member(member)->occupied || !coop_campaign_object(member)) return false;
            if (slot == coop_session_local_slot(g->session) && (int)member != g->peers[slot].member) return false;
            members |= 1u << member;
        }
        for (unsigned slot = 0; slot < COOP_MAX_PLAYERS; slot++) g->peers[slot].member = p[4 + slot] == COOP_NO_SLOT ? -1 : p[4 + slot];
        g->bindings_ready = true;
        g->running = true;
        return true;
    }
    if (type == GAME_RESULT && size == 9 && g->running && p[8] <= 1) {
        uint32_t sequence = read_u32(p + 4);
        if (sequence <= g->last_result || sequence > g->command_sequence) return false;
        g->last_result = sequence;
        g->last_accepted = p[8] != 0;
        return true;
    }
    if (type == GAME_POSES) return receive_poses(g, p, size);
    if (type == GAME_NPC_POSES) return receive_npc_poses(g, p, size);
    if (type == GAME_FINISH && size == 12 && g->running && g->quiesce_received && g->quiesce_ack_sent
        && !g->final_received && read_u32(p + 4) == g->received_revision) {
        uint32_t next_epoch = read_u32(p + 8);
        if (next_epoch && (g->epoch == UINT32_MAX || next_epoch != g->epoch + 1)) return false;
        if (g->next_epoch != next_epoch) return false;
        g->final_received = true;
        coop_engine_audio_seal(g->audio);
        return true;
    }
    if (type == GAME_STOP && size == 4) {
        fail(g, "The host stopped the gameplay session.");
        return true;
    }
    return false;
}

static bool quiesced(const CoopGameplay* g)
{
    for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++) if (g->peers[i].present && !g->peers[i].quiesced) return false;
    return true;
}
static bool actions_settled(const CoopGameplay* g)
{
    return coop_dialogue_settled(g->dialogue) && coop_items_settled(g->items) && coop_battle_settled(g->battle)
        && coop_magic_settled(g->magic) && coop_skills_settled(g->skills) && coop_progression_settled(g->progression)
        && coop_services_settled(g->services) && coop_followers_settled(g->followers)
        && coop_engine_audio_settled(g->audio) && coop_notes_settled(g->notes);
}
static bool message(void* context, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    CoopGameplay* g = context;
    if (g->retired) return true;
    coop_gameplay_retain(g);
    bool accepted = handle_message(context, sender, type, p, size);
    if (g->retired) accepted = true;
    coop_gameplay_release(g);
    return accepted;
}
static void gameplay_poll(CoopGameplay* g, uint64_t now)
{
    if (g->retired) return;
    if (now > g->now) g->now = now;
    if (now > g->action_real) {
        if (!g->paused || g->finishing) g->action_now += now - g->action_real;
        g->action_real = now;
    }
    if (!g->opened) g->opened = now;
    if (g->failed || g->finished) return;
    refresh_combat_presentation(g);
    if (!coop_sector_stream_poll(g->sectors)) {
        fail(g, "Unable to keep campaign regions loaded around the party.");
        return;
    }
    if (coop_session_phase(g->session) != COOP_LOBBY || map_current_map() != g->map
        || coop_session_revision(g->session) != g->roster_revision) {
        fail(g, "Gameplay connection or map changed; resynchronization is required.");
        return;
    }
    if (g->host && g->talker >= 0 && (!anim_is_current_goal_type(coop_campaign_object((unsigned)g->talker), AG_TALK, NULL)
        || coop_dialogue_current(g->dialogue)->active)) g->talker = -1;
    const CoopPlayer* players = coop_session_players(g->session);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (g->peers[i].present != players[i].connected
            || (g->host && players[i].connected && memcmp(players[i].identity, g->peers[i].owner, COOP_IDENTITY_SIZE))) {
            fail(g, "A campaign participant disconnected or changed; resynchronization is required.");
            return;
        }
    }
    if (!g->running && g->now - g->opened > (linked(g) ? 45000u : 15000u)) {
        fail(g, "Timed out establishing the gameplay session.");
        return;
    }
    if (g->host && g->finishing && g->now - g->finish_at > 35000) {
        fail(g, "Timed out acknowledging final character state.");
        return;
    }
    uint8_t p[POSES_SIZE] = { 0 };
    write_u32(p, g->epoch);
    if (g->host && g->finishing) {
        uint8_t stop[8]; write_u32(stop, g->epoch); write_u32(stop + 4, g->next_epoch);
        for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++) {
            GameplayPeer* peer = &g->peers[i];
            if (peer->present && peer->started && !peer->quiesce_sent)
                peer->quiesce_sent = coop_session_send(g->session, i, COOP_GAME_QUIESCE, stop, sizeof(stop));
        }
    }
    if (g->host && g->draining_native && coop_engine_use_settled()) {
        g->draining_native = false;
        coop_engine_audio_seal(g->audio);
    }
    /* Keep staged requests and native goals intact throughout a pause. Passing
     * accepting=false means cancellation to several controllers; pause instead
     * suspends their work clock while transport and world ACKs keep flowing. */
    bool action_work = g->running && (!g->paused || g->finishing);
    bool accepting = !g->finishing && !coop_runtime_entry_pending();
    bool free_action = accepting && !coop_dialogue_current(g->dialogue)->active;
    if (action_work && !coop_battle_poll(g->battle, g->action_now, free_action)) {
        fail(g, "Combat action acknowledgment timed out; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && !coop_magic_poll(g->magic, g->action_now, free_action)) {
        fail(g, "Spell, item-use or reading acknowledgment failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && !coop_items_poll(g->items, g->action_now, accepting)) {
        fail(g, "Item action acknowledgment timed out; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && !coop_skills_poll(g->skills, g->action_now, free_action)) {
        fail(g, "Skill action or feedback acknowledgment failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && !coop_progression_poll(g->progression, g->action_now, free_action)) {
        fail(g, "Character development acknowledgment failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && !coop_services_poll(g->services, g->action_now, accepting)) {
        fail(g, "NPC service acknowledgment failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && !coop_followers_poll(g->followers, g->action_now, free_action)) {
        fail(g, "Companion order acknowledgment failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && !coop_notes_poll(g->notes, g->action_now, free_action)) {
        fail(g, "Map-note acknowledgment failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    /* Keep captures running while clients stop submitting input and every
     * earlier request/native completion receives its final world receipt.
     * Sealing before that fence strands requests on an impossible capture. */
    if (g->host && g->finishing && !g->draining_native && quiesced(g) && actions_settled(g) && !g->world_sealing) {
        coop_world_net_settle(g->world); g->world_sealing = true;
    }
    if (linked(g) && !coop_world_net_poll(g->world, now)) {
        fail(g, "World object replication failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (g->running) spell_ui_coop_refresh();
    bool audio_ready = g->running;
    if (g->host) {
        for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++)
            audio_ready &= !g->peers[i].present || g->peers[i].started;
    }
    if (!coop_engine_audio_poll(g->audio, now, audio_ready)) {
        fail(g, "Sound presentation synchronization failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (action_work && (!coop_engine_dialogue_poll(g->running && !g->finishing) || coop_engine_dialogue_failed() || !coop_dialogue_poll(g->dialogue, g->action_now))) {
        fail(g, "Conversation synchronization failed; resynchronization is required.");
        return;
    }
    if (g->retired) return;
    if (!g->host) {
        if (g->finishing) {
            if (!g->quiesce_ack_sent) {
                uint8_t stop[8]; write_u32(stop, g->epoch); write_u32(stop + 4, g->next_epoch);
                g->quiesce_ack_sent = coop_session_send(g->session, 0, COOP_GAME_QUIESCED, stop, sizeof(stop));
            }
            if (!g->final_received) return;
            write_u32(p + 4, g->received_revision);
            if (coop_session_send(g->session, 0, GAME_FINISHED, p, 8)) {
                g->finished = true;
            }
            return;
        }
        if (g->running && g->now - (g->last_pose_at ? g->last_pose_at : g->opened) > 5000) {
            fail(g, "The host stopped publishing character state.");
            return;
        }
        if (!g->hello_sent) {
            int member = g->peers[coop_session_local_slot(g->session)].member;
            memcpy(p + 4, coop_campaign_member((unsigned)member)->object_id.d.g.data, 16);
            g->hello_sent = coop_session_send(g->session, 0, GAME_HELLO, p, 20);
        }
        return;
    }
    bool ready = true;
    for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++)
        ready &= !g->peers[i].present || g->peers[i].hello;
    if (!ready || !coop_world_net_initialized(g->world)) return;
    bool started = true;
    for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++) {
        GameplayPeer* peer = &g->peers[i];
        if (!peer->present) continue;
        if (!peer->started) {
            for (unsigned slot = 0; slot < COOP_MAX_PLAYERS; slot++) p[4 + slot] = g->peers[slot].present ? (uint8_t)g->peers[slot].member : COOP_NO_SLOT;
            peer->started = coop_session_send(g->session, i, GAME_START, p, 8);
        }
        started &= peer->started;
        if (peer->result) {
            write_u32(p + 4, peer->result);
            p[8] = peer->accepted;
            if (coop_session_send(g->session, i, GAME_RESULT, p, 9)) peer->result = 0;
        }
    }
    /* A peer that has received START may already submit a command while a
     * different peer's START waits for queue space. Its own started flag gates
     * that command; global queue backpressure must not revoke its authority. */
    g->running = ready;
    bool finalizing = g->finishing && !g->draining_native && g->world_sealing && quiesced(g)
        && coop_world_net_settled(g->world) && actions_settled(g);
    if (!started || g->now - g->last_sent < 50) return;
    if (!finalizing || !g->final_revision_started) g->revision++;
    if (finalizing) g->final_revision_started = true;
    write_u32(p + 4, g->revision);
    write_u32(p + 8, (uint32_t)g->map);
    p[12] = (uint8_t)coop_campaign_count();
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if (!member) continue;
        int64_t obj = coop_campaign_object(i);
        if (!obj) {
            fail(g, "An owned character no longer exists.");
            return;
        }
        uint8_t* pose = p + 13 + i * POSE_BYTES;
        memcpy(pose, member->object_id.d.g.data, 16);
        int64_t loc = obj_field_int64_get(obj, OBJ_F_LOCATION);
        write_u32(pose + 16, (uint32_t)location_get_x(loc));
        write_u32(pose + 20, (uint32_t)location_get_y(loc));
        write_u32(pose + 24, (uint32_t)obj_field_int32_get(obj, OBJ_F_OFFSET_X));
        write_u32(pose + 28, (uint32_t)obj_field_int32_get(obj, OBJ_F_OFFSET_Y));
        write_u32(pose + 32, (uint32_t)obj_field_int32_get(obj, OBJ_F_CURRENT_AID));
        write_u32(pose + 36, (uint32_t)obj_field_int32_get(obj, OBJ_F_FLAGS));
        write_u32(pose + 40, (uint32_t)obj_field_int32_get(obj, OBJ_F_HP_DAMAGE));
        write_u32(pose + 44, (uint32_t)obj_field_int32_get(obj, OBJ_F_CRITTER_FATIGUE_DAMAGE));
    }
    for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++)
        if (g->peers[i].present) {
            if (finalizing && g->peers[i].finish_sent) continue;
            /* On backpressure, the next tick publishes newer replaceable state. */
            if (finalizing && !g->peers[i].final_pose_sent)
                g->peers[i].final_pose_sent = coop_session_send(g->session, i, GAME_POSES, p, sizeof(p));
            if (finalizing && g->peers[i].final_pose_sent) {
                uint8_t finish[12];
                write_u32(finish, g->epoch);
                write_u32(finish + 4, g->revision);
                write_u32(finish + 8, g->next_epoch);
                g->peers[i].finish_sent = coop_session_send(g->session, i, GAME_FINISH, finish, sizeof(finish));
            } else if (!finalizing) {
                coop_session_send(g->session, i, GAME_POSES, p, sizeof(p));
            }
        }
    /* Owner poses/results enter the queue ahead of cosmetic NPC traffic. */
    if (!g->finishing && !g->paused) send_npc_poses(g);
    if (finalizing) {
        bool finished = true;
        for (unsigned i = 1; i < COOP_MAX_PLAYERS; i++)
            finished &= !g->peers[i].present || g->peers[i].finished;
        g->finished = finished;
    }
    g->last_sent = g->now;
}
void coop_gameplay_poll(CoopGameplay* g, uint64_t now)
{
    if (!g || g->retired) return;
    coop_gameplay_retain(g);
    if (g->running && !g->failed) {
        unsigned ready=0;
        for (unsigned i=1;i<4;i++) if (g->peers[i].present && g->peers[i].hello && g->peers[i].started) ready|=1u<<i;
        coop_play_network_tick(g->session,g->epoch,ready);
    }
    gameplay_poll(g, now);
    if (!g->retired && g->running && !g->failed) {
        unsigned ready=0;
        for (unsigned i=1;i<4;i++) if (g->peers[i].present && g->peers[i].hello && g->peers[i].started) ready|=1u<<i;
        coop_play_network_tick(g->session,g->epoch,ready);
    }
    coop_gameplay_release(g);
}

bool coop_gameplay_move(CoopGameplay* g, int64_t location, bool run)
{
    return coop_gameplay_move_route(g, &location, 1, run);
}
bool coop_gameplay_move_route(CoopGameplay* g, const int64_t* locations, unsigned count, bool run)
{
    uint8_t slot = coop_session_local_slot(g->session);
    if (slot >= COOP_MAX_PLAYERS || !g->running || g->failed || g->finishing) return false;
    int member = g->peers[slot].member;
    const CoopCampaignMember* pc = coop_campaign_member((unsigned)member);
    if (!pc) return false;
    return coop_gameplay_move_route_actor(g, pc->object_id.d.g.data, locations, count, run);
}
bool coop_gameplay_move_actor(CoopGameplay* g, const uint8_t actor[16], int64_t location, bool run)
{
    return coop_gameplay_move_route_actor(g, actor, &location, 1, run);
}
bool coop_gameplay_move_route_actor(CoopGameplay* g, const uint8_t actor[16], const int64_t* locations, unsigned count, bool run)
{
    if (!g->running || g->failed || g->finishing || !actor || !locations || !count || count > COOP_MOVE_WAYPOINTS || g->command_sequence == UINT32_MAX) return false;
    uint8_t p[42 + (COOP_MOVE_WAYPOINTS - 1) * 8] = { 0 };
    size_t size = count == 1 ? 41u : 42u + (count - 1) * 8u;
    write_u32(p, g->epoch);
    write_u32(p + 4, g->command_sequence + 1);
    write_u32(p + 8, (uint32_t)g->map);
    memcpy(p + 12, actor, 16);
    write_u32(p + 28, (uint32_t)location_get_x(locations[0]));
    write_u32(p + 32, (uint32_t)location_get_y(locations[0]));
    int64_t local_pc = player_get_local_pc_obj();
    p[36] = run || (local_pc && get_always_run(local_pc) && critter_encumbrance_level_get(local_pc) < ENCUMBRANCE_LEVEL_SIGNIFICANT);
    write_u32(p + 37, combat_turn_generation_get());
    if (count > 1) p[41] = (uint8_t)count;
    for (unsigned i = 1; i < count; i++) {
        write_u32(p + 42 + (i - 1) * 8, (uint32_t)location_get_x(locations[i]));
        write_u32(p + 46 + (i - 1) * 8, (uint32_t)location_get_y(locations[i]));
    }
    if (g->host ? !execute_move(g, 0, p, size) : !coop_session_send(g->session, 0, GAME_MOVE, p, size)) return false;
    g->command_sequence++;
    return true;
}
bool coop_gameplay_halt(CoopGameplay* g, int64_t towards)
{
    uint8_t slot = coop_session_local_slot(g->session);
    if (slot >= COOP_MAX_PLAYERS || !g->running || g->failed || g->finishing || g->command_sequence == UINT32_MAX) return false;
    const CoopCampaignMember* member = coop_campaign_member((unsigned)g->peers[slot].member);
    if (!member) return false;
    uint8_t p[40];
    write_u32(p, g->epoch);
    write_u32(p + 4, g->command_sequence + 1);
    write_u32(p + 8, (uint32_t)g->map);
    memcpy(p + 12, member->object_id.d.g.data, 16);
    write_u32(p + 28, (uint32_t)location_get_x(towards));
    write_u32(p + 32, (uint32_t)location_get_y(towards));
    write_u32(p + 36, combat_turn_generation_get());
    if (g->host ? !execute_halt(g, 0, p, sizeof(p)) : !coop_session_send(g->session, 0, GAME_HALT, p, sizeof(p))) return false;
    g->command_sequence++;
    return true;
}
bool coop_gameplay_running(const CoopGameplay* g) { return g->running && !g->failed; }
int coop_gameplay_member_for_connection(const CoopGameplay* g, unsigned connection)
{
    return g && !g->retired && g->bindings_ready && connection < COOP_MAX_PLAYERS && g->peers[connection].present
        ? g->peers[connection].member : -1;
}
bool coop_gameplay_talk(CoopGameplay* g, int64_t npc)
{
    uint8_t slot = coop_session_local_slot(g->session);
    if (slot >= COOP_MAX_PLAYERS || !g->running || g->failed || g->finishing || !npc || !obj_handle_is_valid(npc)) return false;
    const CoopCampaignMember* member = coop_campaign_member((unsigned)g->peers[slot].member);
    CoopObjectId target;
    return member && coop_object_id_from_engine(obj_get_id(npc), &target)
        && coop_gameplay_talk_actor(g, member->object_id.d.g.data, target);
}
bool coop_gameplay_talk_actor(CoopGameplay* g, const uint8_t actor[16], CoopObjectId npc)
{
    if (!g->running || g->failed || g->finishing || !actor || g->command_sequence == UINT32_MAX) return false;
    uint8_t p[49];
    write_u32(p, g->epoch);
    write_u32(p + 4, g->command_sequence + 1);
    write_u32(p + 8, (uint32_t)g->map);
    memcpy(p + 12, actor, 16);
    memcpy(p + 28, npc.bytes, sizeof(npc.bytes));
    write_u32(p + 45, combat_turn_generation_get());
    if (g->host ? !execute_talk(g, 0, p, sizeof(p)) : !coop_session_send(g->session, 0, GAME_TALK, p, sizeof(p))) return false;
    g->command_sequence++;
    return true;
}
CoopBattle* coop_gameplay_battle(CoopGameplay* g) { return g->battle; }
bool coop_gameplay_combat(CoopGameplay* g, CoopCombatOperation operation, uint8_t value, int64_t target)
{
    uint8_t slot = coop_session_local_slot(g->session);
    if (slot >= COOP_MAX_PLAYERS) return false;
    const CoopCampaignMember* member = coop_campaign_member((unsigned)g->peers[slot].member);
    CoopObjectId id = {0};
    if (target && (!obj_handle_is_valid(target) || !coop_object_id_from_engine(obj_get_id(target), &id))) return false;
    return member && coop_gameplay_combat_actor(g, member->object_id.d.g.data, operation, value, id, combat_turn_generation_get());
}
bool coop_gameplay_combat_actor(CoopGameplay* g, const uint8_t actor[16], CoopCombatOperation operation, uint8_t value, CoopObjectId target, uint32_t generation)
{
    return g->running && !g->failed && !g->finishing && coop_battle_submit(g->battle, g->map, actor, operation, value, target, generation);
}
bool coop_gameplay_failed(const CoopGameplay* g) { return g->failed; }
CoopMagic* coop_gameplay_magic(CoopGameplay* g) { return g->magic; }
CoopSkills* coop_gameplay_skills(CoopGameplay* g) { return g->skills; }
CoopProgression* coop_gameplay_progression(CoopGameplay* g) { return g->progression; }
CoopServices* coop_gameplay_services(CoopGameplay* g) { return g->services; }
bool coop_gameplay_skill_request(CoopGameplay* g, const CoopSkillRequest* request)
{
    return g && g->running && !g->failed && !g->finishing && coop_skills_submit(g->skills, request);
}
bool coop_gameplay_skill(CoopGameplay* g, CoopSkillAction action, uint8_t value, int64_t target)
{
    if (!g) return false;
    uint8_t slot = coop_session_local_slot(g->session);
    if (slot >= COOP_MAX_PLAYERS) return false;
    const CoopCampaignMember* member = coop_campaign_member((unsigned)g->peers[slot].member);
    if (!member) return false;
    CoopSkillRequest r = { .map = (uint32_t)g->map, .action = action, .value = value, .generation = combat_turn_generation_get() };
    memcpy(r.actor, member->object_id.d.g.data, 16);
    if (target && (!obj_handle_is_valid(target) || !coop_object_id_from_engine(obj_get_id(target), &r.target))) return false;
    return coop_gameplay_skill_request(g, &r);
}
bool coop_gameplay_skill_feedback(CoopGameplay* g, int64_t actor, unsigned message, bool red, unsigned catalogue)
{
    int member = coop_campaign_slot_for_object(actor);
    if (!g || !g->host || !g->running || g->failed || member < 0) return false;
    for (unsigned slot = 0; slot < COOP_MAX_PLAYERS; slot++)
        if (g->peers[slot].member == member && g->peers[slot].present)
            return coop_skills_feedback(g->skills, (uint8_t)slot, message, red, catalogue);
    return false;
}
CoopEngineAudio* coop_gameplay_audio(CoopGameplay* g) { return g->audio; }
bool coop_gameplay_magic_request(CoopGameplay* g, const CoopMagicRequest* request)
{
    return g && g->running && !g->failed && !g->finishing && coop_magic_submit(g->magic, request);
}
static bool magic_actor(CoopGameplay* g, CoopMagicRequest* request)
{
    uint8_t slot = coop_session_local_slot(g->session);
    if (slot >= COOP_MAX_PLAYERS) return false;
    const CoopCampaignMember* member = coop_campaign_member((unsigned)g->peers[slot].member);
    if (!member) return false;
    request->map = (uint32_t)g->map;
    request->generation = combat_turn_generation_get();
    memcpy(request->actor, member->object_id.d.g.data, 16);
    return true;
}
bool coop_gameplay_cast(CoopGameplay* g, int64_t source, int spell, const TargetDescriptor* target, bool force)
{
    if (!g || !target || !source || !obj_handle_is_valid(source) || spell < 0 || spell >= COOP_MAGIC_SPELL_COUNT) return false;
    CoopMagicRequest r = { .operation = COOP_MAGIC_CAST, .spell = (uint16_t)spell, .flags = force ? COOP_MAGIC_FORCE : 0 };
    if (!magic_actor(g, &r) || !coop_object_id_from_engine(obj_get_id(source), &r.source)) return false;
    if (target->is_loc) {
        r.flags |= COOP_MAGIC_LOCATION;
        r.x = (uint32_t)location_get_x(target->loc);
        r.y = (uint32_t)location_get_y(target->loc);
    } else if (!target->obj || !obj_handle_is_valid(target->obj) || !coop_object_id_from_engine(obj_get_id(target->obj), &r.target)) return false;
    return coop_gameplay_magic_request(g, &r);
}
bool coop_gameplay_cancel_spell(CoopGameplay* g, uint64_t token)
{
    CoopMagicRequest r = { .operation = COOP_MAGIC_CANCEL, .token = token };
    return g && magic_actor(g, &r) && coop_gameplay_magic_request(g, &r);
}
bool coop_gameplay_use_item(CoopGameplay* g, int64_t item, const TargetDescriptor* td, bool force)
{
    if (!g || !item || !obj_handle_is_valid(item)) return false;
    CoopMagicRequest r = { .operation = COOP_MAGIC_USE_ITEM, .flags = force ? COOP_MAGIC_FORCE : 0 };
    if (td && !td->is_loc && td->obj && tig_kb_get_modifier(SDL_KMOD_CTRL)) r.flags |= COOP_MAGIC_OWN_SKILL;
    if (!magic_actor(g, &r) || !coop_object_id_from_engine(obj_get_id(item), &r.source)) return false;
    if (!td) {
        if (force) return false;
    } else if (td->is_loc) {
        r.flags |= COOP_MAGIC_LOCATION;
        r.x = (uint32_t)location_get_x(td->loc);
        r.y = (uint32_t)location_get_y(td->loc);
    } else if (!td->obj || !obj_handle_is_valid(td->obj) || !coop_object_id_from_engine(obj_get_id(td->obj), &r.target)) return false;
    return coop_gameplay_magic_request(g, &r);
}
bool coop_gameplay_written(CoopGameplay* g, int64_t reader, unsigned type, int number, bool vendigroth)
{
    int member = coop_campaign_slot_for_object(reader);
    if (!g || !g->host || !g->running || g->failed || member < 0) return false;
    for (unsigned slot = 0; slot < COOP_MAX_PLAYERS; slot++)
        if (g->peers[slot].member == member && g->peers[slot].present) {
            if (!coop_engine_documents_record(type, number, vendigroth)) return fail(g, "Cannot retain the party document");
            return coop_magic_written(g->magic, (uint8_t)slot, type, number, vendigroth);
        }
    return false;
}
const char* coop_gameplay_error(const CoopGameplay* g) { return g->error; }
uint32_t coop_gameplay_last_result(const CoopGameplay* g, bool* accepted)
{
    *accepted = g->last_accepted;
    return g->last_result;
}
unsigned coop_gameplay_accepted(const CoopGameplay* g) { return g->accepted; }
unsigned coop_gameplay_rejected(const CoopGameplay* g) { return g->rejected; }
bool coop_gameplay_finish(CoopGameplay* g)
{
    if (!g->host || !g->running || g->failed || g->finishing) return false;
    /* A queued retail encounter owns its native continuation. Drain it before
     * saving/resetting the map instead of silently discarding that script. */
    if (coop_engine_dialogue_pending()) return false;
    if (g->talker >= 0) {
        AnimRunInfo* run;
        if (anim_id_to_run_info(&g->talk_goal, &run)) {
            int64_t actor = obj_pool_perm_lookup(g->talk_actor);
            if (actor && !sub_424070(actor, PRIORITY_3, false, true)) return false;
        }
        g->talker = -1;
    }
    g->finishing = true;
    g->finish_at = g->now;
    g->draining_native = !coop_engine_use_settled();
    if (!g->draining_native) {
        coop_engine_audio_seal(g->audio);
    }
    return true;
}
bool coop_gameplay_finished(const CoopGameplay* g) { return g->finished; }
void coop_gameplay_set_paused(CoopGameplay* g, bool paused) { if (g) g->paused = paused; }
bool coop_gameplay_finishing(const CoopGameplay* g) { return g->finishing; }
bool coop_gameplay_draining_native(const CoopGameplay* g) { return g->draining_native; }
bool coop_gameplay_transition(CoopGameplay* g, uint32_t next_epoch)
{
    if (g->epoch == UINT32_MAX || next_epoch != g->epoch + 1 || !coop_gameplay_finish(g)) return false;
    g->next_epoch = next_epoch;
    return true;
}
uint32_t coop_gameplay_transition_epoch(const CoopGameplay* g) { return g->next_epoch; }
uint32_t coop_gameplay_world_revision(const CoopGameplay* g) { return coop_world_net_revision(g->world); }
uint64_t coop_gameplay_world_bytes(const CoopGameplay* g) { return coop_world_net_bytes(g->world); }
uint64_t coop_gameplay_world_deletions(const CoopGameplay* g) { return coop_world_net_deletions(g->world); }
CoopDialogue* coop_gameplay_dialogue(CoopGameplay* g) { return g->dialogue; }
CoopItems* coop_gameplay_items(CoopGameplay* g) { return g->items; }
bool coop_gameplay_item_actor(CoopGameplay* g, const uint8_t actor[16], CoopItemOperation op, CoopObjectId item, uint8_t recipient, int location)
{
    return g->running && !g->failed && !g->finishing && coop_items_submit(g->items, g->map, actor, op, item, recipient, location);
}
bool coop_gameplay_item(CoopGameplay* g, CoopItemOperation op, int64_t item, uint8_t recipient, int location)
{
    unsigned slot = coop_session_local_slot(g->session);
    if (slot >= COOP_MAX_PLAYERS) return false;
    const CoopCampaignMember* member = coop_campaign_member((unsigned)g->peers[slot].member);
    CoopObjectId id = { 0 };
    return member && (!item || coop_object_id_from_engine(obj_get_id(item), &id))
        && coop_gameplay_item_actor(g, member->object_id.d.g.data, op, id, recipient, location);
}

CoopFollowers* coop_gameplay_followers(CoopGameplay* g) { return g ? g->followers : NULL; }
CoopNotes* coop_gameplay_notes(CoopGameplay* g) { return g ? g->notes : NULL; }
