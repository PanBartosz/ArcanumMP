#ifndef ARCMP_OBJECT_STATE_H
#define ARCMP_OBJECT_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COOP_OBJECT_LIMIT (256u * 1024u)
#define COOP_OBJECT_FIELDS 512
#define COOP_OBJECT_ARRAY_LIMIT 4096
#define COOP_OBJECT_KEY_LIMIT 65535
#define COOP_OBJECT_ID_BYTES 17
typedef struct CoopObjectId {
    uint8_t bytes[COOP_OBJECT_ID_BYTES];
} CoopObjectId;
enum CoopObjectValue {
    COOP_VALUE_I32 = 1,
    COOP_VALUE_I64,
    COOP_VALUE_STRING,
    COOP_VALUE_REFERENCE,
    COOP_VALUE_ARRAY32,
    COOP_VALUE_ARRAY64,
    COOP_VALUE_ARRAY_REFERENCE,
    COOP_VALUE_ARRAY_SCRIPT,
    COOP_VALUE_ARRAY_QUEST,
};
typedef struct CoopObjectField {
    uint16_t field;
    uint8_t kind;
    uint32_t size;
    const uint8_t* data;
} CoopObjectField;
typedef struct CoopObjectState {
    CoopObjectId id;
    CoopObjectId prototype;
    uint8_t type;
    uint8_t options;
    uint16_t count;
    CoopObjectField fields[COOP_OBJECT_FIELDS];
} CoopObjectState;
#define COOP_OBJECT_SEPARATE_PC_POSE 1
/* IDs encode kind byte, then zero/padded prototype u32, GUID16, or positional
 * x/y/temp/map u32. All integers are big-endian. No native structs/padding. */
bool coop_object_id_valid(const CoopObjectId* id, bool allow_null);
bool coop_object_state_decode(const void* bytes, size_t size, CoopObjectState* out);
uint8_t* coop_object_state_encode(const CoopObjectState* state, size_t* size);
size_t coop_object_element_size(uint8_t kind);
uint32_t coop_object_read32(const uint8_t* p);
uint64_t coop_object_read64(const uint8_t* p);
void coop_object_write32(uint8_t* p, uint32_t value);
void coop_object_write64(uint8_t* p, uint64_t value);
#endif
