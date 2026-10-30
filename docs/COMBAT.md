# Campaign combat: authoritative turns and basic attacks

Protocol54 adds a replicated native turn-order panel (NOW, round and AP),
clickable human target portraits and host-controlled debugging immortality.
The host can leave turn-based combat while dead or off-turn. See
[party controls](PROTOCOL54_FEATURES.md) for controls and current limits.

Forced attacks now approach a reachable position with a valid native attack
trace. Previously, a diagonal approach could stop behind a wall, then fail a
path check onto the locked door or solid container itself. The correction uses
the same obstruction rules for planning and the final attack, without moving
objects during planning or bypassing locks, damage, weapon wear or action costs.
The retained Schuyler continuation demonstrates an actual Wine guest's forced
attack destroying the original door on the Linux host, with all four engines
then sharing the original estate assignment and save. See delivery 3 evidence
and [STATUS.md](STATUS.md) for the continuation's controlled-stat qualification.

Protocol 12 introduced host-authoritative combat stance, timing, turn ending and basic
attacks to the retained four-player runtime. Native combat calculates movement,
AP consumption, hit rolls and damage. Clients render the resulting object and
combat state without running their own combat simulation. Full campaign combat
acceptance is still pending; the controlled encounter below is only one fixture.
Protocol 13 now carries called shots and per-intent attack modes; equipped-weapon
and ammunition behavior is described in `WEAPONS.md`.
Protocol 14 adds ordered spellcasting and maintained-spell cancellation through
the original spell UI; see `MAGIC.md`. Native spell goals spend AP and fatigue
on the host, independently of the accepted-intent receipt.

## Cooperative allies

Native faction/AI checks recognize registered humans and their follower trees
across ownership. Companions defend the party and grenade AI avoids protected
humans in its blast area. Native leader changes update an NPC subtree's allegiance.
Dynamite notification keeps unrelated bystander reactions without turning another
human's companions against its placer. Native damage and explicit forced attacks
retain their existing behavior. Controlled native defense, hierarchy and
single-player-boundary checks pass on all three builds; actual grenade throws and
longer allied combat remain open. See `ENCOUNTERS.md` for the accepted scope.

Interaction approaches first try the normal route around bodies. If that
fails, the authoritative host can plan a bounded prefix past eligible idle
companions, including one beyond the first edge. The returned prefix is checked
with ordinary collisions and its first blocking NPC must admit the native
PleaseMove animation. Actual movement still checks ordinary collisions. This
applies to registered human requests outside active turn-based combat, including
combat stance. Humans and unrelated NPCs remain obstacles; a targeted NPC is
never asked to yield. Fighting, fleeing and surrendered companions retain native behavior. Native
three-build collision controls and the original Windows dungeon stair transfer
cover the demonstrated case; broader crowd/turn-based variants remain open.

## Controls and execution

R and the native combat button request stance changes. E and the native AP/end
button request ending the owned PC's turn. Space requests the original timing
switch; only the host can change the shared turn-based/real-time policy. Clicking
a living NPC while armed requests a basic attack; Alt-click requests a forced
attack, including an eligible party character or destructible object. Ordinary
unarmed NPC clicks still use the shared conversation service.
Comma selects head, period selects leg, and slash selects arm. Shift-click uses
a single native attempt without approaching. Ordinary clicks capture the local
auto-attack choice for that intent; clients do not inherit the host's repeat
choice. Native automatic defense still follows the host's shared engine setting.

Every request carries epoch, sequence, map, owned actor GUID and expected turn
generation. A message handler only stages it. The host poll validates membership,
ownership, map, living/available actor, current turn, native animation state and
target before invoking native combat/animation APIs. Native weapon range sets the
approach bound, with a minimum 30-tile limit; unrestricted distant targeting is
refused. Turn ending and leaving stance wait for the actor's native fidget/idle
state so an attack's atomic animation cannot be cut short by an end-turn request.
Entering stance during another PC's turn does not grant an action.

The service permits one outstanding request per player, at most 30 new requests
per second. Sequences increase strictly by one. Exact replay of the last request
reuses its receipt; changing a payload with the same sequence fails the session.
Every receipt waits for a fresh host world capture, and the client releases its
pending control only after applying that world revision. Attack success means
native intent acceptance; hit/impact and damage follow asynchronously. Refusals
appear in the original HUD. Clients do not optimistically spend AP or mutate HP.
Movement, stopping, talking and inventory requests also carry the turn generation,
so a request delayed across a turn boundary cannot act in a later turn. Loot
closure may release an access lease regardless of turn.

## Combat state on replicas

The 37-byte `ACB1` record is appended to canonical `ACS2` campaign state inside
the ordered world batch. It publishes timing policy, active turn-based state,
AP, round, generation and a stable current-actor ObjectID. Actor resolution waits
until staged objects exist and rejects actors removed by the same batch.

Replica native combat getters read a presentation state. Only the owned current
actor sees available AP; everyone else sees an empty bar. UI callbacks refresh
when the host changes mode/turn/AP. Loading a native save during active combat
reads its actor/AP/round into this presentation state without constructing native
initiative, starting AI or running combat timers. The next world batch supplies
the authoritative generation. Direct replica AP consumption, damage, attack,
stance and turn-ending mutations are guarded. This serializer test does not
implement campaign save controls, reconnect or active-combat recovery.

Ordinary activity, combat XP, dead controllers and the original-enemy fixture
are described in `ENCOUNTERS.md`.

## Host controller rules

With an active campaign registry and the explicit `COOP_AUTHORITY_HOST` role,
all registered PCs use the native human action-point rules. Previously only the
process's local PC could overexert: spending the last positive AP balance on a
more expensive action costs two fatigue. Remote registered PCs now have the
same eligibility and cost. The host's own AP bar continues to show its own turn;
another player's turn shows zero available AP on that host display.

Remote PC turns use native human turn timing and do not execute the NPC
end-combat script hook. Unregistered NPCs keep native AI/script behavior.
Outside the cooperative host role, the original local-PC rules remain in use.

Any registered PC entering combat can start native turn-based combat, including
a PC in the native concealed fidget state. During an
existing turn, another player entering combat stance does not steal that turn
or replenish its AP. A player leaving stance keeps the shared turn-based session
while another registered PC remains in combat stance; the last one leaving ends
the session. The network service also enforces native stance-exit eligibility,
turn ownership and animation completion.

## Initiative and sectors

Initiative collects the union of native perception regions around all active
registered PCs, rather than only the host's vicinity. Native sorting, NPC turns,
round progression and AP costs remain in the engine. Each PC's region uses at
least ten tiles, clips to the current map and caps at 64 tiles, the maximum
radius supported by the native query's three sectors per axis.

Each region retains a separate `ObjectList` and its sector locks. Four regions
can exceed one native list's nine-sector capacity. The initiative list receives
deduplicated object nodes; locks remain with the region lists and are released
at combat end/reset. Refresh acquires a replacement before releasing that
region's old locks. `object_list_init_empty` initializes the node-only aggregate
using the native list accounting contract.

## Dialogue teardown

The combat fixture's NPC heartbeat starts Virgil's real opening conversation.
This exposed a shutdown crash: `dialog_ui_exit` stopped speech but left active
entries pointing at dialogue files subsequently freed by `dialog_exit`. Later
map-object destruction attempted to unload the dead entry. UI exit now releases
active entries while the UI and dialogue storage are still alive. This does not
execute another dialogue choice or campaign script.

## Resource cleanup exposed by actual attacks

The native target picker allocated a nearby-critter list and never released it;
it also iterated the wrong list when excluding dead targets. It now uses and
releases the correct lists, including party filters. Silent SFX message entries
no longer resolve to the sound directory, archived directories cannot open as
files, and the sound file cache rejects nonpositive lengths and incomplete reads
before publishing an entry. These fixes came from whole-engine sanitizer runs of
real native attacks. Temporary allocation tracing was removed before acceptance.

## Verification and remaining work

`tests/integration_combat_engine.py` runs the native controller fixture on Linux,
Windows/Wine and whole-engine ASan/UBSan. Four PCs forty tiles apart must all enter
initiative and pay equal native overexertion cost. It covers remote concealed
stance, preserving another actor's turn, shared stance exit, real-time stance,
and the ordinary local-role AP boundary. It also saves/loads active combat through
the native combat serializer in replica role, installs the exact authoritative
state and verifies mutation guards. Empty/missing/directory cache entries must
fail without poisoning a subsequent valid read. Three composed frames per build
show AP ownership and combat ending; paused NPC floaters are diagnostic only.
The fixture draws an ordinary initial world frame before invoking combat APIs,
so pre-frame native bounds caches do not give misleading sprite sizes.

`tests/integration_bootstrap.py --mode runtime-combat` launches four actual game
engines over TCP with two Linux and two Windows executables. The host direction
is reversed for a second run; one Linux engine uses whole-engine sanitizers.
A controlled crash-site fixture removes unrelated active quest NPCs, creates a
script-free paralyzed wolf with high HP, and equips all PCs for native unarmed
melee. The first 20 stages cover remote combat start, all four native R/click/E
turns, all four real-time attacks, damage replication, forged ownership, stale
turns, host-only timing, target/distance refusals, stop controls and exact replay.
Stages 21–24 submit movement, stop and inventory requests from deliberately
stale replica presentation state, then a fresh inventory request. These are
ordinary request APIs and real transport messages, not a replacement combat
simulation.

The loss variant disconnects the host during an active remote turn. The replay
variant changes the payload of an already-receipted denial. Both must freeze
campaign/combat clocks and reject further input without normal finish. Retained
24-stage evidence is `evidence/combat-wire-generation-final`; the earlier
20-stage run is `combat-wire-final`. Failure variants are
`combat-wire-loss-final` and `combat-wire-replay-final`. Diagnostic `node-debug`
runs retain pointers for tracing and are not leak-check acceptance evidence.

This controlled fixture does not prove broad hostile-encounter coverage or
ranged attacks/ammunition,
aimed attacks, spells/skills, combat sounds/floaters, full death/resurrection and
follower policy, combat travel/save/recovery or campaign-wide encounters. The
original-enemy and shared combat-XP coverage is separate in `ENCOUNTERS.md`.
Physical Windows remains untested.
The artificial high-HP wolf can have negative native fatigue percentages after
repeated hits; this is not acceptance of an ordinary encounter HUD.
Desktop combat gestures also need dedicated acceptance; composed TIG framebuffer
captures do not establish OS input delivery. Whole-campaign completion remains
false. See `STATUS.md` for current build/evidence records.

## Native campaign options (protocol39)

The original Options combat-mode selector now submits the existing host-authorized
policy request; clients cannot change the campaign by mutating local settings.
Policy values0/1/2 choose real-time, standard turn-based and fast turn-based. Fast
timing and difficulty persist in the campaign environment state. Auto-attack and
weapon switching use each human's authenticated saved preferences. New settings
integration awaits native runtime acceptance.

## Human animation cost parity (protocol41)

Native animation completion now treats all campaign humans as player controlled.
The extra NPC completion charge and required-cost bookkeeping no longer apply
only because a human is remote. Individual native action costs, NPC turns and
fatigue overspend rules remain authoritative. Basic/technical skill difficulty
adjustments also apply to each human. Current-build runtime acceptance is pending.
