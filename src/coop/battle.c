#include "coop/battle.h"
#include "coop/engine_object.h"
#include "coop/engine_options.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "ui/intgame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { RESULT_BYTES = 16,
    STAGED = 1,
    CAPTURING,
    REPLY,
    SENT };
typedef struct BattlePeer {
    uint8_t bytes[COOP_COMBAT_REQUEST_BYTES];
    CoopCombatRequest request;
    unsigned phase;
    unsigned status;
    unsigned count;
    uint64_t window;
    uint64_t capture;
    uint32_t revision;
} BattlePeer;
struct CoopBattle {
    CoopSession* session;
    CoopWorldNetwork* world;
    uint32_t epoch;
    bool host;
    CoopCombatActor actor;
    void* context;
    BattlePeer peers[COOP_MAX_PLAYERS];
    uint64_t now;
    uint64_t submitted_at;
    uint32_t submitted;
    uint32_t last_result;
    uint32_t waiting_revision;
    unsigned last_status;
    bool pending;
    bool received;
    unsigned executed;
    unsigned rejected;
    unsigned continued;
};
CoopBattle* coop_battle_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, uint64_t now, CoopCombatActor actor, void* context)
{
    if (!session || !world || !epoch || !actor) return NULL;
    CoopBattle* b = calloc(1, sizeof(*b));
    if (b) {
        b->session = session;
        b->world = world;
        b->epoch = epoch;
        b->now = now;
        b->actor = actor;
        b->context = context;
        b->host = coop_session_is_host(session);
    }
    return b;
}
void coop_battle_destroy(CoopBattle* b) { free(b); }
bool coop_battle_message(CoopBattle* b, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (!b || !p || size < 8 || coop_object_read32(p) != b->epoch) return false;
    if (b->host) {
        CoopCombatRequest r;
        if (type != COOP_COMBAT_REQUEST || sender >= COOP_MAX_PLAYERS || !coop_session_players(b->session)[sender].connected
            || !coop_combat_request_decode(p, size, &r)) return false;
        BattlePeer* peer = &b->peers[sender];
        if (peer->request.sequence == r.sequence) {
            if (memcmp(peer->bytes, p, size)) return false;
            if (peer->phase == SENT) peer->phase = REPLY;
            return true;
        }
        if (peer->request.sequence == UINT32_MAX || r.sequence != peer->request.sequence + 1
            || (peer->phase && peer->phase != SENT)) return false;
        if (b->now - peer->window >= 1000) {
            peer->window = b->now;
            peer->count = 0;
        }
        if (++peer->count > 30) return false;
        peer->request = r;
        memcpy(peer->bytes, p, size);
        peer->phase = STAGED;
        return true;
    }
    uint32_t sequence = coop_object_read32(p + 4);
    if (sender || type != COOP_COMBAT_RESULT || size != RESULT_BYTES || !sequence || sequence != b->submitted) return false;
    unsigned status = coop_object_read32(p + 8);
    uint32_t revision = coop_object_read32(p + 12);
    if (status > COOP_COMBAT_HOST_ONLY) return false;
    if (b->received) return status == b->last_status && revision == b->waiting_revision;
    if (!b->pending) return false;
    b->last_status = status;
    b->waiting_revision = revision;
    b->received = true;
    return true;
}
static bool actor_idle(int64_t actor)
{
    AnimID current, fidget;
    return !sub_423300(actor, &current) || (anim_is_current_goal_type(actor, AG_ANIM_FIDGET, &fidget) && anim_id_is_equal(&current, &fidget));
}
static int64_t target_object(const CoopCombatRequest* r)
{
    if (r->target.bytes[0] == 3) {
        int64_t max_x, max_y;
        location_limits_get(&max_x, &max_y);
        if (coop_object_read32(r->target.bytes + 1) >= (uint64_t)max_x || coop_object_read32(r->target.bytes + 5) >= (uint64_t)max_y
            || coop_object_read32(r->target.bytes + 13) != r->map) return OBJ_HANDLE_NULL;
    }
    int64_t target = obj_pool_perm_lookup(coop_object_id_to_engine(r->target));
    return target && !(obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT | OF_INVENTORY)) ? target : OBJ_HANDLE_NULL;
}
static unsigned execute(CoopBattle* b, unsigned sender, BattlePeer* peer)
{
    const CoopCombatRequest* r = &peer->request;
    int64_t pc = b->actor(b->context, (uint8_t)sender, peer->bytes);
    if (!pc) return COOP_COMBAT_OWNERSHIP;
    /* Session administration belongs to the host, including while dead or
     * off-turn. A changing NPC turn must not invalidate these switches. */
    if (r->operation == COOP_COMBAT_IMMORTAL) {
        if (sender) return COOP_COMBAT_HOST_ONLY;
        return coop_engine_options_immortal_set(r->value >> 1, (r->value & 1) != 0) ? COOP_COMBAT_OK : COOP_COMBAT_REFUSED;
    }
    if (r->operation == COOP_COMBAT_POLICY) {
        if (sender) return COOP_COMBAT_HOST_ONLY;
        if (!sub_4B6C90(r->value != 0) || combat_is_turn_based() != (r->value != 0)) return COOP_COMBAT_REFUSED;
        combat_campaign_fast_set(r->value == 2);
        return COOP_COMBAT_OK;
    }
    if (r->generation != combat_turn_generation_get()) return COOP_COMBAT_STALE;
    if (r->operation == COOP_COMBAT_STANCE) {
        bool active = combat_critter_is_combat_mode_active(pc);
        if (active == (r->value != 0)) return COOP_COMBAT_OK;
        if (!r->value) {
            if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != pc) return COOP_COMBAT_TURN;
            if (!actor_idle(pc)) return COOP_COMBAT_BUSY;
            if (!combat_can_exit_combat_mode(pc)) return COOP_COMBAT_REFUSED;
            combat_critter_deactivate_combat_mode(pc);
        } else
            combat_critter_activate_combat_mode(pc);
        return combat_critter_is_combat_mode_active(pc) == (r->value != 0) ? COOP_COMBAT_OK : COOP_COMBAT_REFUSED;
    }
    if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != pc) return COOP_COMBAT_TURN;
    if (r->operation == COOP_COMBAT_END_TURN) {
        if (!combat_turn_based_is_active()) return COOP_COMBAT_TURN;
        if (!actor_idle(pc)) return COOP_COMBAT_BUSY;
        combat_turn_based_end_critter_turn(pc);
        return COOP_COMBAT_OK;
    }
    int64_t target = target_object(r);
    if (!target || target == pc) return COOP_COMBAT_TARGET;
    bool forced = (r->value & COOP_ATTACK_FORCE) != 0;
    int hit_loc = (r->value & COOP_ATTACK_AIM_MASK) >> COOP_ATTACK_AIM_SHIFT;
    static const AnimAttackMode modes[] = { ANIM_ATTACK_DEFAULT, ANIM_ATTACK_ONCE, ANIM_ATTACK_REPEAT, ANIM_ATTACK_STAND };
    AnimAttackMode mode = modes[(r->value & COOP_ATTACK_MODE_MASK) >> COOP_ATTACK_MODE_SHIFT];
    int type = obj_field_int32_get(target, OBJ_F_TYPE);
    if (obj_type_is_critter(type)) {
        if (critter_is_dead(target) || (!forced && critter_party_same(pc, target))) return COOP_COMBAT_TARGET;
    } else if (!forced || (type != OBJ_TYPE_CONTAINER && type != OBJ_TYPE_SCENERY && type != OBJ_TYPE_PORTAL && type != OBJ_TYPE_WALL))
        return COOP_COMBAT_TARGET;
    int range = 30;
    int64_t weapon = item_wield_get(pc, ITEM_INV_LOC_WEAPON);
    if (weapon) {
        int native_range = item_weapon_range(weapon, pc);
        if (native_range > range) range = native_range;
    }
    if (object_dist(pc, target) > range) return COOP_COMBAT_DISTANCE;
    combat_critter_activate_combat_mode(pc);
    if (!critter_is_active(pc) || !combat_critter_is_combat_mode_active(pc)) return COOP_COMBAT_UNAVAILABLE;
    if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != pc) return COOP_COMBAT_TURN;
    if (anim_goal_attack_options_pending(pc, target, hit_loc, mode)) {
        b->continued++;
        return COOP_COMBAT_OK;
    }
    return combat_check_attack(pc, target) && anim_goal_attack_with_options(pc, target, hit_loc, mode) ? COOP_COMBAT_OK : COOP_COMBAT_REFUSED;
}
static void present_result(CoopBattle* b)
{
    if (!b->pending || !b->received || coop_world_net_applied_revision(b->world) < b->waiting_revision) return;
    b->pending = false;
    b->last_result = b->submitted;
    static const char* errors[] = { "", "Combat controls are unavailable.", "You cannot control that character.",
        "That target is no longer available.", "That target is too far away.", "It is another character's turn.",
        "The turn changed. Try again.", "Finish your current action before ending the turn.",
        "That combat action cannot be performed.", "Only the host can change combat timing or immortality." };
    if (b->last_status)
        intgame_message_window_display_str(-1, (char*)errors[b->last_status]);
    /* Successful actions must not erase a newly selected spell/skill preview. */
}
bool coop_battle_poll(CoopBattle* b, uint64_t now, bool accepting)
{
    if (coop_world_net_retired(b->world)) return true;
    if (now > b->now) b->now = now;
    if (b->pending && b->now - b->submitted_at > 30000) return false;
    if (b->host)
        for (unsigned sender = 0; sender < COOP_MAX_PLAYERS; sender++) {
            BattlePeer* peer = &b->peers[sender];
            if (peer->phase == STAGED) {
                peer->status = accepting ? execute(b, sender, peer) : COOP_COMBAT_UNAVAILABLE;
                if (coop_world_net_retired(b->world)) return true;
                if (peer->status)
                    b->rejected++;
                else
                    b->executed++;
                peer->capture = coop_world_net_request_capture(b->world);
                peer->phase = CAPTURING;
                fprintf(stderr, "ARCMP combat action sender=%u sequence=%u operation=%u value=%u status=%u\n", sender, peer->request.sequence, peer->request.operation, peer->request.value, peer->status);
            }
            if (peer->phase == CAPTURING && coop_world_net_captures(b->world) >= peer->capture) {
                peer->revision = coop_world_net_revision(b->world);
                peer->phase = REPLY;
            }
            if (peer->phase == REPLY) {
                uint8_t p[RESULT_BYTES];
                coop_object_write32(p, b->epoch);
                coop_object_write32(p + 4, peer->request.sequence);
                coop_object_write32(p + 8, peer->status);
                coop_object_write32(p + 12, peer->revision);
                if (!sender) {
                    b->last_status = peer->status;
                    b->waiting_revision = peer->revision;
                    b->received = true;
                    peer->phase = SENT;
                } else if (coop_session_send(b->session, (uint8_t)sender, COOP_COMBAT_RESULT, p, sizeof(p)))
                    peer->phase = SENT;
            }
        }
    present_result(b);
    return true;
}
bool coop_battle_submit(CoopBattle* b, int map, const uint8_t actor[16], CoopCombatOperation operation, uint8_t value, CoopObjectId target, uint32_t generation)
{
    if (!b || b->pending || b->submitted == UINT32_MAX || !actor) return false;
    CoopCombatRequest r = { .epoch = b->epoch, .sequence = b->submitted + 1, .map = (uint32_t)map, .operation = operation, .value = value, .target = target, .generation = generation };
    memcpy(r.actor, actor, sizeof(r.actor));
    uint8_t p[COOP_COMBAT_REQUEST_BYTES];
    if (!coop_combat_request_encode(&r, p)) return false;
    uint8_t slot = coop_session_local_slot(b->session);
    if (!(b->host ? coop_battle_message(b, slot, COOP_COMBAT_REQUEST, p, sizeof(p)) : coop_session_send(b->session, 0, COOP_COMBAT_REQUEST, p, sizeof(p)))) return false;
    b->submitted++;
    b->pending = true;
    b->received = false;
    b->submitted_at = b->now;
    return true;
}
bool coop_battle_busy(const CoopBattle* b) { return b->pending; }
bool coop_battle_settled(const CoopBattle* b)
{
    if (b->pending) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if (b->peers[i].phase && b->peers[i].phase != SENT) return false;
    return true;
}
uint32_t coop_battle_last_result(const CoopBattle* b, unsigned* status)
{
    *status = b->last_status;
    return b->last_result;
}
unsigned coop_battle_executed(const CoopBattle* b) { return b->executed; }
unsigned coop_battle_rejected(const CoopBattle* b) { return b->rejected; }

unsigned coop_battle_continued(const CoopBattle* b) { return b->continued; }
