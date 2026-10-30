#ifndef ARCMP_GAMEPLAY_H
#define ARCMP_GAMEPLAY_H
#include "coop/session.h"
#include "coop/dialogue.h"
#include "coop/items.h"
#include "coop/battle.h"
#include "coop/magic.h"
#include "coop/skills.h"
#include "coop/progression.h"
#include "coop/services.h"
#include "coop/followers.h"
#include "coop/notes.h"
#include "coop/engine_audio.h"
#include "game/target.h"
enum { COOP_GAME_QUIESCE = 74, COOP_GAME_QUIESCED = 75 };
enum { COOP_MOVE_WAYPOINTS = 6 };
typedef struct CoopGameplay CoopGameplay;
/* Initial action/state adapter. Only the host executes movement goals. Clients
 * apply authoritative PC poses, objects, story state and displayed clocks.
 * Campaign simulation advances only on the host. Shared retail dialogue uses
 * chooser/observer views. Discrete owned-item requests use ordered receipts.
 * Combat, spell and NPC-service intentions also use ordered host execution. */
CoopGameplay* coop_gameplay_create(CoopSession* session, uint32_t epoch);
void coop_gameplay_destroy(CoopGameplay* gameplay);
/* Native callbacks can suspend inside media/rejoin. Keep retired services
 * allocated until every borrowing call has returned. */
void coop_gameplay_retain(CoopGameplay* gameplay);
void coop_gameplay_release(CoopGameplay* gameplay);
void coop_gameplay_poll(CoopGameplay* gameplay, uint64_t now_ms);
bool coop_gameplay_running(const CoopGameplay* gameplay);
/* Public connection-to-campaign binding. This is presentation data, not an
 * owner credential or permission to submit another character's actions. */
int coop_gameplay_member_for_connection(const CoopGameplay* gameplay, unsigned connection);
void coop_gameplay_set_paused(CoopGameplay* gameplay, bool paused);
bool coop_gameplay_failed(const CoopGameplay* gameplay);
const char* coop_gameplay_error(const CoopGameplay* gameplay);
bool coop_gameplay_move(CoopGameplay* gameplay, int64_t location, bool run);
bool coop_gameplay_move_route(CoopGameplay* gameplay, const int64_t* locations, unsigned count, bool run);
/* Explicit actor form used by the input adapter. The host validates it against
 * the sending connection's persisted owner; callers cannot grant ownership. */
bool coop_gameplay_move_actor(CoopGameplay* gameplay, const uint8_t actor_guid[16], int64_t location, bool run);
bool coop_gameplay_move_route_actor(CoopGameplay* gameplay, const uint8_t actor_guid[16], const int64_t* locations, unsigned count, bool run);
bool coop_gameplay_talk(CoopGameplay* gameplay, int64_t npc);
bool coop_gameplay_talk_actor(CoopGameplay* gameplay, const uint8_t actor_guid[16], CoopObjectId npc);
bool coop_gameplay_halt(CoopGameplay* gameplay, int64_t towards);
uint32_t coop_gameplay_last_result(const CoopGameplay* gameplay, bool* accepted);
unsigned coop_gameplay_accepted(const CoopGameplay* gameplay);
unsigned coop_gameplay_rejected(const CoopGameplay* gameplay);
/* Diagnostic/session shutdown barrier: queue one final pose state and wait for
 * every client to acknowledge it before returning a successful finished state. */
bool coop_gameplay_finish(CoopGameplay* gameplay);
bool coop_gameplay_finished(const CoopGameplay* gameplay);
bool coop_gameplay_finishing(const CoopGameplay* gameplay);
bool coop_gameplay_draining_native(const CoopGameplay* gameplay);
/* Drain the current map before beginning the next snapshot epoch. */
bool coop_gameplay_transition(CoopGameplay* gameplay, uint32_t next_epoch);
uint32_t coop_gameplay_transition_epoch(const CoopGameplay* gameplay);
uint32_t coop_gameplay_world_revision(const CoopGameplay* gameplay);
uint64_t coop_gameplay_world_bytes(const CoopGameplay* gameplay);
uint64_t coop_gameplay_world_deletions(const CoopGameplay* gameplay);
CoopDialogue* coop_gameplay_dialogue(CoopGameplay* gameplay);
CoopItems* coop_gameplay_items(CoopGameplay* gameplay);
CoopServices* coop_gameplay_services(CoopGameplay* gameplay);
CoopFollowers* coop_gameplay_followers(CoopGameplay* gameplay);
CoopNotes* coop_gameplay_notes(CoopGameplay* gameplay);
bool coop_gameplay_item(CoopGameplay* gameplay, CoopItemOperation operation, int64_t item, uint8_t recipient, int location);
bool coop_gameplay_item_actor(CoopGameplay* gameplay, const uint8_t actor[16], CoopItemOperation operation, CoopObjectId item, uint8_t recipient, int location);
CoopBattle* coop_gameplay_battle(CoopGameplay* gameplay);
bool coop_gameplay_combat(CoopGameplay* gameplay, CoopCombatOperation operation, uint8_t value, int64_t target);
bool coop_gameplay_combat_actor(CoopGameplay* gameplay, const uint8_t actor[16], CoopCombatOperation operation, uint8_t value, CoopObjectId target, uint32_t generation);
CoopMagic* coop_gameplay_magic(CoopGameplay* gameplay);
CoopEngineAudio* coop_gameplay_audio(CoopGameplay* gameplay);
bool coop_gameplay_magic_request(CoopGameplay* gameplay, const CoopMagicRequest* request);
bool coop_gameplay_cast(CoopGameplay* gameplay, int64_t source, int spell, const TargetDescriptor* target, bool force);
bool coop_gameplay_use_item(CoopGameplay* gameplay, int64_t item, const TargetDescriptor* target, bool force);
bool coop_gameplay_written(CoopGameplay* gameplay, int64_t reader, unsigned type, int number, bool vendigroth);
bool coop_gameplay_cancel_spell(CoopGameplay* gameplay, uint64_t token);
CoopSkills* coop_gameplay_skills(CoopGameplay* gameplay);
bool coop_gameplay_skill_request(CoopGameplay* gameplay, const CoopSkillRequest* request);
bool coop_gameplay_skill(CoopGameplay* gameplay, CoopSkillAction action, uint8_t value, int64_t target);
bool coop_gameplay_native_talk_allowed(CoopGameplay* gameplay, int64_t actor, int64_t target, const AnimID* goal);
bool coop_gameplay_skill_feedback(CoopGameplay* gameplay, int64_t actor, unsigned message, bool red, unsigned catalogue);
CoopProgression* coop_gameplay_progression(CoopGameplay* gameplay);
#endif
