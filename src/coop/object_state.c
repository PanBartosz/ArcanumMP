#include "coop/object_state.h"
#include <stdlib.h>
#include <string.h>

enum { OBJECT_HEADER = 43,
    FIELD_HEADER = 7 };
uint32_t coop_object_read32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint64_t coop_object_read64(const uint8_t* p)
{
    return ((uint64_t)coop_object_read32(p) << 32) | coop_object_read32(p + 4);
}
void coop_object_write32(uint8_t* p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (24 - i * 8));
}
void coop_object_write64(uint8_t* p, uint64_t n)
{
    coop_object_write32(p, (uint32_t)(n >> 32));
    coop_object_write32(p + 4, (uint32_t)n);
}
bool coop_object_id_valid(const CoopObjectId* id, bool allow_null)
{
    if (!id || id->bytes[0] > 3) return false;
    if (id->bytes[0] == 0 || id->bytes[0] == 1) {
        unsigned first = id->bytes[0] == 0 ? 1 : 5;
        for (unsigned i = first; i < COOP_OBJECT_ID_BYTES; i++)
            if (id->bytes[i]) return false;
        return id->bytes[0] == 0 ? allow_null : coop_object_read32(id->bytes + 1) != 0;
    }
    if (id->bytes[0] == 2) {
        uint8_t any = 0;
        for (unsigned i = 1; i < COOP_OBJECT_ID_BYTES; i++)
            any |= id->bytes[i];
        return any != 0;
    }
    return true;
}
size_t coop_object_element_size(uint8_t kind)
{
    switch (kind) {
    case COOP_VALUE_ARRAY32:
        return 4;
    case COOP_VALUE_ARRAY64:
        return 8;
    case COOP_VALUE_ARRAY_REFERENCE:
        return COOP_OBJECT_ID_BYTES;
    case COOP_VALUE_ARRAY_SCRIPT:
        return 12;
    case COOP_VALUE_ARRAY_QUEST:
        return 12;
    default:
        return 0;
    }
}
static bool valid_field(const CoopObjectField* field)
{
    const uint8_t* p = field->data;
    if (!field->field || !p || field->size > COOP_OBJECT_LIMIT) return false;
    switch (field->kind) {
    case COOP_VALUE_I32:
        return field->size == 4;
    case COOP_VALUE_I64:
        return field->size == 8;
    case COOP_VALUE_STRING:
        return field->size <= 4096 && !memchr(p, 0, field->size);
    case COOP_VALUE_REFERENCE: {
        CoopObjectId id;
        if (field->size != sizeof(id)) return false;
        memcpy(&id, p, sizeof(id));
        return coop_object_id_valid(&id, true);
    }
    default:
        break;
    }
    size_t width = coop_object_element_size(field->kind);
    if (!width || field->size < 4) return false;
    uint32_t count = coop_object_read32(p);
    if (count > COOP_OBJECT_ARRAY_LIMIT || field->size != 4 + count * (4 + width)) return false;
    uint32_t previous = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* value = p + 4 + i * (4 + width);
        uint32_t key = coop_object_read32(value);
        if (key > COOP_OBJECT_KEY_LIMIT || (i && key <= previous)) return false;
        previous = key;
        if (field->kind == COOP_VALUE_ARRAY_REFERENCE) {
            CoopObjectId id;
            memcpy(&id, value + 4, sizeof(id));
            if (!coop_object_id_valid(&id, true)) return false;
        }
    }
    return true;
}
bool coop_object_state_decode(const void* bytes, size_t size, CoopObjectState* out)
{
    const uint8_t* p = bytes;
    if (!p || !out || size < OBJECT_HEADER || size > COOP_OBJECT_LIMIT || memcmp(p, "ACO1", 4)) return false;
    CoopObjectState state = { 0 };
    memcpy(&state.id, p + 4, sizeof(state.id));
    memcpy(&state.prototype, p + 21, sizeof(state.prototype));
    state.type = p[38];
    state.options = p[41];
    state.count = (uint16_t)((p[39] << 8) | p[40]);
    if (state.options > COOP_OBJECT_SEPARATE_PC_POSE || (state.options && state.type != 15) || p[42] || state.type > 17 || state.count > COOP_OBJECT_FIELDS
        || !coop_object_id_valid(&state.id, false) || state.id.bytes[0] == 1
        || !coop_object_id_valid(&state.prototype, false) || (state.prototype.bytes[0] != 1 && state.prototype.bytes[0] != 2)) return false;
    size_t at = OBJECT_HEADER;
    for (unsigned i = 0; i < state.count; i++) {
        if (size - at < FIELD_HEADER) return false;
        CoopObjectField* f = &state.fields[i];
        f->field = (uint16_t)((p[at] << 8) | p[at + 1]);
        f->kind = p[at + 2];
        f->size = coop_object_read32(p + at + 3);
        at += FIELD_HEADER;
        if (f->size > size - at || (i && f->field <= state.fields[i - 1].field)) return false;
        f->data = p + at;
        if (!valid_field(f)) return false;
        at += f->size;
    }
    if (at != size) return false;
    *out = state;
    return true;
}
uint8_t* coop_object_state_encode(const CoopObjectState* state, size_t* size)
{
    if (!state || !size || state->count > COOP_OBJECT_FIELDS) return NULL;
    size_t length = OBJECT_HEADER;
    for (unsigned i = 0; i < state->count; i++) {
        const CoopObjectField* f = &state->fields[i];
        if (!valid_field(f) || f->size > COOP_OBJECT_LIMIT - FIELD_HEADER || length > COOP_OBJECT_LIMIT - FIELD_HEADER - f->size
            || (i && f->field <= state->fields[i - 1].field)) return NULL;
        length += FIELD_HEADER + f->size;
    }
    uint8_t* bytes = calloc(1, length);
    if (!bytes) return NULL;
    memcpy(bytes, "ACO1", 4);
    memcpy(bytes + 4, &state->id, sizeof(state->id));
    memcpy(bytes + 21, &state->prototype, sizeof(state->prototype));
    bytes[38] = state->type;
    bytes[39] = (uint8_t)(state->count >> 8);
    bytes[40] = (uint8_t)state->count;
    bytes[41] = state->options;
    size_t at = OBJECT_HEADER;
    for (unsigned i = 0; i < state->count; i++) {
        const CoopObjectField* f = &state->fields[i];
        bytes[at] = (uint8_t)(f->field >> 8);
        bytes[at + 1] = (uint8_t)f->field;
        bytes[at + 2] = f->kind;
        coop_object_write32(bytes + at + 3, f->size);
        at += FIELD_HEADER;
        memcpy(bytes + at, f->data, f->size);
        at += f->size;
    }
    CoopObjectState checked;
    if (!coop_object_state_decode(bytes, length, &checked)) {
        free(bytes);
        return NULL;
    }
    *size = length;
    return bytes;
}
