#include "coop/dialogue.h"
#include <stdlib.h>
#include <string.h>
enum { HEADER = 97 };
static bool text_valid(const char* text, size_t capacity)
{
    for (size_t i = 0; i < capacity; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (!ch) return true;
        if (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') return false;
    }
    return false;
}
bool coop_dialogue_view_valid(const CoopDialogueView* v)
{
    if (!v || !v->conversation || !v->revision || v->chooser >= COOP_MAX_PLAYERS
        || v->mode > COOP_DIALOGUE_FLOAT || v->option_count > COOP_DIALOGUE_OPTIONS || v->speech < -1
        || (!v->active && v->option_count) || (v->mode == COOP_DIALOGUE_FLOAT && (v->active || v->option_count || !v->reply[0])) || !coop_object_id_valid(&v->npc, false)
        || (v->npc.bytes[0] != 2 && v->npc.bytes[0] != 3)
        || !text_valid(v->chooser_name, sizeof(v->chooser_name)) || !v->chooser_name[0]
        || !text_valid(v->reply, sizeof(v->reply)) || !text_valid(v->selected, sizeof(v->selected))) return false;
    bool pc = false;
    for (unsigned i = 0; i < 16; i++) pc |= v->pc_guid[i] != 0;
    if (!pc) return false;
    for (unsigned i = 0; i < v->option_count; i++)
        if (!text_valid(v->options[i], sizeof(v->options[i])) || !v->options[i][0]) return false;
    return true;
}
uint8_t* coop_dialogue_view_encode(const CoopDialogueView* v, size_t* size)
{
    if (!size || !coop_dialogue_view_valid(v)) return NULL;
    size_t length = HEADER + 4 + strlen(v->reply) + strlen(v->selected);
    for (unsigned i = 0; i < v->option_count; i++) length += 2 + strlen(v->options[i]);
    uint8_t* p = calloc(1, length);
    if (!p) return NULL;
    memcpy(p, "ACD1", 4);
    coop_object_write32(p + 4, v->conversation);
    coop_object_write32(p + 8, v->revision);
    coop_object_write32(p + 12, v->active);
    coop_object_write32(p + 16, v->mode);
    coop_object_write32(p + 20, v->chooser);
    memcpy(p + 24, v->pc_guid, 16);
    memcpy(p + 40, v->npc.bytes, COOP_OBJECT_ID_BYTES);
    coop_object_write32(p + 57, (uint32_t)v->speech);
    coop_object_write32(p + 61, v->option_count);
    memcpy(p + 65, v->chooser_name, strlen(v->chooser_name));
    size_t at = HEADER;
    for (unsigned i = 0; i < v->option_count + 2; i++) {
        const char* text = i == 0 ? v->reply : i == 1 ? v->selected : v->options[i - 2];
        size_t n = strlen(text);
        p[at++] = (uint8_t)(n >> 8);
        p[at++] = (uint8_t)n;
        memcpy(p + at, text, n);
        at += n;
    }
    *size = length;
    return p;
}
bool coop_dialogue_view_decode(const void* data, size_t size, CoopDialogueView* out)
{
    const uint8_t* p = data;
    if (!p || !out || size < HEADER + 4 || size > COOP_DIALOGUE_LIMIT || memcmp(p, "ACD1", 4)
        || coop_object_read32(p + 12) > 1 || coop_object_read32(p + 20) >= COOP_MAX_PLAYERS) return false;
    CoopDialogueView v = { 0 };
    v.conversation = coop_object_read32(p + 4);
    v.revision = coop_object_read32(p + 8);
    v.active = coop_object_read32(p + 12) != 0;
    v.mode = coop_object_read32(p + 16);
    v.chooser = (uint8_t)coop_object_read32(p + 20);
    memcpy(v.pc_guid, p + 24, 16);
    memcpy(v.npc.bytes, p + 40, COOP_OBJECT_ID_BYTES);
    v.speech = (int32_t)coop_object_read32(p + 57);
    v.option_count = coop_object_read32(p + 61);
    if (v.option_count > COOP_DIALOGUE_OPTIONS || !memchr(p + 65, 0, COOP_NAME_SIZE)) return false;
    memcpy(v.chooser_name, p + 65, COOP_NAME_SIZE);
    /* Require canonical zero padding after the name's terminator. */
    for (size_t i = strlen(v.chooser_name); i < COOP_NAME_SIZE; i++) if (p[65 + i]) return false;
    size_t at = HEADER;
    for (unsigned i = 0; i < v.option_count + 2; i++) {
        if (size - at < 2) return false;
        size_t n = ((size_t)p[at] << 8) | p[at + 1];
        at += 2;
        if (n >= COOP_DIALOGUE_TEXT || n > size - at || memchr(p + at, 0, n)) return false;
        memcpy(i == 0 ? v.reply : i == 1 ? v.selected : v.options[i - 2], p + at, n);
        at += n;
    }
    if (at != size || !coop_dialogue_view_valid(&v)) return false;
    *out = v;
    return true;
}
