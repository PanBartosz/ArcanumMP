# Architecture decision, 2026-10-20

## Engine base

Use alexbatalov/arcanum-ce at
`a7ff41b300ef712f0e7d088183a3d08957110cdd` (imported unchanged in the first
commit) under `upstream/arcanum-ce`. Its Sustainable Use License and notices are
preserved. This repository modifies that engine for personal/noncommercial use.
Community Edition reads the user's retail assets and provides a native C/SDL
engine for both requested operating systems. It includes the campaign script
interpreter and save system. Its `src/net_compat.h` replaces networking with
constant/no-op macros; merely enabling the old menu cannot restore multiplayer.

The installed retail executable reports 1.0.7.4. The installation also contains
MatchMaker's `mm_won.dll` and Lua script; those are not our implementation base.
The first runtime-preparation implementation mistakenly symlinked loose map
caches. That was corrected, and all affected cache files were restored against
the initial SHA-256 manifest; see STATUS.md and the local restoration evidence.

## Session callbacks and native UI

Session frames are consumed before application dispatch. Each callback owns a
stable payload copy, so nested native UI polling cannot replay its frame or
change its bytes. Session and connection generations fence an older poll after
leave, reconnect or replacement. Destroy retires a session immediately and
releases its storage after outstanding polls return. The lobby uses the same
availability rules for display and activation; keyboard traversal skips
unavailable controls. These corrections were introduced on protocol47.

Protocol49 separates terminal native failures from recoverable transport loss.
Application dispatch stops immediately, while transport polling drains ordered
failure notices and their acknowledgments. Launched clients report through the
host, which relays the first cause to the whole party. A five-second deadline
releases unreachable connections; callbacks and native simulation cannot resume
merely because transport is still draining. Checkpoint formats are unchanged.

## Authority

Introduce a separate co-op service instead of reviving old network message
structs. The host will run all campaign script, timer, combat, item, and quest
mutations. Clients submit player intentions against stable object identities;
the host validates ownership and legality, then publishes authoritative state.
Engine handles are process-local and must never appear on the wire. Client UI
state (camera, panels) stays local. Dialogue has a host-owned conversation ID,
speaker/chooser, revision, observer transcript and choices; only the chooser may
submit a choice for the current revision. Travel and save/load need barriers.

Protocol 20 extends the same ordering to native NPC commerce and schematic
combinations. A host quote binds the item, quantity, conversation/page and native
price/rules; accepting it retires the token before callbacks. Personal crafting
requires the owned PC and NPC crafting requires the current chooser's schematic
dialogue. Service receipts wait for applied world state. Native quantity and
schematic windows remain local presentation and cannot mutate replicas. See
`COMMERCE.md` for wire formats and the combined acceptance fixture.

Protocol 18 adds an ordered skill service and per-owner native skill/trap
feedback. The item service grants distinct loot/stealing access and completes
native theft rolls through local tickets. These human skill actions run on the host;
targeting and presentation stay with the owning process. See `SKILLS.md` for
the controlled four-engine acceptance and remaining campaign boundaries.
Personal skill results persist in native history. Replica frame pings advance
local text floaters only; gameplay and campaign timers remain host operations.
Object installation derives floater visibility from that local presentation
list, so world captures cannot erase or resurrect an expired local floater.

Protocol 19 adds ordered live character development. Opening the native sheet
records its refund baseline on the host; allocation intentions identify that
owner's current edit lease and observed values. Native prerequisites and costs
remain host operations. A pending close completes before immediate reopening
can acquire a new baseline. Outcomes follow fresh capture/applied revision,
and owned replica level notices follow applied state without native leveling.
See `PROGRESSION.md` for the controlled fixture and remaining acceptance.

Dialogue apprenticeship uses the existing host-native `DialogState.pc_obj`,
cost, money and training operations. `TRAINING.md` records a four-engine
payment/refusal/chooser fixture; it does not require a separate training setter
on the wire. Inventory-based NPC services use the protocol 20 service adapter.

Protocol 17 retains native item-use intentions in the ordered spell service.
Gameplay scripts, charges, consumption and schematic learning run on the host;
target selection and personal book/schematic presentation stay on the owner's
process. Unspecified use preserves a null target, and SAP_USE precedes default
effect validation. Plain native approaches carry local completion tickets;
receipts wait for actual use or cancellation. Retired/reentrant callbacks cannot
dispatch the action again. Personal views have bounded ordered acknowledgments and
wait for the required applied world revision. The finish barrier drains native
item goals before freezing simulation and sealing audio. See `USE.md`.

Native callbacks can suspend inside movie playback and return after a new
reconnection epoch exists. Gameplay polling, socket dispatch and asynchronous
item-use/theft callbacks therefore retain their controller while on the stack.
Destroying that controller first retires its world/dialogue services, releases
native UI/ticket/sector/audio bindings, and denies further old-epoch actions.
Action services recheck retirement after native callbacks before capturing a
world or sending a receipt. Allocations are freed only after the last borrowing
call returns. Deferred destruction never clears a replacement controller's native
bindings. The runtime also abandons its old poll frame when a nested callback
replaces gameplay. This closes the captured Device-ending movie/rejoin use-after-free.

Protocol 15's positional audio service follows the same split. The host chooses
native world sounds; bounded ordered events carry sound IDs, stable tokens and
map coordinates. Each listener resolves its own assets and applies local
attenuation/pan/volume. Native handle generations stay local and prevent late
stops from touching reused UI sounds. Audio drains at the travel/finish barrier;
its presentation callbacks do not run native simulation. See `AUDIO.md`.

`src/coop/session.*` owns sockets, framing, handshake, party slots, lobby revision,
readiness and chat. It has no dependency on SDL, game objects, native struct
layout, or old networking. The engine adapter and TIG lobby use this same library
as the standalone network harness. All operations are polled on the game thread;
there is no blocking DNS or socket I/O in the UI.

TCP supplies ordered reliable transport for this small LAN party. Explicit
bounded, versioned messages preserve cross-platform representation. Large world
snapshots use bounded chunking/backpressure in `src/coop/bootstrap.*` without
enlarging the lobby frame/queue budgets. Clients acknowledge successful engine
loading before a host commit. See `PROTOCOL.md` for exact limits. The gameplay
adapter uses a separate pose path, ordered object/story batches and acknowledged
conversation views.

## Campaign engine adapter

`character.*` encodes bounded lobby creation recipes. `engine_character.*`
replays native starting-point, identity and knowledge rules before the host
creates a campaign PC. The TIG lobby reuses the original identity/allocation
screens and waits for Not ready acknowledgment before opening them. Editing
captures native unallocated floors before restoring purchases, so old purchases
can be refunded. `draft.*` replaces one local, content/profile-bound saved recipe
atomically; loading still requires native legality replay. Character controls
wait for roster confirmation after either a preset or custom selection. Starting
purchases remain pending; see `CHARACTERS.md` and `DRAFTS.md`.

`src/coop/campaign.*` now binds persistent owner identities to four permanent PC
ObjectIDs. Party traversal, map filtering and cross-map serialization explicitly
include these members. The host retains one active map and carries the party on
cross-map travel. The registry is archived with normal saves; it is restored
before object loading and validated after all game/UI modules load. See
`CAMPAIGN.md` for the format, test evidence and limits.

The engine probes establish four distinct PCs, map transitions, bidirectional
Linux/Windows save compatibility and initial snapshot transfer into four actual
processes. `engine_snapshot.*` packages normal archives, validates the index,
loads the private reserved slot and binds the client's own PC by profile ID.
The snapshot fixture is about 564 KiB. Simulation stays paused in the bootstrap
probe; the movement stage instead retains the session, verifies owned actions
against the host and publishes PC poses. Explicit engine roles stop replica
time/scripts, and presentation-only movement avoids tile/jump consequences.
See `GAMEPLAY.md`. `object_state.*` explicitly represents normal object fields;
`world.*` stages complete object batches and `world_net.*` chunks them with
acknowledgments. Their identity, cache and sector boundaries are in `WORLD.md`.
`story_state.*` and `engine_story.*` now carry script globals, global quests,
shared discoveries/rumor state, generator/newspaper state and host clocks in the
same acknowledged batch as object changes. `STORY.md` describes shared quest
journals/rewards and the native restoration boundary. `dialogue.*` retains every
published conversation page, defers native choices until every participant has
acknowledged presentation, and validates chooser/revision/option authority.
`engine_dialogue.*` connects native host callbacks and replica presentation;
`dialogue_ui.*` provides wrapped text, scrolling and local history. See
`DIALOGUE.md` for the service/UI boundaries. `runtime.*` now retains the lobby
connection through deferred campaign launch, transfer/loading and the gameplay
handshake. It gates simulation during loading/failure and routes ordinary UI
movement, stopping and NPC approaches; `RUNTIME.md` describes the exact subset.
Native cross-map transfers now drain the current epoch before mutating the host
map, then restore its normal destination save on every client and recreate the
world/dialogue services. `TRAVEL.md` describes the barrier and its tested scope.
The runtime-lifetime `controls.*` adapter now coordinates pause, unanimous
rest/load votes and save barriers across gameplay epochs. `checkpoint.*` stores
complete native bundles atomically; `engine_rest.*` runs native rest on the host
before rebuilding replica campaigns. See `COORDINATION.md`. Broader campaign
actions and seamless live reconnection remain open.
`COMBAT.md` records the native controller prerequisite: cooperative host PCs use
human AP rules and initiative regions around the whole party with separate
sector holds. Combat intentions now execute in the host poll with turn-generation validation
and receipts ordered behind world application. Replicas use presentation-only
native combat queries; see `COMBAT.md` for the controlled fixture and remaining
combat acceptance work.
`items.*` now stages discrete owned-item requests outside socket/input
callbacks, retains the last exact receipt for replay, and waits for a capture
after execution plus replica application before releasing the local request.
`ITEMS.md` describes native inventory/pickup integration and its remaining scope.
`LOOT.md` adds per-player target access to that ordered service. Native loot
callbacks stage requests for the host; only that player's client constructs the
view after world application. Shared transfers recheck current item ownership,
and access revocation releases the host's native container/decay state.
Retain single-player script
semantics; do not make `tig_net_is_active()` true globally, since that selects
many legacy multiplayer behaviors that bypass campaign functionality.

The full acceptance contract is `OBJECTIVE.md`. Session tests cannot validate
campaign synchronization or full-game completion.

Native activity now uses a host-only party proximity adapter in `campaign.*`,
covering NPC heartbeat admission/delay and animation culling. Combat XP sharing
uses an explicit credit operation rather than changing all native XP awards;
quest sharing therefore keeps its existing accounting. Owned dead characters
keep their object/registry identity and do not invoke the local game-over reset.
`ENCOUNTERS.md` documents these policies and their original-content fixtures.

`magic.*` adds a separate ordered cast/cancel stream using the same host poll and
capture/applied-world receipt boundary. `engine_magic.*` validates the owned
caster, native knowledge/source/target rules and turn, then submits one native
invocation. Native IDs stay local: maintained spells publish separate allocation
tokens, caster GUIDs, spell IDs and optional target identities in the bounded
`ACS3`/`ACM1` subsystem view. Replica UI uses that view for the original maintenance
bar while native effects arrive in normal object fields. Replica invocation,
component processing and interruption are gated. `MAGIC.md` describes native
execution, slot reuse, UI integration and the remaining FX/service/campaign work.
