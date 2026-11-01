# LAN wire protocol, version 61

All participants must use protocol 61. Version 61 adds wallet-to-wallet gold
transfers (item opcode 18, null item ID, recipient campaign member, positive
32-bit amount bounded by INT_MAX). The host checks sender ownership, activity,
proximity, funds and recipient overflow, then uses the existing ordered item
request/world acknowledgment path.

`COOP_PLAY_POLICY` remains two big-endian u32s: epoch and packed host settings.
The second word uses bits 0–3 for auto-pause, bits 4–9 for autosave minutes
(0/2/5/10/15/30), and bit 10 for save after travel. Remaining bits must be zero.
Host autosaves capture at safe idle without changing the epoch; loading them
uses the existing save fingerprint and unanimous party vote. Older campaign
archives remain readable; network peers must update together.

Version 60 adds presentation references
to dialogue views (`ACD2`) and saved dialogue recovery (`ACDLGR06`). A reference
is a canonical resource path, numeric text key and text variant. Each player
resolves it locally; original conditions, actions and choice indices remain
authoritative. Personal language files do not enter the gameplay fingerprint.
Older `ACD1` transcripts and recovery formats 1–5 remain readable. See
[dialogue serialization](DIALOGUE.md#protocol-6) and [language setup](LOCALIZATION.md).

Version 59 changed only the world-transfer envelope; action ownership,
canonical state, CRC validation and apply/ACK barriers retain their semantics.

`WORLD_BEGIN` (type 32) is now 20 bytes: epoch, revision, wire length, canonical
CRC-32 and decoded length (five big-endian u32 values). Equal lengths mean raw
bytes. A smaller wire length means a zlib stream produced at `Z_BEST_SPEED`.
Wire length must be positive and no larger than decoded length; decoded length
is 20 bytes through 16 MiB. Clients require exact decoded size, complete stream
consumption and canonical CRC before calling the unchanged world decoder.
`WORLD_CHUNK` and `WORLD_ACK` are unchanged. Incompressible captures use raw
bytes. Only one batch is outstanding; every peer must apply/ACK it before the
next. No new client simulation or altered script/event timing is introduced.

NPC pose packets retain the version 53 format. Changed actors are sent at the
existing maximum rate, with a one-second refresh for unchanged actors. Per-peer
pending bits survive output backpressure; crowded queues use a rotating bounded
packet budget, and player poses enter the queue before cosmetic NPC traffic.

See [measurements and verification](QOL_BENCHMARKS.md).

## Earlier protocol additions

Version54 carries the native turn-order presentation in combat state ACB2:
the previous37-byte prefix, a count at37 and32 canonical17-byte object IDs at38
(582 bytes total). Entries begin with the current actor; unused entries are
zero and duplicate entries are invalid. Native save formats are unchanged.
COMBAT_REQUEST retains its51-byte layout and adds operation4, host-only
immortality (`value = member << 1 | enabled`, member0–3). Timing and immortality
validate host ownership/session state independently of actor vitality or turn
generation. Ordinary actions retain the active-owned-actor and turn gates.
Owner option bit5 persists immortality; player option edits preserve it.

Version53 added GAME_NPC_POSES (type29): nearby NPC positions/art at up to20Hz,
using bounded packets and stable IDs. It also corrected default-run option
migration and client presentation ticks. See [the correction record](PROTOCOL53_FIXES.md).

The version 54 addition required all participants to update together.

Version52 adds complete local movement routes. GAME_MOVE keeps its41-byte
single-point form; a multi-point request appends a count2–6 at offset41 and
(count-1) big-endian x/y pairs at42. Length must match exactly. The host validates
all points and ownership before replacing any current action, then queues native
walking/running goals in order. Invalid later points cannot partially start a
route. New movement and Halt discard the previous route. Native interruptions
and shared-operation barriers retain their existing behavior.

The run byte now describes the owner's resolved walk/run intention. The sending
engine applies that player's Always Run setting, Ctrl/Num modifiers and native
encumbrance threshold. The host must not reinterpret explicit walking using its
own keyboard or another actor's preferences. All peers must update to52; native
checkpoint formats are unchanged.

Version51 preserves complete plotted world journeys. CONTROL_REQUEST (type61)
is now 244 bytes: offset 123 is a waypoint count (0–30), followed at 124 by 30
big-endian packed sector X/Y values. CONTROL_VIEW (type62) is 254 bytes, with
count at 133 and route at 134. A zero count retains a direct journey to `value`;
otherwise the final waypoint must equal `value`. Unused entries are zero;
consecutive duplicate waypoints and routes on non-travel actions are rejected.
The host checks each destination against map bounds and blocked sectors, then
reconstructs every segment with the native pathfinder after the shared vote.
All peers receive the same route, which is retained in interrupted-operation
state. Native checkpoint formats are unchanged; older multiplayer saves remain
readable, while peers must all run protocol 51.

Version50 retains scripted floating/printed story messages across detached
dialogue controllers and captures ordinary queued hints awaiting observer ACKs
before reconnect retires their transport. Fresh hints cannot overtake the
retained FIFO. `arcmp-dialogue.bin` writes `ACDLGR05`, adding up to 32
notices containing the original PC GUID, speaker ID/name, speech ID, text and
an optional existing journal-page index. The bounded sidecar limit is 128 KiB;
versions01–04 remain readable. Delivery uses existing dialogue float messages.
The host retains each notice through observer acknowledgments; rejoin can
redeliver it without adding another journal entry. Separate identical native
events remain separate entries. Network version50 prevents older clients from
receiving snapshots containing the new sidecar layout.

Version49 adds acknowledged terminal campaign failure. A launched client reports
its native error to the host; the host freezes application traffic and broadcasts
the first cause to every connected owner. Each recipient acknowledges receipt
before the host closes its socket. The failed runtime remains frozen while
transport polls finish the exchange, bounded to five seconds from resumed polling.
Queued application bytes retain their framing and sequence numbers but cannot
execute more native callbacks. Terminal failures disable automatic reconnect;
ordinary transport loss retains its existing retry/recovery behavior. Version48
was an unfinished local experiment and was never accepted. Saved campaign formats
are unchanged; everyone must use matching current-protocol executables to connect.

Version47 adds MAGIC_RESULT status12 for native line-of-sight refusal. Both
spell and item-use refusals persist in the acting owner's native message history.
Request, snapshot and checkpoint formats are unchanged. Older peers are refused
before loading the campaign.

Version46 adds a public connection-to-campaign-member binding to GAME_START.
After its four-byte epoch, four bytes identify the campaign member for each
connection slot;255 means absent. The client validates occupied members,
uniqueness, absent slots and its own authenticated member before installing the
whole map. No owner credentials are published. The active lobby uses this map
to show the saved native characters after Load or rejoin, rather than fresh lobby
selections. Input ownership remains host-validated against private identities.
Checkpoint formats are unchanged. Older network peers are refused at handshake.

Version45 retains opening admissions through an entire scripted conversation
chain. Identical NPC openings from heartbeats or another observing human are
coalesced until all speakers, callbacks and acknowledgments finish. Explicit
script continuations keep their original line/page semantics. Unavailable queued
speakers are discarded without failing the campaign. `arcmp-dialogue.bin` writes
`ACDLGR04`, adding a bounded list of opening keys so transport recovery does not
restart the encounter. Versions01–03 remain readable. Older network peers are
refused before snapshot loading; dialogue wire messages are unchanged.

Version44 retains active native dialogue panels in the campaign continuation.
`arcmp-dialogue.bin` introduced `ACDLGR03`, preserving the native mode, the
preceding player response and a checked reference to the existing transcript
page. Choice pages, barter/companion inventory, passive character sheets, maps,
crafting, identification and repair can resume without rerunning entry actions
or service preparation. Readers accept `ACDLGR01` and `ACDLGR02`; missing fields
default to a choice page with no selected-response/history reference. Older
network peers are refused at connection time, before snapshot transfer.

The selected-response/history extension was exercised in protocol43 development
builds. Protocol44 includes that extension and the native panel mode. The existing
bounded dialogue wire messages and chooser ownership rules are unchanged.

Version38 adds named campaign slots and the shared save catalog (types70/71).
Party-control requests/views now include the selected slot fingerprint, presence
and description so consent identifies the exact saved campaign. See
[party coordination](COORDINATION.md) for formats and UI behavior.
Version38 also routes Fate activation/refund through the existing progression
request layout: operation3, kind/lease0, index0–11, direction1/2, expected pending
mask and point total. Full heal cannot be deactivated. See [progression](PROGRESSION.md).

Version37 adds a saved ordered-arrival event to the native campaign schema.
Its monotonic FIFO sequence survives partial timer pings and recovery; legacy
arrival records remain readable. The handshake rejects older executables before
snapshot loading. Optional dialogue and committed-transition recovery sidecars
remain backward compatible with older saves that lack them.

The framing/lobby implementation is `src/coop/session.c`; the initial snapshot
transfer is `src/coop/bootstrap.c`. The first movement/pose adapter is
`src/coop/gameplay.c`; object batches use `src/coop/world_net.c`. Campaign
subsystems use `story_state.*`, and shared conversations use `dialogue.*`.
See `STORY.md` and `DIALOGUE.md` for their formats. Do not send native game structs
over this transport.

All integer fields use big-endian byte order. Each TCP frame begins with:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `ACMP` |
| 4 | 2 | Protocol version, currently 52 |
| 6 | 2 | Message type |
| 8 | 4 | Payload bytes, at most 1024 |
| 12 | 4 | Per-direction sequence, first 1, then strictly increasing |

Partial headers, partial payloads and multiple frames in one TCP read are
supported. Each connection has bounded 32 KiB receive/send buffers. Work per
poll is bounded. Oversized headers are rejected before allocating any payload.
The host's slot is 0; remote slots are 1–3. Slot 255 means unassigned.

| Type | Direction | Payload |
| --- | --- | --- |
| 1 HELLO | Client → host | 32-byte content SHA-256, 16-byte persistent identity, 32-byte name |
| 2 ROSTER | Host → client | Assigned slot (u8), revision (u32), locked flag (u8), reserved mask (u8), four player records |
| 3 CHAT_REQUEST | Client → host | NUL-terminated text, at most 240 characters |
| 4 CHAT | Host → client | Sender slot (u8), name (32 bytes), NUL-terminated text |
| 5 READY | Client → host | 0 or 1 (u8) |
| 6 CHARACTER | Client → host | Prototype (u32), 32-byte character name; custom selections append the 176-byte ACH2 recipe |
| 7 PING | Either | Empty |
| 8 PONG | Either | Empty |
| 9 ERROR_TEXT | Host → client | NUL-terminated explanation |
| 10 FAILURE_REQUEST | Launched client → host | NUL-terminated native failure reason, at most 159 printable characters |
| 11 FAILURE_NOTICE | Host → client | NUL-terminated terminal reason; guest reports include their authenticated player name |
| 12 FAILURE_ACK | Client → host | Empty; acknowledges the terminal notice on this connection |

A player record is exactly 247 wire bytes: connected (u8), ready (u8), prototype
(u32), player name (32 bytes), character name (32 bytes), custom flag (u8), and
176 recipe bytes. ROSTER is 995 bytes. The recipe bytes are zero for a preset.
Custom selections require prototype 16066 and a matching recipe/name. Names must
terminate within 32 bytes. Text is printable ASCII. Empty/all-space names and
chat are rejected. The engine validates available prototypes and replays native
creation rules before campaign launch. See `CHARACTERS.md` for ACH2 and saved ACH1 compatibility.

The game creates `arcmp-player.id` in its private runtime using OS randomness.
It contains exactly 16 nonzero-as-a-whole opaque bytes. Invalid existing profiles
are preserved and reported as errors. Keep this file to retain campaign ownership;
changing a nickname or connection slot does not change the owner. The host rejects
duplicate active identities. Identities are not included in ROSTER broadcasts;
clients retain their own identity from their configuration. This is an ownership
key, not cryptographic authentication. Version 5 added combined object and
campaign-state batches. Version 6 adds ordered conversation views and choices.
Version 7 adds NPC talk/stop intentions and chooser cancellation; see `RUNTIME.md`.
Version 15 adds ordered positional sound events and their drain barrier; see
`AUDIO.md`. All participants must use the same protocol version.
Version 21 adds runtime-lifetime pause, rest and checkpoint requests/views
(types 61–63), unanimous operation votes and durable recovery copies. See
`COORDINATION.md` for ordering, controls and formats.
Version 22 adds runtime-lifetime shared presentation messages 64/65 and travel
action 7 in party controls. Its value packs the destination sector X/Y as two
16-bit unsigned coordinates; native travel uses that sector's centre tile.
Journey phase 6 publishes elapsed game hours. See `MEDIA.md` and `TRAVEL.md`.
Version 23 adds typed owned-companion orders/results (66/67), with host execution
and applied-world receipts. See `CONTINUITY.md` for wire fields and story policy.
Version 24 extends story state to `ACS4`, appending a 52-byte `ACE1` lighting and
sound view. See `ENVIRONMENT.md` for its fields and save compatibility.
Version 25 adds reserved owners to locked rosters and acknowledged live rejoin
views (68/69), followed by a fresh snapshot epoch. See `RECONNECT.md`.
Version 26 transfers and acknowledges the first complete world state after
GAME_HELLO and before GAME_START. Earlier peers reject that ordering, so every
participant had to upgrade. This initializes combat generations and native
presentation state before any player can submit an action after loading.
Version 27 adds canonical sector state to `ACW3` world batches. It also raises
the bounded world-send budget to 24 chunks per peer per tick and permits 45
seconds for linked initial-world establishment (the pre-link handshake retains
15 seconds and the world progress watchdog remains 30 seconds).
Version 28 adds operation 13 (native world use) to the existing 59-byte item
request, using its ordered receipt and applied-world boundary. Sector records
advance to `ASC2` with structural roofs and bounded static-light lists; the
`ACW3` outer header remains unchanged. See `ITEMS.md` and `SECTORS.md`.
Version 29 extends the environment suffix to 92-byte `ACE2`, adding scripted
audio revision and persistent-member origin sectors for independent regional
playback. Carried item exports canonicalize location/offsets to zero while
retaining parent/inventory references; ground coordinates retain map bounds.
Legacy `ACE1` is accepted only at the save boundary. `ACS4` and
`ACW3` outer headers stay unchanged. See `ENVIRONMENT.md`.
Version 30 extends character recipes to 176-byte `ACH2` with sixteen bounded
starting shop trades. CHARACTER is 212 bytes for custom builds, player records
are 247 bytes and ROSTER is 995 bytes. The host replays native stock, price, gold
and inventory rules before launch. See `CHARACTERS.md` and `DRAFTS.md`.
Rebuild every participant together; earlier wire versions are incompatible.

Only HELLO may establish a remote identity. No client message can select another
slot, replace the roster, or supply an accepted ready state for another player.
The host publishes roster revisions; clients do not optimistically alter roster
state. Character or membership changes clear all readiness. Ready requires a
selected prototype. `all_ready` requires at least two connected, ready players.
The runtime requires it before the host can request campaign launch.
During loading/play, the session locks membership/character/readiness, while
chat and engine messages remain available; new joins receive an error without
changing the roster.

## Initial snapshot transfer

Engine types 16–95 are delivered only after the content/profile handshake to a
registered adapter. Sender identity comes from the connection's slot. Clients
can send only to the host. Unknown/unhandled engine messages disconnect the
peer. Application sends reserve 2 KiB for transport/lobby control frames and
return false on queue backpressure without consuming a sequence or disconnecting.
The caller retries later; it must keep the payload immutable until queued.

The bootstrap adapter supports one bounded transfer attempt per instance:

| Type | Direction | Payload |
| --- | --- | --- |
| 16 SNAPSHOT_OFFER | Host → client | Epoch (u32, nonzero), total size (u32), CRC-32 (u32), participant mask (u8) |
| 17 SNAPSHOT_CHUNK | Host → client | Epoch (u32), exact next offset (u32), 1–1016 data bytes |
| 18 SNAPSHOT_READY | Client → host | Epoch (u32), sent only after validating/loading the entire snapshot |
| 19 SNAPSHOT_COMMIT | Host → client | Epoch (u32), sent after every participant acknowledges loading |
| 20 SNAPSHOT_CANCEL | Either | Epoch (u32) |

An offer requires a ready party matching the local participant mask. Allocation
is bounded to 64 MiB. Chunks must be contiguous, fit the announced size and match
the epoch. CRC-32 uses the zlib/IEEE convention. The host sends at most eight
chunks per peer per poll and waits for every client's explicit loaded signal.
Thirty seconds without transfer/load progress, or a roster revision/membership
change before commit, fails the attempt and cancels the remaining participants.
The normal connection timeout still applies. Repeated identical READY requests
are idempotent and do not change the roster revision.

Host COMMITTED means commit frames were queued for all loaded clients. Clients
enter COMMITTED when that frame is received. This is a loading barrier, not a
distributed atomic transaction or a gameplay disconnect/recovery policy.
The bootstrap-only diagnostic stops simulation and closes its sockets after
this barrier. Movement mode instead retains them for poses and object batches;
the runtime now retains the session after ordinary lobby launch too; see `RUNTIME.md`.

The transferred `ACSNAP1` envelope contains the normal `.tfai`/`.tfaf` save pair
and explicit big-endian lengths. The archive index itself retains the engine's
little-endian format. Before writing the reserved `arcmp-bootstrap` slot, the
adapter validates exact sizes, total archived bytes, directory nesting, bounded
relative components and required campaign files. Native town-map fog and note
files (`.tmf`/`.tmn`) are permitted beside the data files. Absolute paths, traversal,
separators inside names, Windows device names and excessive path lengths are
rejected. The full legacy binary save parser is not a hardened untrusted-data
parser. CRC is corruption detection, not authentication.

The world archive includes campaign ownership bindings; identities are therefore
visible to clients in a snapshot even though they are omitted from lobby rosters.
Do not treat these opaque IDs as secret authentication tokens.

## Initial gameplay adapter

All fields below are explicitly encoded big-endian, with raw 16-byte GUIDs.
Every payload begins with epoch (u32). Directions and state transitions are
validated by the adapter, and the transport provides the sender slot.

| Type | Direction | Fields after epoch |
| --- | --- | --- |
| 21 GAME_HELLO | Client → host | Client's loaded PC GUID (16 bytes) |
| 22 GAME_START | Host → client | Epoch plus four connection-to-member bytes (255 absent) |
| 23 GAME_MOVE | Client → host | Command number (u32), map (u32), actor GUID (16 bytes), first x/y (u32 each), explicit run (u8), turn generation (u32); 41 bytes for one point, otherwise count at41 and remaining x/y pairs at42; 50–82 bytes for2–6 waypoints |
| 24 GAME_RESULT | Host → client | Last processed command number (u32), accepted (u8) |
| 25 GAME_POSES | Host → client | Revision (u32), map (u32), occupied count (u8), four 48-byte pose records |
| 26 GAME_STOP | Host → client | Empty |
| 27 GAME_FINISH | Host → client | Final pose revision (u32), next epoch (u32; zero finishes the diagnostic, otherwise exactly current epoch + 1) |
| 28 GAME_FINISHED | Client → host | Acknowledged final pose revision (u32) |
| 35 GAME_TALK | Client → host | Command number (u32), map (u32), actor GUID (16 bytes), portable NPC ObjectID (17 bytes), turn generation (u32) — 49 bytes including epoch |
| 41 GAME_HALT | Client → host | Command number (u32), map (u32), actor GUID (16 bytes), facing target x/y (u32 each), turn generation (u32) — 40 bytes including epoch |
| 42 ITEM_REQUEST | Client → host | Independent item sequence/map (u32 each), actor GUID (16), operation (u8), item/target ObjectID (17), recipient member (u8), inventory location (i32), loot access generation and turn generation (u32 each) — 59 bytes including epoch |
| 43 ITEM_RESULT | Host → client | Item sequence, status, required applied world revision (u32 each) |
| 44 LOOT_ACCESS | Host → client | Access generation, required applied world revision (u32 each), target ObjectID (17; null revokes access), mode (u8: loot 0, steal 1; zero on closure) — 30 bytes including epoch |
| 45 COMBAT_REQUEST | Client → host | Sequence/map (u32 each), actor GUID (16), operation/value (u8 each), target ObjectID (17), turn generation (u32) — 51 bytes including epoch |
| 46 COMBAT_RESULT | Host → client | Sequence, status, required applied world revision (u32 each) — 16 bytes including epoch |
| 47 MAGIC_REQUEST | Client → host | Ordered cast/cancel/item-use intent, source and target identities, location/token and turn generation — 86 bytes including epoch |
| 48 MAGIC_RESULT | Host → client | Sequence, status, required applied world revision (u32 each) — 16 bytes including epoch |
| 49 AUDIO_BATCH | Host → client | Ordered positional sound batch; 16-byte header and up to 36 canonical 28-byte events |
| 50 AUDIO_ACK | Client → host | Epoch and applied audio revision (u32 each) — 8 bytes |
| 51 WRITTEN_VIEW | Host → initiating client | Personal reading or schematic notice, reader GUID and required applied world revision — 40 bytes including epoch |
| 52 WRITTEN_ACK | Client → host | Epoch and personal view sequence (u32 each) — 8 bytes |
| 53 SKILL_REQUEST | Client → host | Ordered conceal/repair/disarm action, actor/target identities and turn generation — 51 bytes including epoch |
| 54 SKILL_RESULT | Host → client | Epoch, sequence, status, required world revision (u32 each) — 16 bytes |
| 55 SKILL_FEEDBACK | Host → client | Personal native skill/trap catalogue message, actor GUID and required world revision — 36 bytes including epoch |
| 56 SKILL_FEEDBACK_ACK | Client → host | Epoch and personal sequence (u32 each) — 8 bytes |
| 57 PROGRESSION_REQUEST | Client → host | Owned character-sheet open/close/edit, lease and observed allocation state — 44 bytes including epoch |
| 58 PROGRESSION_RESULT | Host → client | Epoch, sequence, status, applied-world requirement and lease (u32 each) — 20 bytes |
| 59 SERVICE_REQUEST | Client → host | Owned actor, conversation/page, quote/buy/combine, item/crafter, quote token, quantity and recipe — 66 bytes including epoch |
| 60 SERVICE_RESULT | Host → client | Epoch, sequence, status, required applied world revision, quote token and total price (u32 each) — 24 bytes |

A pose record contains GUID (16), x/y (u32 each), offset x/y (i32 each), current
art ID (u32), object flags (u32), HP damage (i32) and fatigue damage (i32).
Empty campaign slots have zero records. The exact pose payload is 205 bytes.
The current result tracks the latest processed movement/stop/talk intention;
queued intentions can supersede an earlier result. Owned inventory actions
use separate ordered receipts in `ITEMS.md`. `LOOT.md` extends them with
per-player target access, shared container/corpse transfers and partial Take All.
Combat requests use a separate ordered receipt stream; see `COMBAT.md`.
Conversation choices already
use their own ordered per-request results in `DIALOGUE.md`.

Gameplay accepts at most 30 move/stop/talk intentions per peer per one-second window.
The host publishes poses at most every 50 ms, retaining transport queue bounds.
All loaded peers must confirm the correct PC before starting. Roster revisions,
disconnects and unexpected map changes stop this adapter. Native cross-map
transfers use the final-state barrier with a nonzero next epoch, then create
fresh bootstrap/gameplay services for that epoch. Bootstrap messages must carry
the runtime's expected epoch; retired gameplay frames are invalid. See
`TRAVEL.md` for the full load/resume lifecycle. Connection/initialization,
missing-pose and final-acknowledgment timeouts prevent indefinite waiting. See
`GAMEPLAY.md` for engine authority gates, validation and remaining work.

## Combat state and requests (version 12)

Version 12's `ACS2` appended one canonical `ACB1` combat record after the area
flags. Version 14 retains it at the same position in `ACS3`. All fields
are big-endian; the exact record is 37 bytes:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | `ACB1` |
| 4 | 4 | Flags: bit 0 turn-based policy, bit 1 active turn-based combat; others zero |
| 8 | 4 | Remaining AP, 0–INT32_MAX |
| 12 | 4 | Native round, 0–INT32_MAX |
| 16 | 4 | Turn generation, UINT32_MAX forbidden |
| 20 | 17 | Current actor ObjectID |

Inactive state requires null actor, zero AP and zero round; generation may
persist. Active state requires turn-based policy, nonzero round/generation and
an instance GUID or validated positional actor identity. Actor resolution follows
staged object allocation and precedes world commit. Replica installation changes
presentation only.

COMBAT_REQUEST offsets including epoch: epoch 0, sequence 4, map 8, actor GUID
12, operation 28, value 29, target 30 and turn generation 47. Operations are attack
0, stance 1, end turn 2 and timing policy 3. Stance/policy values are zero/one;
end turn requires zero. Version 13 interprets the attack value as these bits:

| Bits | Meaning |
| --- | --- |
| 0 | Explicit forced attack, including an eligible ally or destructible object |
| 1–2 | Native hit location: torso/default 0, head 1, arm 2, leg 3 |
| 3–4 | Attack mode: native default 0, once with approach 1, repeat with approach 2, single attempt without approach 3 |
| 5–7 | Reserved; must be zero |

Called shots and mode bits never authorize an otherwise forbidden friendly
target. The 51-byte request and all offsets remain unchanged. Only attack has a nonnull target;
prototypes are never attack targets. Every sequence is positive and strictly the
previous sequence plus one, with one pending request per peer. Exact last-request
replays reuse the receipt; conflicting replays and malformed frames fail closed.

COMBAT_RESULT contains epoch at 0, sequence at 4, status at 8 and required applied
world revision at 12. Status values are success 0, unavailable 1, ownership 2,
target 3, distance 4, another turn 5, stale generation 6, busy 7, native refusal 8
and host-only timing 9. All receipts wait for a fresh capture; clients retain the
pending action until that revision is applied. Native animation effects may
continue after accepted intent. An identical native attack already in progress
with matching body location and mode is confirmed without creating another goal
or spending AP; it is still a new
request/receipt when it carries a new sequence. The host poll executes staged combat requests;
transport callbacks never execute them.

Version 12 also appends expected turn generation to GAME_MOVE at offset 37,
GAME_HALT at 36, GAME_TALK at 45 and ITEM_REQUEST at 55. Stale requests have no
native action effect. LOOT_CLOSE may still release its lease. This version is
incompatible with earlier executables; rebuild all participants. Version 13
also rejects version 12 peers during the handshake, before new attack flags can
be mistaken for the earlier boolean value.

## Spell requests and maintained views (version 14)

MAGIC_REQUEST offsets including epoch: epoch 0, sequence 4, map 8, actor GUID
12, operation 28 (u8; cast 0, cancel 1, item use 2 since version 16), spell 29 (u16), flags 31 (u8), source
ObjectID 32, target ObjectID 49, tile x/y 66/70 (u32), token 74 (u64) and turn
generation 82 (u32). The exact length is 86 bytes. Flags are explicit force bit 0
and location bit 1; other bits must be zero. Casts require a nonnull instance
source, spell 0–222, zero token and either an instance target with zero x/y or a
null target with bounded x/y. Cancels require nonzero token, null source/target,
zero spell/flags/x/y. Generation UINT32_MAX and prototype sources/targets are
invalid. Per-peer sequence, replay, staging, rate limit, timeout and
capture/applied-revision rules match the separate combat service.
Item use has the cast source/target/location shape with zero spell/token. Since
version 17 it also permits a null target with zero flags and x/y, representing
unspecified native use. Force with a null target is invalid. Cast validation is
unchanged. Version 17 rejects earlier peers during the handshake.
The host obtains effects from the owned item. Object SAP_USE precedes default
validation; explicit tile use retains the native path without that script.
Plain approaches wait for actual native use or refusal before capture/receipt;
local tokens guard their completion callbacks. Skill subgoals/throws still
acknowledge accepted native intent rather than every later outcome.
See `USE.md` for the exercised subset and unverified branches.

MAGIC_RESULT statuses are accepted intent 0, unavailable 1, ownership 2,
target 3, distance 4, another turn 5, stale generation 6, busy 7, native refusal
8, unknown spell 9, fatigue 10, unavailable source 11 and blocked line of sight12. Cancellation validates
the owned caster and a separate allocation token, never a reusable native slot
alone. Targeting/resistance and native invocation run only on the host.

`ACS3` uses header offset 4 for the byte length of the appended maintained-spell
view. All earlier header/variable/flag/quest/rumor/generator/newspaper/area/ACB1
positions are retained. `ACM1` follows ACB1: four-byte tag, count u32 (0–512), then
43-byte entries: token u64, caster GUID 16, spell u16 (0–222), target ObjectID 17
(nullable). Entries must be strictly ordered by positive token; identities,
content and object references validate before world commit. The maintained view
is presentation only; clients never reconstruct native run pointers or timers.
See `MAGIC.md` for UI, authority, token lifecycle and acceptance limits. Version
14 rejects version 13 peers during the handshake.

## Skill controls and feedback

Version 20 adds types 59–60 for NPC service quotes, purchases/transfers and
schematic combinations, plus item operation 12 for partial party transfers.
`COMMERCE.md` defines their formats, authority and receipt ordering. Version 20
rejects older peers at the handshake.

Version 19 adds types 57–58 for host-owned character development and editor
leases; see `PROGRESSION.md` for its recorded acceptance.
Version 18 adds types 53–56 and item operation 11 for native stealing access.
Skill actions are allowlisted controls, not arbitrary native skill invocations.
Feedback has its own bounded per-recipient sequence/FIFO, waits for applied
world state and acknowledges exact replay without repeating presentation.
Stealing completes through a host-local ticket and native AP/roll/transfer;
take-all and rearrangement are refused for this access mode. `SKILLS.md` defines
the precise formats, action/result semantics and acceptance limits.

## Object batches

| Type | Direction | Fields |
| --- | --- | --- |
| 32 WORLD_BEGIN | Host → client | Epoch, revision, wire bytes, canonical CRC-32, decoded bytes (u32 each; version 59) |
| 33 WORLD_CHUNK | Host → client | Epoch, revision, exact next offset (u32 each), 1–1012 data bytes |
| 34 WORLD_ACK | Client → host | Epoch, applied revision (u32 each) |

Revisions start at one. Only one batch may be in flight; all peers must apply and
acknowledge it before the next capture. Replayed BEGIN/ACK, wrong epochs,
noncontiguous chunks, oversized declarations and illegal directions fail the
adapter. Allocation is bounded to 16 MiB and transfer progress to 30 seconds.
CRC is checked before staged object application. The diagnostic finish barrier
drains world batches before final poses; pose publication continues during that
drain. Its combined world/final-acknowledgment deadline is 35 seconds.

An `ACW5` batch has a 32-byte header: magic (4), map (u32), upsert count (u32),
deletion count (u32), story byte count (u32), sector byte count (u32), atlas
byte count (u32), and document byte count (u32). Each upsert is length (u32)
followed by one `ACO1` object. Deletions follow as 17-byte GUID identities, then
the optional `ACS4` subsystem state, canonical sector section and optional
`ACT1` exploration atlas followed by the optional `ACD1` party document catalogue.
The catalogue is independently change-tracked, limited to 16,396 bytes, and
validated against installed written content before native mutation. See
`DOCUMENTS.md` for its codec, F7 menu and save/rejoin semantics. The atlas is
independently change-tracked; its bound is 1,050,632 bytes. See `CARTOGRAPHY.md`
for its schema and native save behavior. `SECTORS.md`
specifies the sector records and pinning policy. Counts are at most 32,768 each;
duplicates and upsert/delete conflicts are rejected. Only the current map is
supported. Deleting prototypes or registered campaign PCs is forbidden.
Subsystem data is at most 64 KiB and validated with all object records before
installation. Zero story length retains previous subsystem values. `STORY.md`
specifies the full format and native restoration policy. The final pose revision
is assigned once after all world acknowledgments, separately from regular poses
sent during draining, and must be newer than those poses.

An object ID is one kind byte plus 16 payload bytes:

| Kind | Payload |
| --- | --- |
| 0 | All zeros, allowed only for null references |
| 1 | Nonzero prototype number (u32), then twelve zero bytes |
| 2 | Nonzero 16-byte GUID |
| 3 | Positional x, y, temporary ID, map (u32 each) |

The 43-byte `ACO1` header contains magic (4), ID (17), prototype ID (17), object
type (u8), field count (u16), options (u8) and reserved zero (u8). Object type is
0–17; the engine also checks the actual prototype/type and field schema.
Instance IDs must be GUID or positional; prototype IDs use kind 1 or 2.
Option bit 1 means separate PC poses; it is valid only for an existing PC
(type 15), excludes the seven fields carried by GAME_POSES and preserves their
current local values when staging. Other option bits are invalid.

Each field has number (u16), kind (u8), payload size (u32), then payload bytes.
Field numbers are strictly increasing, nonzero, and validated against the
engine's normal field metadata. The entire object is at most 256 KiB and 512
fields. Values are:

| Kind | Payload |
| --- | --- |
| 1 | i32 (4 bytes) |
| 2 | i64 (8 bytes) |
| 3 | String bytes, at most 4096, no interior NUL |
| 4 | Object reference (17 bytes) |
| 5 | Sparse array of i32 |
| 6 | Sparse array of i64 |
| 7 | Sparse array of 17-byte object references |
| 8 | Sparse script array: flags (u32), counters (u32), number (i32) |
| 9 | Sparse PC quest array: days (u32), milliseconds (u32), state (i32) |

Sparse arrays begin with count (u32), then key (u32) and value per entry. Count
is at most 4096; keys are strictly increasing and at most 65535. Signed values
use their two's-complement bit representation. No native script/quest padding
or pointer bytes are sent. Missing overrides inherit the matched prototype;
an upsert replaces the previous complete override set rather than patching
individual fields. See `WORLD.md` for native application, tests and boundaries.

Chat history retains the latest 64 messages, with a snapshot of each sender name
so slot reuse cannot relabel old messages. New participants receive retained
history after their initial roster. Remote chat is limited to eight messages per
second. Slow/invalid peers are disconnected independently.

Connection/HELLO timeout: five seconds. Complete-message inactivity timeout:
15 seconds. Heartbeats: three seconds. Callers supply a monotonic millisecond
clock. A partial byte stream does not postpone a handshake indefinitely.

The campaign registry can rebind an owner to a permanent character independently
of their new connection slot. Protocol 25 reserves the original running slots
and resynchronizes returning owners from the live host; see `RECONNECT.md`.
Profiles identify owners without cryptographic authentication.

## Positional sound presentation (version 15)

Type 49 is a host-to-client event batch, with a 16-byte epoch/revision/map/count
header and up to 36 canonical 28-byte START/MOVE/STOP events. Type 50 is the
client's eight-byte epoch/applied-revision ACK. Native sound IDs, stable tokens
and map coordinates are transmitted; native handles and paths are excluded.
The host waits for every peer before retiring a batch. Finishing/travel drains
this service with the world and action services. See `AUDIO.md` for the exact
byte offsets, replay rules, bounds and acceptance limits.

LAN discovery uses a separate bounded UDP protocol; see `LAN.md`. No encryption,
password or host migration is implemented. Content matching uses the prepared
runtime SHA-256. The optional native launcher revalidates content and binaries;
direct executable launch retains the prepared fingerprint.

## Protocol 31 campaign conversations and examination

Dialogue mode 10 carries host-selected floating/printed retail lines on the
existing ordered 36–40 transport. It is inactive and option-free; `reply` is the
line and `selected` its speaker name. ACKs refer to the transported revision;
current conversation/choice state stays on the active native page. This permits
hints during a conversation without closing or advancing its choices.

Item operation 14 is `COOP_WORLD_EXAMINE`, in the existing 59-byte request:
recipient zero, location -1. The connection owns the actor, and epoch/map/turn
checks still apply. The target must be live; ground targets must be within 30
tiles. Inventory targets must belong to the actor or an opened loot container
within three tiles. The host alone executes SAP_EXAMINE with no native AP charge;
script state and messages use existing world/dialogue barriers. UI descriptions
remain immediate and local. Repeated native hover refreshes throttle identical
script requests to at most one per 500 ms.

Protocol 32 adds the bounded shared-fade descriptor (presentation kind 3),
chooser-only NPC character sheets/maps and deferred journey proposals after a
map dialogue closes. `TIME.md` describes native clock/scheduler fixes; the
portable clock and saved DateTime layouts remain unchanged. See `MEDIA.md` and
`DIALOGUE.md` for the exact new presentation/interaction semantics.

Protocol 33 adds the independently tracked `ACT1` exploration atlas in `ACW4`.
Earlier protocol versions cannot read the new world header. All participants
must upgrade together; legacy native `.tmf` exploration files remain readable.

Protocol 34 adds the independently tracked `ACD1` document catalogue and the
32-byte `ACW5` header. Participants must upgrade together. Native checkpoints
without a document sidecar remain readable and start with an empty catalogue.

Protocol 35 retains ACW5 and adds a saveable native `Coop Arrival` time event
(type 35, appended after the original native types). It carries the original
human triggerer object, tile location and map through ordinary native event
serialization, object-reference recovery and map files. Older peers are rejected
because they cannot restore that native event type. Tile callbacks which occur
while the next gameplay epoch is unbound remain armed; the host executes them
once the four-engine handshake and current control context are ready.
The initiating traveller remains the triggerer across party placement; equal
pending arrivals coalesce, with at most 64 retained. Unique deadlines preserve
arrival order through native save/load. Pending entry events reject
ordinary commands and new collective operations, while dialogue, pause and
existing votes remain available. Events wait through existing conversations and
presentations. Their real-time clock is natively saved, and does not advance by
skipped rest/journey hours. Replica simulation never executes them.

Protocol 36 adds companion Inventory as order 8 in the existing canonical
76-byte follower request. It carries no target, force flag or destination. Host
validation retains stable human ownership, active companion, distance, map and
combat-generation checks. The shared conversation then uses the existing mode-3
service protocol, including zero-price companion transfers and owner-bound
quotes. Older peers reject the new order, so every participant must upgrade.
Native saves and arrival-event layouts remain unchanged.

## Protocol 39: destination choices and campaign options

All peers require protocol39. Maintained magic uses ACM2: its header is136bytes
(8 plus four32-byte choices), followed by the unchanged43-byte maintained entries.
Each choice has actor GUID16, token u64, origin map u32, spell u16, deferred-PC-charge
u8 and one zero byte; an empty token requires all fields zero. Active actor/token
pairs are unique. Magic operation3 chooses/cancels a pending destination: token and
spell match it, source is the actor instance GUID, target/flags/y are zero and x is
the known area ID below4096 (zero cancels). The fixed request remains86bytes.
Item-use flag4 carries Ctrl's own-skill preference, only on object-targeted item Use.
Disarm skill value1 similarly suppresses assistance; repair retains value0.

ACE3 environment state is96bytes. Original offsets0–49 are retained, difficulty
0–2 is at50, canonical fast-turn-based bool at51, sound state52–91 and four owner
option bytes92–95. Each owner byte is zero (unconfigured) or0x80 plus four bits:
auto-attack, auto-switch weapon, always-run and follower skills. ACE1/52-byte and
ACE2/92-byte native save sidecars remain readable; they default to normal difficulty,
standard turn-based timing and unconfigured owner preferences. Shared live story
records use the current schema. Native sidecars retain their trailing CRC32.

Progression operation4 updates the authenticated human's option mask. Kind/index/
lease/points are zero, direction is the new mask0–15, expected is the old byte (zero
or0x80–0x8f). It uses existing ordered requests, replay handling and world receipts.
Combat policy value2 selects fast turn-based mode;0/1 remain real-time/standard TB.
Only the host can apply campaign policy or difficulty changes.

Item operations15/16/17 take/store/drop a gold/ammunition quantity. The existing
59-byte request carries quantity at47, recipient zero, original item instance and
loot access generation at51. Take/store require the matching live nonstealing lease.
Ownership, available quantity, removal, native capacity and overflow are checked on
the host. The existing exact-replay record and receipt/world barrier cover effects.
The optional native spell-destination sidecar contains a136-byte empty-entry ACM2
view plus CRC32; it never replays the originating cast or item Use.

## Protocol 40: map annotations and native owner feedback

All participants require protocol40. `ACW6` extends the world header to36bytes:
original offsets4–28 retain their meaning; offset32 is the appended note-catalog
size. Object upserts/deletions follow the header, then story, sectors, exploration,
documents and map notes. Each optional section carries its current complete view
only when changed. Notes are decoded/content-checked before native world mutation.

`ACN1` map-note catalogs contain magic4, serial/revision/count(u32 each), count
276-byte records and CRC32. Records hold ID/revision/context/x/y(u32), type(u8),
three zero bytes,250-byte NUL-padded text and two zero bytes. Type0/1/2 are user
note/skull/chest;3 is an immutable script-created quest marker. Context0 denotes
the original world map; other contexts are original town-map IDs. IDs are unique,
increasing and at most0x0fffffff. Revisions are nonzero for active notes and do not
exceed the catalog revision. Limits are1024 total and199 per context.

Types72/73 carry note requests/results. Requests are304bytes: epoch/sequence/map
at0/4/8, owned actor GUID16 at12, operation/type at28/29, two zero bytes, ID at32,
expected revision36, context40, x/y44/48, text250 at52 and two final zero bytes.
Add0 requires zero ID/expected; Edit1 requires both; Remove2 has zero position,
type and text. Peers cannot create/change quest markers. Each owner has one
outstanding ordered request, exact-replay caching and ordinary30/s and 30s bounds.
Results are epoch/sequence/status/required-world-revision(u32 each). Statuses are
OK0, unavailable1, ownership2, changed3, location4 and full5. Success waits for a
fresh world capture, and the recipient waits for that revision before completion.
The CRC catalog is also the optional native map-note save sidecar.

The existing36-byte personal feedback record gains catalogs2–5. Catalog2 permits
red spell warnings600–603,605,10000;3 permits red item reasons1–19;4 permits blessing
IDs50–65535 with red=false;5 permits curse IDs50–65535 with red=true. Existing
owner/map/sequence/world-revision/exact-replay checks apply. Native rendering
looks up original strings/metadata and never reapplies effects. Queues hold64
records per owner and drain before save/travel. Lobby removal uses the existing
host error/roster messages and no new peer command.

## Native group commands and trap attribution (protocol41)

All participants now require protocol41. Message66 retains its original76-byte
prefix and appends128bytes of NUL-padded command text, making204bytes. Existing
orders0–8 require all-zero appended text. Order9 is a native broadcast: follower
ID is null, force=false, x/y carry the sender's cursor tile, optional target is
GUID/position ID, and text is nonempty, terminated and free of control characters.
The host uses the authenticated human, map and combat generation; individual
NPC orders retain native eligibility and existing follower validation. Human
Join/Disband broadcast mutations are excluded from campaign play. Ordered/replay/
world-revision receipts and limits are unchanged. The host never reads its own
cursor for a peer request.

Feedback catalog6 accepts only red native trap-placement messages2 and3 in the
existing36-byte acknowledged record. The native save sidecar `arcmp-traps.bin`
uses `ACTRAPS1`(8), count(u32), sorted24-byte records and CRC32. Each record is
portable object ID17, map(u32), stable campaign-member(u8) and two zero bytes.
Count is bounded at65536; duplicate/unsorted IDs, bad owners/static-map IDs,
noncanonical padding, truncation, trailing data and CRC faults reject the load.
Disarm/one-shot removal retire records. Old saves without attribution keep native
historical behavior; new records do not depend on a TCP slot or engine handle.

Native time-event type37 (`COOP_TRAPS`) preserves type28's spell, location and
optional bomb object in parameters0–2, adding stable member+1 in integer parameter3.
It is saved by the original event system. At detonation the existing native magic
attacker reference receives that member; casting source/parent remain native.
Earlier type28 events remain readable without retroactive owner inference.

Snapshot root-file preflight now recognizes the optional dialogue, transition,
spell-destination, map-note and trap sidecars, rejects duplicates, and validates
their complete portable schema/CRC before native extraction. Transition fade
limits match the native adapter; trap keys sort by map then portable object ID.
ACE1, ACE2 and ACE3 environment sidecars remain readable. Full native validation
still binds owners, objects and content after loading; a valid byte envelope
alone does not authorize a gameplay action.

## Input quiescence before final state (protocol42)

All participants require protocol42. Messages74/75 carry epoch and next-epoch
(u32 each; next-epoch0 denotes finish without a map transition). Host74 freezes
new input at each client; client75 is queued after its earlier action frames.
The host records every connected owner's75, continues world captures and drains
all action/native/presentation receipts before settling the final world. The
existing final pose and GAME_FINISH follow that drain; GAME_FINISHED separately
acknowledges actual final completion. A client cannot interpret stopped input as
final state. Reconnect/roster draining recognizes messages through75. The fence
uses stable gameplay epoch and ordered TCP streams, with existing transition
and acknowledgment deadlines. No engine handle appears on the wire.
