#include "coop/engine_craft.h"
#include "coop/authority.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/proto.h"
#include "game/tech.h"
#include "ui/schematic_ui.h"
CoopServiceStatus coop_engine_craft(int64_t pc, int64_t crafter, unsigned recipe)
{
    if (!coop_authority_can_simulate() || !pc || !crafter || !critter_is_active(pc) || !critter_is_active(crafter)
        || (crafter != pc && (obj_field_int32_get(crafter, OBJ_F_TYPE) != OBJ_TYPE_NPC || object_dist(pc, crafter) > 10))) return COOP_SERVICE_UNAVAILABLE;
    bool known = false;
    for (int tech = 0; tech < TECH_COUNT; tech++)
        for (int degree = 1; degree < DEGREE_COUNT && degree <= tech_degree_get(crafter, tech); degree++)
            if (tech_schematic_get(tech, degree) == (int)recipe) known = true;
    if (obj_field_int32_get(crafter, OBJ_F_TYPE) == OBJ_TYPE_PC) {
        int count = obj_arrayfield_length_get(crafter, OBJ_F_PC_SCHEMATICS_FOUND_IDX);
        for (int i = 0; i < count; i++) if (obj_arrayfield_uint32_get(crafter, OBJ_F_PC_SCHEMATICS_FOUND_IDX, i) == recipe) known = true;
    }
    if (!known || !schematic_ui_recipe_valid((int)recipe)) return COOP_SERVICE_KNOWLEDGE;
    SchematicInfo info = { 0 }; schematic_ui_info_get((int)recipe, &info);
    if (info.qty <= 0 || info.qty > 1000) return COOP_SERVICE_LIMIT;
    const int* components[] = { info.item1, info.item2 };
    for (unsigned part = 0; part < 2; part++) {
        int64_t prototype = 0, component = 0;
        for (unsigned variant = 0; variant < 3; variant++) {
            prototype = sub_4685A0(components[part][variant]);
            if (prototype && (component = sub_462540(crafter, prototype, 7))) break;
        }
        if (!component || !prototype) return COOP_SERVICE_COMPONENTS;
        int discipline = obj_field_int32_get(prototype, OBJ_F_ITEM_DISCIPLINE), complexity = obj_field_int32_get(prototype, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY);
        if (complexity < 0 && (discipline < 0 || discipline >= TECH_COUNT || tech_degree_level_get(crafter, discipline) < -(int64_t)complexity)) return COOP_SERVICE_EXPERTISE;
    }
    return schematic_ui_process_authoritative((int)recipe, crafter, pc) ? COOP_SERVICE_OK : COOP_SERVICE_COMPONENTS;
}
