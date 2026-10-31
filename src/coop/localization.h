#ifndef ARCMP_LOCALIZATION_H
#define ARCMP_LOCALIZATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* References identify text only. Conditions, reply choices and effects always
 * come from the shared original campaign, never from a language pack. */
#define COOP_TEXT_PATH 96
typedef struct CoopTextRef {
    char path[COOP_TEXT_PATH];
    int32_t key;
    uint8_t variant;
} CoopTextRef;
typedef bool (*CoopLanguageRead)(const char* path, char** data, size_t* size);
void coop_language_init(void);
void coop_language_reader(CoopLanguageRead reader);
unsigned coop_language_count(void);
const char* coop_language_name(unsigned index);
unsigned coop_language_pending(void);
bool coop_language_available(unsigned index);
bool coop_language_select(unsigned index);
const char* coop_language_active(void);
const char* coop_tr(const char* text);
/* Canonical host log text is translated only when presented locally. */
const char* coop_tr_rendered(const char* text);
/* Cached native names may already be localized. Shared event producers use
 * their original resource text; local presentation resolves the same key. */
const char* coop_language_original(const char* text);
void coop_language_canonical_begin(void);
void coop_language_canonical_end(void);
const char* coop_language_message(const char* path, int key, const char* original);
void coop_language_source(const char* pointer, const char* path, int key, unsigned variant);
void coop_language_record(const char* result, const char* source);
void coop_language_remember(const char* result, const CoopTextRef* reference);
CoopTextRef coop_language_reference(const char* result);
bool coop_language_resolve(const CoopTextRef* ref, const char* pc, const char* npc, const char* rendered, char* out, size_t capacity);
bool coop_language_asset(const char* path, char* out, size_t capacity);
bool coop_language_ref_valid(const CoopTextRef* ref);
#ifdef __cplusplus
}
#endif
#endif
