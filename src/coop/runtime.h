#ifndef ARCMP_RUNTIME_H
#define ARCMP_RUNTIME_H
#include "coop/gameplay.h"
#include "game/teleport.h"
#include "coop/controls.h"
#include "coop/presentation.h"
#include "coop/save_catalog.h"
#include "tig/window.h"
typedef enum CoopRuntimePhase {
    COOP_RUNTIME_OFF,
    COOP_RUNTIME_LOBBY,
    COOP_RUNTIME_PREPARING,
    COOP_RUNTIME_LOADING,
    COOP_RUNTIME_STARTING,
    COOP_RUNTIME_PLAYING,
    COOP_RUNTIME_FAILED,
    COOP_RUNTIME_DRAINING,
    COOP_RUNTIME_RESTING,
    COOP_RUNTIME_JOURNEY,
    COOP_RUNTIME_RECONNECTING,
} CoopRuntimePhase;
/* One engine-thread controller. The lobby retains ownership of the session and
 * polls it before tick(); all native load/start operations run outside input
 * and socket callbacks. Detach before destroying the borrowed session. */
bool coop_runtime_attach(CoopSession* session, const uint8_t owner[COOP_IDENTITY_SIZE]);
bool coop_runtime_start(void);
bool coop_runtime_start_checkpoint(void);
bool coop_runtime_recover_checkpoint(void);
bool coop_runtime_checkpoint_exists(void);
bool coop_runtime_recovery_exists(void);
CoopSaveCatalog* coop_runtime_save_catalog(void);
bool coop_runtime_save_set(CoopSaveSet* party);
bool coop_runtime_save_message(uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_runtime_checkpoint_request(CoopControlAction action, const CoopSaveEntry* entry, const char* description);
bool coop_runtime_resume_slot(const CoopSaveEntry* entry);
CoopCheckpointStatus coop_runtime_remove_slot(const CoopSaveEntry* entry);
bool coop_runtime_native_save_allowed(void);
bool coop_runtime_pretravel_save(void);
bool coop_runtime_native_load_allowed(void);
CoopControls* coop_runtime_controls(void);
CoopSession* coop_runtime_session(void);
bool coop_runtime_control(CoopControlAction action, unsigned value, uint32_t proposal, int64_t bed);
bool coop_runtime_control_message(uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_runtime_paused(void);
/* Host script/UI presentation calls are handled synchronously while the
 * presentation pump keeps transport alive without advancing native simulation. */
bool coop_runtime_present(CoopPresentKind kind, unsigned id, const char* path, unsigned flags, unsigned track);
bool coop_runtime_present_message(uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_runtime_present_active(void);
bool coop_runtime_fade(const FadeData* fade);
int coop_runtime_present_pump(void);
bool coop_runtime_present_input(void);
void coop_runtime_end_campaign(bool defeated);
CoopPresentation* coop_runtime_presentation(void);
const char* coop_runtime_present_status(void);
void coop_runtime_present_observer(void (*observer)(void* context), void* context);
bool coop_runtime_pause_toggle(void);
/* Native confirmation dialogs keep transport/simulation alive and cancel when
 * a shared operation must replace their underlying screen. */
bool coop_runtime_modal_tick(TigWindowModalDialogChoice* choice);
bool coop_runtime_fate(int64_t actor, unsigned fate, bool activate);
bool coop_runtime_difficulty(unsigned value);
bool coop_runtime_player_options(unsigned mask);
bool coop_runtime_player_option(unsigned option, bool value);
bool coop_runtime_note(const CoopNoteRequest* request);
bool coop_runtime_magic_choice(CoopMagicRequest* request);
bool coop_runtime_commit_spell_destination(int64_t actor, int64_t location, unsigned spell, bool charge);
bool coop_runtime_reconnect_message(uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_runtime_retry_now(void);
void coop_runtime_tick(uint64_t now);
/* Returns whether a campaign was entered and needs a native menu reset. */
bool coop_runtime_detach(void);
void coop_runtime_before_reset(void);
CoopRuntimePhase coop_runtime_phase(void);
const char* coop_runtime_status(void);
const char* coop_runtime_autosave_status(void);
/* A native map replacement cannot safely continue after its cache fails.
 * Freeze the active campaign and retain its last durable recovery checkpoint. */
void coop_runtime_native_failure(const char* message);
bool coop_runtime_blocks_simulation(void);
CoopGameplay* coop_runtime_gameplay(void);
bool coop_runtime_move(int64_t location, bool run);
bool coop_runtime_move_route(const int64_t* locations, unsigned count, bool run);
bool coop_runtime_journey(int64_t destination);
bool coop_runtime_journey_route(const int64_t* destinations, unsigned count);
bool coop_runtime_follower_order(int64_t follower, CoopFollowerOrder order, int64_t target, int64_t location, bool force);
bool coop_runtime_broadcast(int64_t actor, const char* command, int64_t target, int64_t location);
bool coop_runtime_bed(int64_t bed);
bool coop_runtime_talk(int64_t npc);
bool coop_runtime_halt(int64_t towards);
bool coop_runtime_combat(CoopCombatOperation operation, uint8_t value, int64_t target);
bool coop_runtime_cast(int64_t source, int spell, const TargetDescriptor* target, bool force);
bool coop_runtime_use_item(int64_t item, const TargetDescriptor* target, bool force);
bool coop_runtime_skill(CoopSkillAction action, uint8_t value, int64_t target);
bool coop_runtime_skill_feedback(int64_t actor, unsigned message, bool red);
bool coop_runtime_trap_feedback(int64_t actor);
bool coop_runtime_native_feedback(int64_t actor, unsigned catalogue, unsigned message);
void coop_runtime_progression_editor(bool open);
/* Returns true when the active co-op session owns this native edit, including
 * a refused/busy request, so callers never fall through to a local mutation. */
bool coop_runtime_progression_change(int64_t actor, CoopProgressionKind kind, unsigned index, bool increase);
bool coop_runtime_service_item(int64_t item, bool buy);
bool coop_runtime_service_quantity(int64_t item, unsigned quantity, bool buy);
bool coop_runtime_craft(unsigned recipe, int64_t crafter);
void coop_runtime_service_close(void);
/* Returns true whenever a co-op session owns this native presentation call. */
bool coop_runtime_written(int64_t reader, unsigned type, int number, bool vendigroth);
bool coop_runtime_cancel_spell(uint64_t token);
/* Called only by the native teleport entry point: 0 uses its normal queue,
 * 1 was deferred behind the campaign barrier, -1 rejects an unavailable trip. */
int coop_runtime_defer_teleport(const TeleportData* teleport);
uint32_t coop_runtime_epoch(void);
bool coop_runtime_item(CoopItemOperation operation, int64_t item, uint8_t recipient, int location);
bool coop_runtime_native_loot(int64_t pc, int64_t target);
bool coop_runtime_world_use(int64_t target);
bool coop_runtime_world_examine(int64_t target);
bool coop_runtime_native_world_used(int64_t actor, int64_t target, const AnimID* goal);
bool coop_runtime_native_world_allowed(int64_t actor, int64_t target, const AnimID* goal);
bool coop_runtime_native_talk_allowed(int64_t actor, int64_t target, const AnimID* goal);
/* Arrival tiles remain armed in the checkpoint until every engine has bound
 * the new gameplay epoch. Their original actions then run on the host. */
bool coop_runtime_defer_tile(int64_t location, int64_t triggerer);
bool coop_runtime_entry_pending(void);
bool coop_runtime_arrival_event(TimeEvent* event);
/* Explicit integration-harness opt-in; writes private runtime state/view files. */
void coop_runtime_probe_enable(void);
void coop_runtime_probe_retry_delay(uint32_t milliseconds);
bool coop_runtime_probe_autosave(bool travelled);
#endif
