#include "coop/engine_progression.h"
#include "coop/authority.h"
#include "game/critter.h"
#include "game/fate.h"
#include "game/obj.h"
#include "game/level.h"
#include "game/object.h"
#include "game/player.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/tech.h"
#include <limits.h>
CoopProgressionStatus coop_engine_progression_fate(int64_t actor, const CoopProgressionRequest* request)
{
    if (!actor || !request || !coop_authority_can_simulate() || !player_is_pc_obj(actor) || critter_is_dead(actor)
        || (obj_field_int32_get(actor, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return COOP_PROGRESS_UNAVAILABLE;
    if (request->operation != COOP_PROGRESS_FATE || request->index >= FATE_COUNT || (request->direction != 1 && request->direction != 2)) return COOP_PROGRESS_REQUIREMENTS;
    int points = stat_base_get(actor, STAT_FATE_POINTS);
    if (points < 0 || request->points != (uint32_t)points || request->expected != (uint32_t)obj_field_int32_get(actor, OBJ_F_PC_FLAGS_FATE)) return COOP_PROGRESS_CHANGED;
    if (request->direction == 1) {
        if (!points) return COOP_PROGRESS_POINTS;
        return fate_activate(actor, request->index) ? COOP_PROGRESS_OK : COOP_PROGRESS_CHANGED;
    }
    /* A fate consumed by another action must never refund its spent point. */
    if (!request->index || points == INT32_MAX) return COOP_PROGRESS_LIMIT;
    return fate_deactivate(actor, request->index) ? COOP_PROGRESS_OK : COOP_PROGRESS_CHANGED;
}

static unsigned offset(CoopProgressionKind kind, unsigned index)
{
    static const unsigned offsets[] = {0, 8, 9, 10, 26, 42};
    return offsets[kind] + index;
}
int coop_engine_progression_value(int64_t actor, CoopProgressionKind kind, unsigned index)
{
    if (!actor || !coop_progression_selection_valid(kind, index)) return -1;
    switch (kind) {
    case COOP_PROGRESS_STAT: return stat_base_get(actor, (int)index);
    case COOP_PROGRESS_HP: return object_hp_pts_get(actor);
    case COOP_PROGRESS_FATIGUE: return critter_fatigue_pts_get(actor);
    case COOP_PROGRESS_SKILL: return index < BASIC_SKILL_COUNT ? basic_skill_points_get(actor, (int)index) : tech_skill_points_get(actor, (int)index - BASIC_SKILL_COUNT);
    case COOP_PROGRESS_COLLEGE: return spell_college_level_get(actor, (int)index);
    case COOP_PROGRESS_DISCIPLINE: return tech_degree_get(actor, (int)index);
    case COOP_PROGRESS_SCHEME: return auto_level_scheme_get(actor);
    default: return -1;
    }
}
bool coop_engine_progression_open(int64_t actor, CoopProgressionBaseline* baseline)
{
    if (!baseline || !actor || !coop_authority_can_simulate() || !player_is_pc_obj(actor) || !critter_is_active(actor)) return false;
    for (unsigned kind = 0; kind <= COOP_PROGRESS_DISCIPLINE; kind++)
        for (unsigned index = 0; coop_progression_selection_valid((CoopProgressionKind)kind, index); index++) {
            int value = coop_engine_progression_value(actor, (CoopProgressionKind)kind, index);
            if (value < 0) return false;
            baseline->values[offset((CoopProgressionKind)kind, index)] = value;
        }
    return true;
}
CoopProgressionStatus coop_engine_progression_edit(int64_t actor, const CoopProgressionBaseline* baseline, const CoopProgressionRequest* r)
{
    if (!actor || !baseline || !r || !coop_authority_can_simulate() || !player_is_pc_obj(actor) || !critter_is_active(actor)) return COOP_PROGRESS_UNAVAILABLE;
    if (r->operation != COOP_PROGRESS_EDIT || !coop_progression_selection_valid(r->kind, r->index) || (r->direction != 1 && r->direction != 2)) return COOP_PROGRESS_REQUIREMENTS;
    int current = coop_engine_progression_value(actor, r->kind, r->index), points = stat_level_get(actor, STAT_UNSPENT_POINTS);
    if (current < 0 || points < 0 || (uint32_t)current != r->expected || (uint32_t)points != r->points) return COOP_PROGRESS_CHANGED;
    if (r->kind == COOP_PROGRESS_SCHEME) {
        if (r->direction != 1 || !auto_level_scheme_name(r->index) || (r->index && !auto_level_scheme_rule(r->index))) return COOP_PROGRESS_REQUIREMENTS;
        return auto_level_scheme_set(actor, r->index) == r->index ? COOP_PROGRESS_OK : COOP_PROGRESS_REQUIREMENTS;
    }
    bool increase = r->direction == 1;
    int floor = baseline->values[offset(r->kind, r->index)], cost = 1, next = current;
    if (!increase && current <= floor) return COOP_PROGRESS_BASELINE;
    if (increase && current == INT32_MAX) return COOP_PROGRESS_LIMIT;
    switch (r->kind) {
    case COOP_PROGRESS_STAT:
        if (increase && stat_level_get(actor, r->index) >= stat_level_max(actor, r->index)) return COOP_PROGRESS_LIMIT;
        cost = stat_cost(current + (increase ? 1 : 0)); next += increase ? 1 : -1;
        break;
    case COOP_PROGRESS_HP: case COOP_PROGRESS_FATIGUE:
        next += increase ? 1 : -1; break;
    case COOP_PROGRESS_SKILL:
        cost = r->index < BASIC_SKILL_COUNT
            ? (increase ? basic_skill_cost_inc(actor, r->index) : basic_skill_cost_dec(actor, r->index))
            : (increase ? tech_skill_cost_inc(actor, r->index - BASIC_SKILL_COUNT) : tech_skill_cost_dec(actor, r->index - BASIC_SKILL_COUNT));
        if (cost <= 0 || cost > INT32_MAX - (increase ? current : 0)) return COOP_PROGRESS_LIMIT;
        next += increase ? cost : -cost;
        if (next < 0 || (!increase && next < floor)) return COOP_PROGRESS_BASELINE;
        break;
    case COOP_PROGRESS_COLLEGE:
        if (increase && current >= 5) return COOP_PROGRESS_LIMIT;
        if (!increase && current <= 0) return COOP_PROGRESS_BASELINE;
        cost = spell_cost(r->index * 5 + current - (increase ? 0 : 1)); next += increase ? 1 : -1;
        break;
    case COOP_PROGRESS_DISCIPLINE:
        if (increase && current >= DEGREE_COUNT - 1) return COOP_PROGRESS_LIMIT;
        if (!increase && current <= 0) return COOP_PROGRESS_BASELINE;
        cost = tech_degree_cost_get(current + (increase ? 1 : 0)); next += increase ? 1 : -1;
        break;
    default: return COOP_PROGRESS_REQUIREMENTS;
    }
    if (cost <= 0 || (increase ? points < cost : points > INT32_MAX - cost)) return COOP_PROGRESS_POINTS;
    int balance = points + (increase ? -cost : cost);
    if (balance > stat_level_max(actor, STAT_UNSPENT_POINTS)) return COOP_PROGRESS_LIMIT;
    int old_skill_level = r->kind == COOP_PROGRESS_SKILL && r->index < BASIC_SKILL_COUNT ? basic_skill_level(actor, r->index) : -1;
    bool changed = false;
    switch (r->kind) {
    case COOP_PROGRESS_STAT: changed = stat_base_set(actor, r->index, next) == next; break;
    case COOP_PROGRESS_HP: changed = object_hp_pts_set(actor, next) == next; break;
    case COOP_PROGRESS_FATIGUE: changed = critter_fatigue_pts_set(actor, next) == next; break;
    case COOP_PROGRESS_SKILL:
        changed = (r->index < BASIC_SKILL_COUNT ? basic_skill_points_set(actor, r->index, next) : tech_skill_points_set(actor, r->index - BASIC_SKILL_COUNT, next)) == next;
        break;
    case COOP_PROGRESS_COLLEGE: changed = increase ? spell_add(actor, r->index * 5 + current, false) : spell_remove(actor, r->index * 5 + current - 1); break;
    case COOP_PROGRESS_DISCIPLINE: changed = (increase ? tech_degree_inc(actor, r->index) : tech_degree_dec(actor, r->index)) == next; break;
    default: return COOP_PROGRESS_REQUIREMENTS;
    }
    if (!changed) return COOP_PROGRESS_REQUIREMENTS;
    /* The native basic-skill minus control refunds internal points until its
     * displayed level changes (effects can cap it). Keep that click meaning,
     * bounded by the host's accepted baseline and point-balance range. */
    if (!increase && old_skill_level >= 0) {
        while (basic_skill_level(actor, r->index) == old_skill_level && next > floor) {
            int refund = basic_skill_cost_dec(actor, r->index);
            if (refund <= 0 || next - floor < refund || balance > INT32_MAX - refund
                || balance + refund > stat_level_max(actor, STAT_UNSPENT_POINTS)) break;
            if (basic_skill_points_set(actor, r->index, next - refund) != next - refund) break;
            next -= refund; balance += refund;
        }
    }
    stat_base_set(actor, STAT_UNSPENT_POINTS, balance);
    return COOP_PROGRESS_OK;
}
