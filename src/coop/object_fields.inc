/* Compiled inside obj.c to keep private field layouts out of the wire API. */
#include "coop/engine_object.h"
#include <stdio.h>

struct CoopStagedObject {
    Object object;
};

static bool replica_pose_field(int field)
{
    return field == OBJ_F_CURRENT_AID || field == OBJ_F_LOCATION || field == OBJ_F_OFFSET_X || field == OBJ_F_OFFSET_Y
        || field == OBJ_F_FLAGS || field == OBJ_F_HP_DAMAGE || field == OBJ_F_CRITTER_FATIGUE_DAMAGE;
}

bool coop_object_id_from_engine(ObjectID id, CoopObjectId* out)
{
    memset(out, 0, sizeof(*out));
    if (id.type == OID_TYPE_HANDLE) id = obj_get_id(id.d.h);
    switch (id.type) {
    case OID_TYPE_NULL:
        break;
    case OID_TYPE_A:
        out->bytes[0] = 1;
        coop_object_write32(out->bytes + 1, (uint32_t)id.d.a);
        break;
    case OID_TYPE_GUID:
        out->bytes[0] = 2;
        memcpy(out->bytes + 1, id.d.g.data, 16);
        break;
    case OID_TYPE_P:
        out->bytes[0] = 3;
        coop_object_write32(out->bytes + 1, (uint32_t)location_get_x(id.d.p.location));
        coop_object_write32(out->bytes + 5, (uint32_t)location_get_y(id.d.p.location));
        coop_object_write32(out->bytes + 9, (uint32_t)id.d.p.temp_id);
        coop_object_write32(out->bytes + 13, (uint32_t)id.d.p.map);
        break;
    default:
        return false;
    }
    return coop_object_id_valid(out, true);
}
ObjectID coop_object_id_to_engine(CoopObjectId id)
{
    ObjectID out = { 0 };
    switch (id.bytes[0]) {
    case 1:
        out.type = OID_TYPE_A;
        out.d.a = (int32_t)coop_object_read32(id.bytes + 1);
        break;
    case 2:
        out.type = OID_TYPE_GUID;
        memcpy(out.d.g.data, id.bytes + 1, 16);
        break;
    case 3:
        out.type = OID_TYPE_P;
        out.d.p.location = location_make(coop_object_read32(id.bytes + 1), coop_object_read32(id.bytes + 5));
        out.d.p.temp_id = (int32_t)coop_object_read32(id.bytes + 9);
        out.d.p.map = (int32_t)coop_object_read32(id.bytes + 13);
        break;
    }
    return out;
}
static uint8_t replica_kind(int type)
{
    switch (type) {
    case OD_TYPE_INT32:
        return COOP_VALUE_I32;
    case OD_TYPE_INT64:
        return COOP_VALUE_I64;
    case OD_TYPE_STRING:
        return COOP_VALUE_STRING;
    case OD_TYPE_HANDLE:
        return COOP_VALUE_REFERENCE;
    case OD_TYPE_INT32_ARRAY:
    case OD_TYPE_UINT32_ARRAY:
        return COOP_VALUE_ARRAY32;
    case OD_TYPE_INT64_ARRAY:
    case OD_TYPE_UINT64_ARRAY:
        return COOP_VALUE_ARRAY64;
    case OD_TYPE_HANDLE_ARRAY:
        return COOP_VALUE_ARRAY_REFERENCE;
    case OD_TYPE_SCRIPT_ARRAY:
        return COOP_VALUE_ARRAY_SCRIPT;
    case OD_TYPE_QUEST_ARRAY:
        return COOP_VALUE_ARRAY_QUEST;
    default:
        return 0;
    }
}
static size_t replica_native_width(uint8_t kind)
{
    switch (kind) {
    case COOP_VALUE_ARRAY32:
        return sizeof(uint32_t);
    case COOP_VALUE_ARRAY64:
        return sizeof(uint64_t);
    case COOP_VALUE_ARRAY_REFERENCE:
        return sizeof(ObjectID);
    case COOP_VALUE_ARRAY_SCRIPT:
        return sizeof(Script);
    case COOP_VALUE_ARRAY_QUEST:
        return sizeof(PcQuestState);
    default:
        return 0;
    }
}
static struct {
    uint8_t kind;
    uint8_t* next;
    unsigned count;
} replica_array_export;

static bool replica_export_element(void* entry, int index)
{
    if (index < 0 || index > COOP_OBJECT_KEY_LIMIT || !replica_array_export.count) return false;
    replica_array_export.count--;
    uint8_t* p = replica_array_export.next;
    coop_object_write32(p, (uint32_t)index);
    p += 4;
    uint32_t value32;
    uint64_t value64;
    switch (replica_array_export.kind) {
    case COOP_VALUE_ARRAY32:
        memcpy(&value32, entry, sizeof(value32));
        coop_object_write32(p, value32);
        break;
    case COOP_VALUE_ARRAY64:
        memcpy(&value64, entry, sizeof(value64));
        coop_object_write64(p, value64);
        break;
    case COOP_VALUE_ARRAY_REFERENCE: {
        ObjectID oid;
        CoopObjectId id;
        memcpy(&oid, entry, sizeof(oid));
        if (!coop_object_id_from_engine(oid, &id)) return false;
        memcpy(p, &id, sizeof(id));
        break;
    }
    case COOP_VALUE_ARRAY_SCRIPT: {
        Script script;
        memcpy(&script, entry, sizeof(script));
        coop_object_write32(p, script.hdr.flags);
        coop_object_write32(p + 4, script.hdr.counters);
        coop_object_write32(p + 8, (uint32_t)script.num);
        break;
    }
    case COOP_VALUE_ARRAY_QUEST: {
        PcQuestState quest;
        memcpy(&quest, entry, sizeof(quest));
        coop_object_write32(p, quest.datetime.days);
        coop_object_write32(p + 4, quest.datetime.milliseconds);
        coop_object_write32(p + 8, (uint32_t)quest.state);
        break;
    }
    }
    replica_array_export.next = p + coop_object_element_size(replica_array_export.kind);
    return true;
}
static bool replica_export_field(Object* object, int field, CoopObjectField* out)
{
    int type = object_fields[field].type;
    uint8_t kind = replica_kind(type);
    if (!kind) return false;
    intptr_t* storage = &object->data[sub_40D230(object, field)];
    uint8_t* bytes;
    size_t size;
    if (kind >= COOP_VALUE_ARRAY32) {
        SizeableArray* array = *(SizeableArray**)storage;
        unsigned count = array ? (unsigned)array->count : 0;
        if (count > COOP_OBJECT_ARRAY_LIMIT || (array && array->size != (int)replica_native_width(kind))) return false;
        size = 4 + count * (4 + coop_object_element_size(kind));
        bytes = MALLOC(size);
        coop_object_write32(bytes, count);
        if (array) {
            replica_array_export.kind = kind;
            replica_array_export.next = bytes + 4;
            replica_array_export.count = count;
            if (!sa_enumerate(&array, replica_export_element) || replica_array_export.count) {
                FREE(bytes);
                return false;
            }
        }
    } else if (kind == COOP_VALUE_STRING) {
        const char* text = *(char**)storage;
        size = text ? strlen(text) : 0;
        if (size > 4096) return false;
        bytes = MALLOC(size ? size : 1);
        if (size) memcpy(bytes, text, size);
    } else {
        size = kind == COOP_VALUE_I32 ? 4 : kind == COOP_VALUE_I64 ? 8
                                                                   : COOP_OBJECT_ID_BYTES;
        bytes = MALLOC(size);
        ObjDataDescriptor descriptor = { 0 };
        descriptor.type = type;
        descriptor.ptr = storage;
        obj_data_fetch(&descriptor);
        bool carried_position = false;
        if (obj_type_is_item(object->type)
            && (field == OBJ_F_LOCATION || field == OBJ_F_OFFSET_X || field == OBJ_F_OFFSET_Y)) {
            unsigned flags;
            obj_field_fetch(object, OBJ_F_FLAGS, &flags);
            carried_position = (flags & OF_INVENTORY) != 0;
        }
        /* Native carried items can retain their last ground coordinates from
         * another map. Their parent/inventory relationships define placement. */
        if (kind == COOP_VALUE_I32) {
            uint32_t value = carried_position ? 0 : (uint32_t)descriptor.storage.value;
            if (field == OBJ_F_FLAGS) value &= ~(OF_TEXT | OF_TEXT_FLOATER | OF_HAS_OVERLAYS | OF_HAS_UNDERLAYS);
            coop_object_write32(bytes, value);
        } else if (kind == COOP_VALUE_I64) {
            coop_object_write64(bytes, carried_position ? 0 : (uint64_t)descriptor.storage.value64);
        } else {
            CoopObjectId id;
            if (!coop_object_id_from_engine(descriptor.storage.oid, &id)) {
                FREE(bytes);
                return false;
            }
            memcpy(bytes, &id, sizeof(id));
        }
    }
    *out = (CoopObjectField) { (uint16_t)field, kind, (uint32_t)size, bytes };
    return true;
}
uint8_t* coop_engine_object_export(int64_t handle, size_t* size)
{
    return coop_engine_object_export_fields(handle, size, false);
}
uint8_t* coop_engine_object_export_fields(int64_t handle, size_t* size, bool separate_pc_pose)
{
    if (!size || !obj_handle_is_valid(handle) || obj_is_proto(handle)) return NULL;
    CoopObjectState state = { 0 };
    if (!coop_object_id_from_engine(obj_get_id(handle), &state.id)) return NULL;
    Object* object = obj_lock(handle);
    if (!coop_object_id_from_engine(object->prototype_oid, &state.prototype)) {
        obj_unlock(handle);
        return NULL;
    }
    state.type = (uint8_t)object->type;
    state.options = separate_pc_pose && object->type == OBJ_TYPE_PC ? COOP_OBJECT_SEPARATE_PC_POSE : 0;
    bool good = true;
    for (int field = 1; good && field < OBJ_F_TOTAL_NORMAL; field++) {
        if (!object_field_valid(object->type, field) || !sub_40D320(object, field)) continue;
        if (state.options && replica_pose_field(field)) continue;
        good = state.count < COOP_OBJECT_FIELDS && replica_export_field(object, field, &state.fields[state.count]);
        if (good) state.count++;
    }
    obj_unlock(handle);
    uint8_t* bytes = good ? coop_object_state_encode(&state, size) : NULL;
    for (unsigned i = 0; i < state.count; i++)
        FREE((void*)state.fields[i].data);
    return bytes;
}
void coop_engine_object_discard(CoopStagedObject* state)
{
    if (!state) return;
    if (state->object.field_48) {
        object_inst_enumerate_overridden_fields(&state->object, object_inst_field_dealloc);
        FREE(state->object.data);
        sub_40C5B0(&state->object);
    }
    FREE(state);
}
static void replica_read_value(ObjDataDescriptor* descriptor, uint8_t kind, const uint8_t* p)
{
    switch (kind) {
    case COOP_VALUE_I32:
    case COOP_VALUE_ARRAY32:
        descriptor->storage.value = (int32_t)coop_object_read32(p);
        break;
    case COOP_VALUE_I64:
    case COOP_VALUE_ARRAY64:
        descriptor->storage.value64 = (int64_t)coop_object_read64(p);
        break;
    case COOP_VALUE_REFERENCE:
    case COOP_VALUE_ARRAY_REFERENCE: {
        CoopObjectId id;
        memcpy(&id, p, sizeof(id));
        descriptor->storage.oid = coop_object_id_to_engine(id);
        break;
    }
    case COOP_VALUE_ARRAY_SCRIPT:
        descriptor->storage.scr.hdr.flags = coop_object_read32(p);
        descriptor->storage.scr.hdr.counters = coop_object_read32(p + 4);
        descriptor->storage.scr.num = (int32_t)coop_object_read32(p + 8);
        break;
    case COOP_VALUE_ARRAY_QUEST:
        descriptor->storage.quest.datetime.days = coop_object_read32(p);
        descriptor->storage.quest.datetime.milliseconds = coop_object_read32(p + 4);
        descriptor->storage.quest.state = (int32_t)coop_object_read32(p + 8);
        descriptor->storage.quest.padding_C = 0;
        break;
    }
}
CoopStagedObject* coop_engine_object_stage(const void* bytes, size_t size)
{
    CoopObjectState view;
    if (!coop_object_state_decode(bytes, size, &view)) return NULL;
    ObjectID prototype_id = coop_object_id_to_engine(view.prototype);
    int64_t prototype = obj_pool_perm_lookup(prototype_id);
    if (!prototype || !obj_is_proto(prototype) || obj_field_int32_get(prototype, OBJ_F_TYPE) != view.type) return NULL;
    for (unsigned i = 0; i < view.count; i++) {
        unsigned field = view.fields[i].field;
        if (field >= OBJ_F_TOTAL_NORMAL || !object_field_valid(view.type, (int)field)
            || replica_kind(object_fields[field].type) != view.fields[i].kind || (view.options && replica_pose_field((int)field))) return NULL;
    }
    int64_t original = view.options ? obj_pool_perm_lookup(coop_object_id_to_engine(view.id)) : 0;
    if (view.options && (!original || obj_field_int32_get(original, OBJ_F_TYPE) != OBJ_TYPE_PC)) return NULL;
    CoopStagedObject* state = CALLOC(1, sizeof(*state));
    Object* object = &state->object;
    object->type = view.type;
    object->oid = coop_object_id_to_engine(view.id);
    object->prototype_oid = prototype_id;
    object->prototype_obj = prototype;
    object->num_fields = (int16_t)view.count;
    object->modified = true;
    sub_40C580(object);
    object->data = CALLOC(view.count ? view.count : 1, sizeof(*object->data));
    for (unsigned i = 0; i < view.count; i++) {
        const CoopObjectField* field = &view.fields[i];
        sub_40D370(object, field->field, true);
        ObjDataDescriptor descriptor = { 0 };
        descriptor.type = object_fields[field->field].type;
        descriptor.ptr = &object->data[i];
        const uint8_t* p = field->data;
        if (field->kind == COOP_VALUE_STRING) {
            char* text = MALLOC(field->size + 1);
            memcpy(text, p, field->size);
            text[field->size] = 0;
            *(char**)descriptor.ptr = text;
        } else if (field->kind >= COOP_VALUE_ARRAY32) {
            uint32_t count = coop_object_read32(p);
            size_t width = coop_object_element_size(field->kind);
            sa_allocate((SizeableArray**)descriptor.ptr, (int)replica_native_width(field->kind));
            for (uint32_t j = 0; j < count; j++) {
                const uint8_t* entry = p + 4 + j * (4 + width);
                descriptor.idx = (int)coop_object_read32(entry);
                replica_read_value(&descriptor, field->kind, entry + 4);
                obj_data_store(&descriptor);
            }
        } else {
            replica_read_value(&descriptor, field->kind, p);
            obj_data_store(&descriptor);
        }
    }
    if (view.options) {
        Object* source = obj_lock(original);
        for (int field = 1; field < OBJ_F_TOTAL_NORMAL; field++) {
            if (!replica_pose_field(field)) continue;
            if (object_fields[field].type == OD_TYPE_INT64) {
                int64_t value;
                obj_field_fetch(source, field, &value);
                obj_field_store(object, field, &value);
            } else {
                int value;
                obj_field_fetch(source, field, &value);
                obj_field_store(object, field, &value);
            }
        }
        obj_unlock(original);
    }
    return state;
}
ObjectID coop_engine_object_id(const CoopStagedObject* state) { return state->object.oid; }
ObjectID coop_engine_object_prototype(const CoopStagedObject* state) { return state->object.prototype_oid; }
bool coop_engine_object_matches(const CoopStagedObject* state, int64_t handle)
{
    if (!obj_handle_is_valid(handle) || obj_is_proto(handle)) return false;
    /* Spatial lookup can cache a static object before its lazy positional OID
     * has been materialized in Object. Compare its canonical public identity. */
    ObjectID actual = obj_get_id(handle);
    Object* object = obj_lock(handle);
    bool matches = object->type == state->object.type && objid_is_equal(actual, state->object.oid)
        && objid_is_equal(object->prototype_oid, state->object.prototype_oid);
    obj_unlock(handle);
    return matches;
}
static const ObjectID* replica_deleted_ids;
static unsigned replica_deleted_count;
static bool replica_reference_found;
static bool replica_check_reference(void* entry, int index)
{
    (void)index;
    ObjectID id;
    memcpy(&id, entry, sizeof(id));
    if (id.type == OID_TYPE_HANDLE) id = obj_get_id(id.d.h);
    for (unsigned i = 0; i < replica_deleted_count; i++) {
        if (objid_is_equal(id, replica_deleted_ids[i])) {
            replica_reference_found = true;
            return false;
        }
    }
    return true;
}
bool coop_engine_object_refers_to(int64_t handle, const ObjectID* ids, unsigned count)
{
    replica_deleted_ids = ids;
    replica_deleted_count = count;
    replica_reference_found = false;
    Object* object = obj_lock(handle);
    for (int field = 1; field < OBJ_F_TOTAL_NORMAL && !replica_reference_found; field++) {
        if (!object_field_valid(object->type, field) || !sub_40D320(object, field)) continue;
        intptr_t* storage = &object->data[sub_40D230(object, field)];
        if (object_fields[field].type == OD_TYPE_HANDLE && *(ObjectID**)storage)
            replica_check_reference(*(ObjectID**)storage, 0);
        if (object_fields[field].type == OD_TYPE_HANDLE_ARRAY && *(SizeableArray**)storage)
            sa_enumerate((SizeableArray**)storage, replica_check_reference);
        if (replica_reference_found) {
            char owner[OID_STR_LEN];
            objid_id_to_str(owner, object->oid);
            fprintf(stderr, "ARCMP deletion referenced by object=%s type=%d field=%d handle=%" PRId64 "\n", owner, object->type, field, handle);
        }
    }
    obj_unlock(handle);
    return replica_reference_found;
}
static bool replica_resolve_id(ObjectID* id)
{
    if (id->type == OID_TYPE_NULL) return true;
    ObjectID permanent = id->type == OID_TYPE_HANDLE ? obj_get_id(id->d.h) : *id;
    for (unsigned i = 0; i < replica_deleted_count; i++)
        if (objid_is_equal(permanent, replica_deleted_ids[i])) return false;
    if (id->type == OID_TYPE_HANDLE) return obj_handle_is_valid(id->d.h);
    int64_t handle = obj_pool_perm_lookup(*id);
    if (!handle) return false;
    memset(id, 0, sizeof(*id));
    id->type = OID_TYPE_HANDLE;
    id->d.h = handle;
    return true;
}
static bool replica_resolve_element(void* entry, int index)
{
    (void)index;
    ObjectID id;
    memcpy(&id, entry, sizeof(id));
    if (!replica_resolve_id(&id)) return false;
    memcpy(entry, &id, sizeof(id));
    return true;
}
bool coop_engine_object_resolve(CoopStagedObject* state)
{
    return coop_engine_object_resolve_except(state, NULL, 0);
}
bool coop_engine_object_resolve_except(CoopStagedObject* state, const ObjectID* deleted, unsigned count)
{
    replica_deleted_ids = deleted;
    replica_deleted_count = count;
    Object* object = &state->object;
    for (int field = 1; field < OBJ_F_TOTAL_NORMAL; field++) {
        if (!object_field_valid(object->type, field) || !sub_40D320(object, field)) continue;
        intptr_t* storage = &object->data[sub_40D230(object, field)];
        if (object_fields[field].type == OD_TYPE_HANDLE && *(ObjectID**)storage
            && !replica_resolve_id(*(ObjectID**)storage)) return false;
        if (object_fields[field].type == OD_TYPE_HANDLE_ARRAY && *(SizeableArray**)storage
            && !sa_enumerate((SizeableArray**)storage, replica_resolve_element)) return false;
    }
    return true;
}
int64_t coop_engine_object_location(CoopStagedObject* state, int* x, int* y)
{
    int64_t location;
    obj_field_fetch(&state->object, OBJ_F_LOCATION, &location);
    obj_field_fetch(&state->object, OBJ_F_OFFSET_X, x);
    obj_field_fetch(&state->object, OBJ_F_OFFSET_Y, y);
    return location;
}
void coop_engine_object_install(CoopStagedObject* state, int64_t handle)
{
    assert(coop_engine_object_matches(state, handle));
    Object* object = obj_lock(handle);
    unsigned old_flags;
    obj_field_fetch(object, OBJ_F_FLAGS, &old_flags);
    object_inst_enumerate_overridden_fields(object, object_inst_field_dealloc);
    FREE(object->data);
    sub_40C5B0(object);
    object->data = state->object.data;
    object->field_48 = state->object.field_48;
    object->field_4C = state->object.field_4C;
    object->num_fields = state->object.num_fields;
    object->modified = true;
    state->object.data = NULL;
    state->object.field_48 = NULL;
    unsigned flags;
    obj_field_fetch(object, OBJ_F_FLAGS, &flags);
    const unsigned local_flags = OF_TEXT | OF_TEXT_FLOATER | OF_HAS_OVERLAYS | OF_HAS_UNDERLAYS;
    unsigned merged = (flags & ~local_flags) | (old_flags & local_flags);
    if (merged != flags) obj_field_store(object, OBJ_F_FLAGS, &merged);
    obj_unlock(handle);
    obj_find_move(handle);
}
