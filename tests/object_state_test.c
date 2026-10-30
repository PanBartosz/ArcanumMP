#include "coop/object_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                \
        }                                                           \
    } while (0)
int main(void)
{
    CoopObjectState state = { 0 }, decoded;
    state.id.bytes[0] = 2;
    state.id.bytes[1] = 0xAB;
    state.prototype.bytes[0] = 1;
    coop_object_write32(state.prototype.bytes + 1, 16067);
    state.type = 15;
    uint8_t scalar[8], array[4 + 2 * 8], reference[17] = { 3 }, script[20], quest[20];
    coop_object_write64(scalar, UINT64_C(0xFFEEDDCCBBAA9988));
    coop_object_write32(array, 2);
    coop_object_write32(array + 4, 0);
    coop_object_write32(array + 8, UINT32_MAX);
    coop_object_write32(array + 12, 900);
    coop_object_write32(array + 16, 42);
    for (unsigned i = 0; i < 4; i++)
        coop_object_write32(reference + 1 + 4 * i, 100 + i);
    coop_object_write32(script, 1);
    coop_object_write32(script + 4, 35);
    coop_object_write32(script + 8, 7);
    coop_object_write32(script + 12, 0x12345678);
    coop_object_write32(script + 16, 3071);
    memcpy(quest, script, sizeof(quest));
    state.fields[0] = (CoopObjectField) { 1, COOP_VALUE_I64, 8, scalar };
    state.fields[1] = (CoopObjectField) { 20, COOP_VALUE_STRING, 4, (const uint8_t*)"Hero" };
    state.fields[2] = (CoopObjectField) { 30, COOP_VALUE_ARRAY32, sizeof(array), array };
    state.fields[3] = (CoopObjectField) { 31, COOP_VALUE_REFERENCE, sizeof(reference), reference };
    state.fields[4] = (CoopObjectField) { 32, COOP_VALUE_ARRAY_SCRIPT, sizeof(script), script };
    state.fields[5] = (CoopObjectField) { 33, COOP_VALUE_ARRAY_QUEST, sizeof(quest), quest };
    state.count = 6;
    size_t size;
    uint8_t* bytes = coop_object_state_encode(&state, &size);
    CHECK(bytes && coop_object_state_decode(bytes, size, &decoded));
    CHECK(decoded.count == 6 && decoded.type == 15 && !memcmp(&decoded.id, &state.id, sizeof(state.id)));
    CHECK(!memcmp(decoded.fields[0].data, "\xFF\xEE\xDD\xCC\xBB\xAA\x99\x88", 8));
    CHECK(coop_object_read64(decoded.fields[0].data) == UINT64_C(0xFFEEDDCCBBAA9988));
    CHECK(coop_object_read32(decoded.fields[2].data + 12) == 900);
    for (unsigned i = 0; i < state.count; i++)
        CHECK(decoded.fields[i].size == state.fields[i].size && !memcmp(decoded.fields[i].data, state.fields[i].data, state.fields[i].size));
    CoopObjectState retained;
    memset(&decoded, 0x42, sizeof(decoded));
    retained = decoded;
    CHECK(!coop_object_state_decode(bytes, size - 1, &decoded) && !memcmp(&retained, &decoded, sizeof(decoded)));
    state.fields[2].data = array;
    coop_object_write32(array + 12, 0); // duplicate sparse key
    CHECK(!coop_object_state_encode(&state, &size));
    coop_object_write32(array + 12, COOP_OBJECT_KEY_LIMIT + 1u);
    CHECK(!coop_object_state_encode(&state, &size));
    coop_object_write32(array + 12, 900);
    state.fields[1].data = (const uint8_t*)"H\0ro";
    CHECK(!coop_object_state_encode(&state, &size));
    state.fields[1].data = (const uint8_t*)"Hero";
    state.fields[1].field = 1;
    CHECK(!coop_object_state_encode(&state, &size));
    state.fields[1].field = 20;
    state.options = COOP_OBJECT_SEPARATE_PC_POSE;
    size_t option_size;
    uint8_t* option = coop_object_state_encode(&state, &option_size);
    CHECK(option && coop_object_state_decode(option, option_size, &decoded) && decoded.options == COOP_OBJECT_SEPARATE_PC_POSE);
    free(option);
    state.type = 16;
    CHECK(!coop_object_state_encode(&state, &option_size));
    state.type = 15;
    state.options = 0;
    uint32_t random = 20261020;
    for (unsigned i = 0; i < 30000; i++) {
        random = random * 1664525u + 1013904223u;
        size_t at = random % size;
        uint8_t saved = bytes[at];
        bytes[at] ^= (uint8_t)(random >> 24);
        bool valid = coop_object_state_decode(bytes, size, &decoded);
        if (valid) {
            size_t encoded_size;
            uint8_t* encoded = coop_object_state_encode(&decoded, &encoded_size);
            CHECK(encoded && encoded_size == size && !memcmp(bytes, encoded, size));
            free(encoded);
        }
        bytes[at] = saved;
    }
    free(bytes);
    puts("PASS: explicit object IDs/fields/sparse values, golden byte order, staged rejection and malformed corpus");
    return 0;
}
