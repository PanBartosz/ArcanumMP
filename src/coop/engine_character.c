#include "coop/engine_character.h"
#include "coop/starting_shop.h"
#include "game/background.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/level.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/player.h"
#include "game/portrait.h"
#include "game/proto.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/tech.h"
#include "game/obj_private.h"
#include <string.h>

static bool spend(int64_t pc, int cost)
{
    int points = stat_level_get(pc, STAT_UNSPENT_POINTS);
    if (cost <= 0 || cost > points) return false;
    return stat_base_set(pc, STAT_UNSPENT_POINTS, points - cost) == points - cost;
}
bool coop_engine_character_capture(int64_t pc, CoopCharacter* c)
{
    if (!pc || !c || !player_is_pc_obj(pc) || stat_level_get(pc, STAT_LEVEL) != 1) return false;
    memset(c, 0, sizeof(*c));
    c->race = (uint8_t)stat_base_get(pc, STAT_RACE); c->gender = (uint8_t)stat_base_get(pc, STAT_GENDER);
    c->background = (uint16_t)background_get(pc); c->portrait = (uint32_t)portrait_get(pc);
    c->hp = (uint16_t)object_hp_pts_get(pc); c->fatigue = (uint16_t)critter_fatigue_pts_get(pc);
    c->unspent = (uint16_t)stat_base_get(pc, STAT_UNSPENT_POINTS);
    for (int i = 0; i < 8; i++) { c->stats[i] = (uint16_t)stat_base_get(pc, i); c->disciplines[i] = (uint8_t)tech_degree_get(pc, i); }
    for (int i = 0; i < 16; i++) {
        c->skills[i] = (uint8_t)(i < BASIC_SKILL_COUNT ? basic_skill_points_get(pc, i) : tech_skill_points_get(pc, i - BASIC_SKILL_COUNT));
        c->colleges[i] = (uint8_t)spell_college_level_get(pc, i);
    }
    char* name = NULL;
    obj_field_string_get(pc, OBJ_F_PC_PLAYER_NAME, &name);
    if (!name) return false;
    size_t length = strlen(name);
    if (length < sizeof(c->name)) memcpy(c->name, name, length);
    FREE(name);
    if (length >= sizeof(c->name)) return false;
    return coop_character_valid(c);
}
bool coop_engine_character_identity(int64_t pc, const CoopCharacter* c)
{
    static const int bodies[] = { TIG_ART_CRITTER_BODY_TYPE_HUMAN, TIG_ART_CRITTER_BODY_TYPE_DWARF,
        TIG_ART_CRITTER_BODY_TYPE_ELF, TIG_ART_CRITTER_BODY_TYPE_HUMAN, TIG_ART_CRITTER_BODY_TYPE_HALFLING,
        TIG_ART_CRITTER_BODY_TYPE_HALFLING, TIG_ART_CRITTER_BODY_TYPE_HUMAN, TIG_ART_CRITTER_BODY_TYPE_HALF_OGRE };
    if (!coop_character_valid(c) || !pc || !player_is_pc_obj(pc) || stat_base_get(pc, STAT_LEVEL) != 1
        || (c->gender == GENDER_FEMALE && c->race != RACE_HUMAN && c->race != RACE_ELF
            && c->race != RACE_HALF_ELF && c->race != RACE_HALF_ORC)) return false;
    background_clear(pc);
    object_set_gender_and_race(pc, bodies[c->race], c->gender, c->race);
    int background = 0;
    bool found = background_find_first(pc, &background);
    for (unsigned n = 0; found && background != c->background && n < 256; n++) found = background_find_next(pc, &background);
    if (!found || background != c->background) return false;
    if (background) background_set(pc, background, background_get_description(background));
    int portrait = 0;
    found = portrait_find_first(pc, &portrait);
    for (unsigned n = 0; found && (uint32_t)portrait != c->portrait && n < 4096; n++) found = portrait_find_next(pc, &portrait);
    if (!found || (uint32_t)portrait != c->portrait) return false;
    obj_field_int32_set(pc, OBJ_F_CRITTER_PORTRAIT, portrait);
    obj_field_string_set(pc, OBJ_F_PC_PLAYER_NAME, c->name);
    return true;
}
bool coop_engine_character_apply(int64_t pc, const CoopCharacter* c)
{
    if (!coop_engine_character_identity(pc, c)) return false;
    for (int stat = 0; stat < 8; stat++) {
        int value = stat_base_get(pc, stat);
        if (c->stats[stat] < value) return false;
        while (value < c->stats[stat]) {
            if (stat_level_get(pc, stat) >= stat_level_max(pc, stat) || !spend(pc, stat_cost(value + 1))
                || stat_base_set(pc, stat, value + 1) != value + 1) return false;
            value++;
        }
    }
    int hp = object_hp_pts_get(pc), ft = critter_fatigue_pts_get(pc);
    if (c->hp < hp || c->fatigue < ft) return false;
    if ((c->hp > hp && !spend(pc, c->hp - hp)) || (c->fatigue > ft && !spend(pc, c->fatigue - ft))) return false;
    object_hp_pts_set(pc, c->hp); critter_fatigue_pts_set(pc, c->fatigue);
    for (int i = 0; i < 16; i++) {
        bool basic = i < BASIC_SKILL_COUNT;
        int skill = basic ? i : i - BASIC_SKILL_COUNT;
        int points = basic ? basic_skill_points_get(pc, skill) : tech_skill_points_get(pc, skill);
        if (c->skills[i] < points) return false;
        while (points < c->skills[i]) {
            int cost = basic ? basic_skill_cost_inc(pc, skill) : tech_skill_cost_inc(pc, skill);
            if (points + cost > c->skills[i] || !spend(pc, cost)) return false;
            int next = basic ? basic_skill_points_set(pc, skill, points + cost) : tech_skill_points_set(pc, skill, points + cost);
            if (next != points + cost) return false;
            points = next;
        }
        int level = spell_college_level_get(pc, i);
        if (c->colleges[i] < level) return false;
        while (level < c->colleges[i]) {
            int spell = i * 5 + level;
            if (!spend(pc, spell_cost(spell)) || !spell_add(pc, spell, false)) return false;
            level++;
        }
    }
    for (int i = 0; i < 8; i++) {
        int degree = tech_degree_get(pc, i);
        if (c->disciplines[i] < degree) return false;
        while (degree < c->disciplines[i]) {
            if (!spend(pc, tech_degree_cost_get(degree + 1)) || tech_degree_inc(pc, i) != degree + 1) return false;
            degree++;
        }
    }
    object_hp_damage_set(pc, 0); critter_fatigue_damage_set(pc, 0);
    return stat_base_get(pc, STAT_UNSPENT_POINTS) == c->unspent;
}
bool coop_engine_character_validate(const CoopCharacter* c)
{
    if (!coop_character_valid(c)) return false;
    int64_t prototype = sub_4685A0(BP_GENERIC_PC), pc = 0;
    if (!prototype) return false;
    /* Bare pool instances never enter sectors, animation, inventory generation
     * or scripts. Validation cannot alter the lobby's current PC or campaign. */
    obj_create_inst(prototype, 0, &pc);
    if (!pc) return false;
    level_set(pc, 1);
    bool good = coop_engine_character_apply(pc, c);
    background_clear(pc);
    obj_deallocate(pc);
    return good && (!c->trades || coop_starting_validate(c));
}
