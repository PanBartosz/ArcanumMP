# Returning to a running party

Protocol 25 reserves launched character slots for the original player profiles.
If a connection drops during ordinary play, the host pauses the world and the
other players keep their current campaign open. The disconnected player retries
the same host automatically, using the same `arcmp-player.id`. **Retry now** in
the native party panel starts the next attempt immediately.

Keep the original profile file when restarting the game or changing a nickname.
Joining the running host with that profile restores its original slot and
character. An unknown profile cannot replace a missing owner. Connected owners
cannot join twice. Presets, custom recipes and character names remain reserved;
readiness of the surviving players is retained.

After all owners return, everyone receives a fresh snapshot of the live host's
campaign. This preserves the current world and characters rather than rolling
the party back to its last manual save. A new gameplay epoch retires old
requests, and each client binds its owned PC before controls resume. Existing
per-player pause holds survive reconnection, including a hold belonging to the
disconnected owner. The host can use Resume all after synchronization.

Native dialogue choice pages are retained across rejoin, including the original
chooser and exact offered options. After synchronization and release of any player
pause, the host restores that page without re-entering Talk or repeating quest
entry effects. Queued script conversations retain their FIFO order and deduplicate
callbacks. Native barter/companion inventory, identification, repair, follower
crafting, passive sheets and NPC maps reopen for the original chooser, without
replaying entry actions or preparing stock again. Unexecuted requests retire;
already-applied native changes remain. Personal allocation and unrelated local
windows close during replacement.

A disconnect during a movie, slide or fade interrupts local playback and enters
the ordinary reserved-owner rejoin flow. The native script stays suspended until
the same segment restarts and completes for everyone. Recovery files keep the
last completed checkpoint while the native caller is suspended. This avoids
publishing a durable snapshot that would require an unsaved native call stack.
Abrupt host loss still uses the last completed durable checkpoint.

Native teleports already committed by scripts also persist with the campaign.
The destination, initiating character and presentation/time parameters resume
after pending conversations. The originating dialogue action, item hand-in or fare
is not repeated. Original arrival events remain native save records; newly queued
arrivals include explicit FIFO sequence numbers independent of retry deadlines.

The retained opening/ending and dialogue/FIFO/arrival continuations cover native
interruption and resumption in both host directions. Broader combinations and
sustained independent fault play remain in acceptance.
Optional `arcmp-dialogue.bin` and `arcmp-transition.bin` files in the native save
use bounded, explicit integer encodings and checksums. Older saves without these
sidecars load with no pending continuation. Old arrival records remain readable;
older executables are rejected by the network handshake before loading new records.

The waiting party panel retains chat for connected players, names the reserved
characters, marks missing owners **Rejoining**, and shows the countdown. The
grace period is two minutes. Failed client attempts retry every two seconds
after the initial one-second delay. The transport still detects silent failures
through its existing heartbeat timeout.

An explicit host **Leave party** sends a terminal reason and stops client
retries. An abrupt host loss can trigger retries while its process is unavailable;
if it does not return, use the durable **Recover party** flow with the original
profiles. Moving an active world to a different live host remains separate work.
A restarted host presenting a new unlocked lobby is reported explicitly rather
than silently treating it as the old running campaign.

Native campaign errors are terminal, rather than retryable network outages.
For example, if a saved map cannot restore its native actions, the detecting
engine freezes immediately. A guest reports the error to the host; the host
shares the cause and waits for connected players' receipts before closing.
The first error remains visible on each failed party panel, with Leave and
Back to title available. Automatic reconnect stops. Recover the last durable
party checkpoint with the original profiles; a failed restoration does not
replace it with a new snapshot. The terminal exchange gives unreachable peers
five seconds before releasing their connections.

An original campaign movie can also suspend a native spell/item/script callback.
Recovery retires its old action controllers immediately while keeping their memory
alive until that callback returns. The host then continues the original native
caller after restarting the interrupted shared presentation. The Linux and Windows/Wine-host
Device-ending fault continuations pass the real movie, fresh snapshot,
five original slides and all-owner return to the menu. Other native dialogue/media
combinations remain in acceptance; this does not establish every interruption.

## Opening-story interruption

A new party can disconnect before the first campaign snapshot exists. During the
original opening movies, the host keeps its in-progress native launch suspended.
Returning original profiles pass the reserved-owner acknowledgment barrier;
receivers and presentation state are refreshed, then the interrupted movie
restarts. Character creation is not repeated. The first campaign snapshot follows
when the opening story and destination load finish. The controlled Linux and Windows/Wine-host
opening-disconnect checks pass with probe playback; decoder/visual playback
recovery and broader media interruption remain open.

## Synchronization and wire changes

Protocol 26 retains the version-25 packet layouts but strengthens startup order.
After native archive loading and GAME_HELLO, clients apply the first complete
world update while controls stay closed. The host sends GAME_START only after
all peers acknowledge that update. This initializes the combat generation and
maintained-spell presentation even when the native save omits an inactive
combat generation. It applies to initial launch, travel, save/load and rejoin.
Recovery also retires any announced destination epoch when a client finishes
draining before the host; the next epoch exceeds both live and pending values.

The host's locked roster contains the fixed reserved-slot mask. A disconnected
row retains its character data but clears connected/readiness flags. A returning
HELLO is matched against the reserved owner identity, then receives that same
slot, original character and launch readiness. Unknown identities are rejected.

ROSTER now has a seven-byte header: assigned slot (u8), revision (u32), locked
(u8), reserved mask (u8). Four unchanged 183-byte player records follow, for
739 bytes total. Lock/unlock publishes a roster revision. Disconnected custom
records are valid only in reserved slots.

Types 68/69 are the reconnect view and acknowledgment. All integers use
big-endian encoding. A view is exactly 16 bytes:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Target gameplay epoch |
| 4 | 4 | View revision |
| 8 | 1 | Fixed participant mask |
| 9 | 1 | Currently connected mask |
| 10 | 1 | Waiting (1) or synchronizing (2) |
| 11 | 1 | Reserved zero |
| 12 | 4 | Remaining grace milliseconds, 1–120000 |

The eight-byte ACK contains epoch and revision; its connection supplies the
sender slot. Future ACKs, conflicting exact replays and malformed masks are
rejected. Older views/ACKs are harmless. The host waits for every owner to
acknowledge both preparation and synchronization. The engine adapter confirms
that old services are retired and a new snapshot receiver exists before ACKing.
TCP ordering therefore drains earlier messages, including presentation ACKs
that do not carry a gameplay epoch, before the new bootstrap starts.

Native operation interruption uses the same barrier. During rest or waiting,
completed hours, script effects and healing remain applied; the bed is released
and the rest stops. During world travel, the native route closes at its current
location, temporarily hidden PCs become visible, and the host's local character
is restored before the party is placed and saved. The camera returns to that
owned PC. Placement plans reachable, separated positions for the whole party
before moving anyone, searching nearby anchors if the sector centre cannot fit
everyone. The new party-control view
reports Interrupted with the elapsed hours. Town waiting retains its native
policy of advancing time without healing.

A dropped owner during destination/snapshot loading retires that transfer and
starts a newer epoch from the host's current world. A not-yet-executed travel,
rest, save or load request is cancelled. Already-applied native changes stay in
the host world. Reconnection does not claim that an interrupted cooperative save
was stored on every machine; the synchronized recovery file is refreshed instead.

Socket callbacks only stage reconnect views. Native goal cancellation, UI
teardown, archive capture and restoration occur in the runtime tick. The host
keeps its timer anchor current while waiting, so disconnected wall time is not
charged to the campaign on return. A roster change during a transfer retires
that attempt and requests a new epoch.

## Verification and remaining work

The portable `reconnect` test combines real four-session TCP connections,
reserved custom characters, an unknown profile, changed nicknames, reversed
return order, delayed preparation ACKs, timeout, explicit shutdown and 20,000
view mutations. The timeout uses its deterministic clock rather than a
two-minute real wait.

`runtime-reconnect` combines a remote retail dialogue chooser, a pause hold
owned by that chooser, an in-flight item request, an actual TCP drop and native
Retry button click. A second recovery drops two owners, including another
pause holder. It then exercises item drop/pickup, movement and shared dialogue
again while retaining owner GUIDs and a live script marker. `STATUS.md` records
the accepted runs; this describes fixture scope rather than campaign completion.

`runtime-reconnect-barriers` combines an active remote combat turn and maintained
spell with recovery, a fresh end-turn and spell cancellation, interrupted native
waiting, interrupted world travel, snapshot receiver loss and resumed item
actions. This scenario checks native state rather than reproducing the combat
or rest algorithms. Its coverage and accepted results are recorded in `STATUS.md`.

Native dialogue/teleport/media continuation is now implemented as described
above; runtime acceptance of those additions remains open. Unsafe unresolved
actions retain the frozen-failure/durable-checkpoint fallback.
Fresh-process rejoin, all combat/spell and bed branches, and prolonged faulty-
network sessions require wider native acceptance. Playable rejoin does not establish every campaign
barrier, host migration or a complete four-player story run.

## Travel input fence (protocol42)

A native original exit can commit travel while another human has just submitted
an action. The new all-owner input fence stops further runtime/dialogue input and
acknowledges each client's preceding TCP actions before the host seals world
replication. Outstanding actions, native completion tickets and world-bound
receipts drain while captures remain active. Existing final state/finish follows
that fence. The former impossible post-settle capture ticket now reopens capture.
The real four-engine Vendigroth continuation exposed this race; acceptance of its
correction passes original descent228 and floor229 on Linux host. Retain the
accepted prefixes and the later partial floor230 checkpoint; floor230 remains open.

## Dialogue and native panels (protocol44)

The saved native continuation includes its preceding response, original chooser,
page/options/actions, FIFO callback queue, panel mode and existing journal page.
Recovery rebinds the native reaction context and publishes that retained state.
It does not repeat Talk scripts, entry actions or barter stock/gold preparation.
Native panels are reconstructed for their chooser, including companion inventory
entries without a dialogue file. Ordered arrivals remain blocked until recovered
dialogue and its queued callbacks close normally. A restored transcript page is
reused once; later identical conversations remain distinct history.

The connected four-engine continuation passes both Linux-host and Windows/Wine-
host directions, including actual original dialogue choice, pending FIFO/arrival
recovery and a second two-owner drop with an open companion inventory. Native
acceptance of other restored panel variants remains open. Evidence is
`evidence/evil-checks/core-v44-dialogue-panels.json`.

## Ordinary starting-party recovery

The qualified original starting-party continuation in
`evidence/evil-checks/core-v44-ordinary-recovery-currency.json` uses legal five-point
lobby builds and active original NPCs, with no campaign placement/resource grants
or script edits. Independent movement, original combat, inventory, personal
options and local screens continue around two Saves, two unanimous Loads and
three transport recoveries in each host direction. All four final owners,
inventories and shared XP match.

The600s timeline composes retained prefixes and successful resumed tails after
a large host clock jump. It is not an uninterrupted10-minute process run or
OS-suspend recovery acceptance. Longer independent sessions and broader original
chapters remain required.

## Native cached-map failure (protocol47)

Cached spell/animation/timer parsing failures now propagate out of map opening.
The cooperative native failure boundary freezes the campaign and reports Recover
party. The last completed durable checkpoint remains the recovery source; the
partially replaced native world is not treated as a new accepted save. Native
cache pressure and malformed table controls pass on Linux, Windows/Wine and
sanitizers. A four-engine malformed-map failure/recovery still needs acceptance.

The current protocol47 builds also pass the14-stage four-engine combat/maintained
spell/rest/journey/snapshot-receiver loss continuation in both host directions,
with four recoveries to epoch9 and four identical CRC-valid final recovery
archives. This exercises normal interrupted state restoration; it does not
establish malformed-map recovery. Scopes: `evidence/native-core-persistence-v47.json`.
