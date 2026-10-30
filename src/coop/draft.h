#ifndef ARCMP_DRAFT_H
#define ARCMP_DRAFT_H
#include "coop/character.h"
#include "coop/identity.h"
#define COOP_DRAFT_BYTES (60 + COOP_CHARACTER_BYTES)
#define COOP_DRAFT_SHARE_BYTES (44 + COOP_CHARACTER_BYTES)
#define COOP_DRAFT_SLOTS 8
typedef enum CoopDraftStatus {
    COOP_DRAFT_OK,
    COOP_DRAFT_MISSING,
    COOP_DRAFT_IO,
    COOP_DRAFT_INVALID,
    COOP_DRAFT_CONTENT,
    COOP_DRAFT_PROFILE,
} CoopDraftStatus;
/* Atomic file replacement after flushing a private, exclusively created temp
 * file. This stores a recipe; native legality still requires engine replay. */
bool coop_draft_save(const char* path, const uint8_t content[32], const uint8_t owner[COOP_IDENTITY_SIZE], const CoopCharacter* character);
CoopDraftStatus coop_draft_load(const char* path, const uint8_t content[32], const uint8_t owner[COOP_IDENTITY_SIZE], CoopCharacter* character);
/* Slots 1–8 are named builds; zero is the compatible last accepted build. */
bool coop_draft_slot_path(char* out, size_t size, const char* canonical, unsigned slot);
bool coop_draft_remember(const char* canonical, const uint8_t content[32], const uint8_t owner[16], const CoopCharacter* character, unsigned* slot);
bool coop_draft_export(const char* path, const uint8_t content[32], const CoopCharacter* character);
CoopDraftStatus coop_draft_import(const char* path, const uint8_t content[32], CoopCharacter* character);
bool coop_draft_delete(const char* canonical, unsigned slot);
#endif
