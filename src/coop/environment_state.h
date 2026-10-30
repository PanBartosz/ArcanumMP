#ifndef ARCMP_ENVIRONMENT_STATE_H
#define ARCMP_ENVIRONMENT_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { COOP_ENVIRONMENT_LEGACY_BYTES = 52,
    COOP_ENVIRONMENT_V2_BYTES = 92, COOP_ENVIRONMENT_BYTES = 96 };
typedef struct CoopEnvironmentState {
    uint32_t light_scheme, light_default, hour, indoor, outdoor;
    uint32_t schemes[2], after_combat[2], after_song[2];
    bool combat, pending_song, scripted_sound;
    uint8_t combat_track;
    uint32_t sound_revision;
    uint8_t sound_members;
    uint32_t sound_origins[4][2];
    uint8_t difficulty, owner_options[4];
    bool fast_turn_based;
} CoopEnvironmentState;
bool coop_environment_valid(const CoopEnvironmentState* state);
bool coop_environment_encode(const CoopEnvironmentState* state, uint8_t bytes[COOP_ENVIRONMENT_BYTES]);
bool coop_environment_decode(const void* bytes, size_t size, CoopEnvironmentState* out);
#endif
