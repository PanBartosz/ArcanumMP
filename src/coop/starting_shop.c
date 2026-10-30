#include "coop/starting_shop.h"
#include "coop/engine_character.h"
#include "game/background.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/item.h"
#include "game/level.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/obj_private.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/random.h"
#include "game/reaction.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CoopStartingShop {
    int64_t pc, vendor, stock;
    struct CoopStartingShop* next;
};
static CoopStartingShop* active_shops;
static bool private_vendor(int64_t object)
{
    for (CoopStartingShop* s = active_shops; s; s = s->next)
        if (s->vendor == object) return true;
    return false;
}
static int inv_count(int64_t owner) { return obj_field_int32_get(owner, obj_field_int32_get(owner, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER ? OBJ_F_CONTAINER_INVENTORY_NUM : OBJ_F_CRITTER_INVENTORY_NUM); }
static int64_t inv_item(int64_t owner, int index) { return obj_arrayfield_handle_get(owner, obj_field_int32_get(owner, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER ? OBJ_F_CONTAINER_INVENTORY_LIST_IDX : OBJ_F_CRITTER_INVENTORY_LIST_IDX, index); }
static void clear_inventory(int64_t owner)
{
    while (owner && inv_count(owner) > 0) {
        int64_t item = inv_item(owner, 0);
        if (!item) break;
        item_remove(item);
        object_delete(item);
    }
}
void coop_starting_close(CoopStartingShop* s)
{
    if (!s) return;
    CoopStartingShop** at = &active_shops;
    while (*at && *at != s)
        at = &(*at)->next;
    if (*at) *at = s->next;
    if (s->pc) {
        clear_inventory(s->pc);
        background_clear(s->pc);
        object_delete(s->pc);
    }
    if (s->stock && s->stock != s->vendor) {
        clear_inventory(s->stock);
        object_delete(s->stock);
    }
    if (s->vendor) {
        clear_inventory(s->vendor);
        object_delete(s->vendor);
    }
    free(s);
}
static unsigned quantity(int64_t item)
{
    return obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_AMMO ? (unsigned)obj_field_int32_get(item, OBJ_F_AMMO_QUANTITY) : 1;
}
static int prototype(int64_t item)
{
    ObjectID id = obj_get_id(obj_field_handle_get(item, OBJ_F_PROTOTYPE_HANDLE));
    return id.type == OID_TYPE_A ? id.d.a : 0;
}
static int64_t find_item(int64_t owner, unsigned basic, unsigned count)
{
    for (int i = 0; i < inv_count(owner); i++) {
        int64_t item = inv_item(owner, i);
        if ((unsigned)prototype(item) == basic && quantity(item) >= count && obj_field_int32_get(item, OBJ_F_TYPE) != OBJ_TYPE_GOLD) return item;
    }
    return 0;
}
static bool trade(CoopStartingShop* s, CoopStartingTrade transaction)
{
    bool selling = transaction.quantity < 0;
    unsigned count = (unsigned)(selling ? -(int)transaction.quantity : transaction.quantity);
    int64_t from = selling ? s->pc : s->stock, to = selling ? s->stock : s->pc;
    int64_t item = find_item(from, transaction.prototype, count);
    if (!item || (obj_field_int32_get(item, OBJ_F_TYPE) != OBJ_TYPE_AMMO && count != 1) || item_check_remove(item) != ITEM_CANNOT_OK) return false;
    int unit = item_cost(item, selling ? s->pc : s->vendor, selling ? s->vendor : s->pc, false);
    int64_t total = (int64_t)unit * count, payer = selling ? s->vendor : s->pc, payee = selling ? s->pc : s->vendor;
    if (unit <= 0 || total > INT_MAX || item_gold_get(payer) < total || item_gold_get(payee) > INT_MAX - total) return false;
    int slot = -1;
    if (item_check_insert_quantity(item, to, (int)count, &slot) != ITEM_CANNOT_OK) return false;
    bool split = obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_AMMO && quantity(item) > count;
    int64_t moved = item;
    if (split) {
        obj_perm_dup(&moved, item);
        if (!moved) return false;
        obj_field_handle_set(moved, OBJ_F_ITEM_PARENT, 0);
        obj_field_int32_set(moved, OBJ_F_FLAGS, obj_field_int32_get(moved, OBJ_F_FLAGS) & ~OF_INVENTORY);
        obj_field_int32_set(moved, OBJ_F_AMMO_QUANTITY, (int)count);
    }
    if (!item_transfer(moved, to)) {
        if (split && obj_handle_is_valid(moved)) object_delete(moved);
        return false;
    }
    if (split) obj_field_int32_set(item, OBJ_F_AMMO_QUANTITY, (int)quantity(item) - (int)count);
    return item_gold_transfer(payer, payee, (int)total, 0);
}
CoopStartingShop* coop_starting_open(const CoopCharacter* c)
{
    if (!coop_character_valid(c) || map_current_map() != map_by_type(MAP_TYPE_SHOPPING_MAP)) return NULL;
    int64_t source = 0, object;
    int it;
    if (obj_inst_first(&object, &it)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_NPC && !private_vendor(object)) {
                source = object;
                break;
            }
        } while (obj_inst_next(&object, &it));
    if (!source) return NULL;
    CoopStartingShop* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->next = active_shops;
    active_shops = s;
    unsigned rng = random_state_get();
    obj_perm_dup(&s->vendor, source);
    int64_t substitute = critter_substitute_inventory_get(source);
    if (substitute) {
        obj_perm_dup(&s->stock, substitute);
        obj_field_handle_set(s->vendor, OBJ_F_NPC_SUBSTITUTE_INVENTORY, s->stock);
    } else
        s->stock = s->vendor;
    random_seed(0xac500001u);
    if (obj_field_int32_get(s->vendor, OBJ_F_CRITTER_INVENTORY_SOURCE)) {
        clear_inventory(s->vendor);
        sub_463E20(s->vendor);
    }
    if (s->stock != s->vendor && obj_field_int32_get(s->stock, OBJ_F_CONTAINER_INVENTORY_SOURCE)) {
        clear_inventory(s->stock);
        sub_463E20(s->stock);
    }
    obj_create_inst(sub_4685A0(BP_GENERIC_PC), obj_field_int64_get(source, OBJ_F_LOCATION), &s->pc);
    if (s->pc) level_set(s->pc, 1);
    bool good = s->pc && coop_engine_character_apply(s->pc, c);
    if (good) {
        random_seed(0xac500002u ^ c->race ^ (unsigned)c->gender << 8 ^ (unsigned)c->background << 16);
        item_generate_inventory(s->pc);
        reaction_forget(s->vendor, s->pc);
        for (unsigned i = 0; good && i < c->trades; i++)
            good = trade(s, c->purchases[i]);
    }
    random_seed(rng);
    if (!good) {
        coop_starting_close(s);
        return NULL;
    }
    return s;
}
bool coop_starting_validate(const CoopCharacter* c)
{
    CoopStartingShop* s = coop_starting_open(c);
    bool good = s != NULL;
    coop_starting_close(s);
    return good;
}
bool coop_starting_apply(int64_t pc, const CoopCharacter* c)
{
    if (!pc || !coop_character_valid(c)) return false;
    if (!c->trades) {
        unsigned rng = random_state_get();
        random_seed(0xac500002u ^ c->race ^ (unsigned)c->gender << 8 ^ (unsigned)c->background << 16);
        item_generate_inventory(pc);
        random_seed(rng);
        return true;
    }
    CoopStartingShop* s = coop_starting_open(c);
    if (!s) return false;
    clear_inventory(pc);
    bool good = true;
    while (good && inv_count(s->pc) > 0)
        good = item_transfer(inv_item(s->pc, 0), pc);
    coop_starting_close(s);
    return good;
}
int coop_starting_gold(const CoopStartingShop* s) { return s ? item_gold_get(s->pc) : 0; }
unsigned coop_starting_items(CoopStartingShop* s, bool selling, CoopStartingItem* out, unsigned capacity)
{
    if (!s || !out) return 0;
    int64_t owner = selling ? s->pc : s->stock;
    unsigned n = 0;
    for (int i = 0; i < inv_count(owner) && n < capacity; i++) {
        int64_t item = inv_item(owner, i);
        int basic = prototype(item);
        if (basic <= 0 || basic > UINT16_MAX || obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_GOLD) continue;
        CoopStartingItem row = { .prototype = (uint16_t)basic, .quantity = quantity(item) };
        row.price = item_cost(item, selling ? s->pc : s->vendor, selling ? s->vendor : s->pc, false);
        char name[80];
        object_examine(item, s->pc, name);
        snprintf(row.name, sizeof(row.name), "%s", name);
        out[n++] = row;
    }
    return n;
}

bool coop_starting_choose(CoopCharacter* c)
{
    CoopStartingShop* s = coop_starting_open(c);
    if (!s) return false;
    CoopStartingItem rows[300];
    unsigned n = coop_starting_items(s, false, rows, 300);
    int gold = coop_starting_gold(s);
    bool good = false;
    for (unsigned i = 0; i < n && !good; i++) {
        if (rows[i].price <= 0 || rows[i].price > gold || c->trades == COOP_STARTING_TRADES) continue;
        CoopCharacter next = *c;
        next.purchases[next.trades++] = (CoopStartingTrade) { rows[i].prototype, 1 };
        if (coop_starting_validate(&next)) {
            *c = next;
            good = true;
        }
    }
    coop_starting_close(s);
    return good;
}
bool coop_starting_probe(bool framebuffer)
{
    (void)framebuffer;
    CoopCharacter c;
    if (!coop_engine_character_capture(player_get_local_pc_obj(), &c)) return false;
    unsigned rng = random_state_get();
    if (!coop_starting_choose(&c) || random_state_get() != rng) return false;
    CoopStartingShop* a = coop_starting_open(&c);
    CoopStartingShop* b = coop_starting_open(&c);
    CoopStartingItem x[300] = { 0 }, y[300] = { 0 };
    unsigned nx = coop_starting_items(a, true, x, 300), ny = coop_starting_items(b, true, y, 300);
    bool good = a && b && nx == ny && nx && !memcmp(x, y, nx * sizeof(*x)) && coop_starting_gold(a) == coop_starting_gold(b) && random_state_get() == rng;
    int gold = coop_starting_gold(a);
    CoopCharacter sold = c;
    sold.purchases[sold.trades++] = (CoopStartingTrade) { c.purchases[0].prototype, -1 };
    CoopStartingShop* sale = coop_starting_open(&sold);
    good &= sale && coop_starting_gold(sale) > gold;
    coop_starting_close(sale);
    CoopStartingItem stock[300];
    unsigned available = coop_starting_items(a, false, stock, 300), ammo_cases = 0;
    for (unsigned i = 0; i < available; i++)
        if (stock[i].quantity > 1 && stock[i].price > 0) {
            CoopCharacter ammo = c;
            ammo.trades = 1;
            memset(ammo.purchases, 0, sizeof(ammo.purchases));
            unsigned amount = stock[i].quantity > 2 ? 2 : 1;
            ammo.purchases[0] = (CoopStartingTrade) { stock[i].prototype, (int16_t)amount };
            CoopStartingShop* stack = coop_starting_open(&ammo);
            if (stack) {
                int after_buy = coop_starting_gold(stack);
                coop_starting_close(stack);
                ammo.purchases[ammo.trades++] = (CoopStartingTrade) { stock[i].prototype, -(int16_t)amount };
                stack = coop_starting_open(&ammo);
                good &= stack && coop_starting_gold(stack) > after_buy;
                coop_starting_close(stack);
                ammo_cases++;
                break;
            }
        }
    coop_starting_close(a);
    coop_starting_close(b);
    CoopCharacter invalid = c;
    invalid.purchases[0].prototype = 65535;
    good &= !coop_starting_validate(&invalid);
    invalid = c;
    invalid.purchases[0].quantity = 32767;
    good &= !coop_starting_validate(&invalid);
    fprintf(stderr, "ARCMP starting shop %s deterministic=1 invalid_stock=1 excessive_quantity=1 sell=1 ammo_cases=%u gold=%d\n", good ? "PASS" : "FAIL", ammo_cases, gold);
    return good;
}

uint32_t coop_starting_inventory(int64_t pc)
{
    uint64_t rows[300] = { 0 };
    unsigned count = 0;
    for (int i = 0; i < inv_count(pc) && count < 300; i++) {
        int64_t item = inv_item(pc, i);
        unsigned basic = (unsigned)prototype(item);
        unsigned n = obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_GOLD ? (unsigned)item_gold_get(item) : quantity(item);
        unsigned at = 0;
        while (at < count && (rows[at] >> 32) != basic)
            at++;
        if (at == count) rows[count++] = (uint64_t)basic << 32;
        rows[at] += n;
    }
    for (unsigned i = 0; i < count; i++)
        for (unsigned j = i + 1; j < count; j++)
            if (rows[j] < rows[i]) {
                uint64_t tmp = rows[i];
                rows[i] = rows[j];
                rows[j] = tmp;
            }
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < count; i++)
        for (unsigned b = 0; b < 8; b++)
            hash = (hash ^ (uint8_t)(rows[i] >> (b * 8))) * 16777619u;
    return hash;
}
uint32_t coop_starting_preview(const CoopCharacter* c)
{
    CoopStartingShop* s = coop_starting_open(c);
    if (!s) return 0;
    uint32_t hash = coop_starting_inventory(s->pc);
    coop_starting_close(s);
    return hash;
}
