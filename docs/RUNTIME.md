# Lobby launch and interactive campaign controller

`runtime.*` connects the existing lobby, snapshot transfer and gameplay adapters
to the ordinary main-menu/game loops. This exposes campaign launch, independent
movement/stopping, NPC approaches and shared conversation choices. The planned
gameplay, services, coordination and recovery capabilities are implemented;
[STATUS.md](STATUS.md) identifies the remaining campaign integration and platform
acceptance. The versioned sections below retain implementation history rather
than an additional backlog. See `COORDINATION.md` for shared controls.

## Player flow

Choose Multiplayer from the original main menu. Host or join using the host's
LAN IPv4 address, choose a retail preset or create a character and mark Ready.
Creation uses the original identity and point-allocation screens; the native
editor can now reopen accepted builds and load a per-profile saved draft after
restarting. See `DRAFTS.md`. The background determines initial outfit/gold. See
`CHARACTERS.md`. The host's Start campaign
button becomes available once at least two connected players are ready; the
same path supports four. The host creates every owned PC and enters the normal
single-player campaign. Additional PCs receive separate inventory instances and
find distinct, pathable entrance tiles near the host. Normal campaign launch
does not alter campaign globals or preset stats.

All players receive the host's normal save snapshot over TCP, bind their own
PC by profile identity and wait for the existing loaded/commit barrier. Protocol
26 then transfers the first complete world update and waits for every peer's
applied acknowledgment before GAME_START opens controls. This supplies native
combat generations and maintained-spell presentation omitted by the archive. The
lobby remains visible with loading status while this happens. Host simulation
stays stopped through preparation/loading and the character handshake. Once
the gameplay service is running, the lobby closes and the original HUD is shown.
The host then advances normal campaign scripts, timers and NPC animation;
replicas display the authoritative world, PC poses and clocks.

Left-clicking ground sends a movement intention for the local owned PC. Normal
walk/run modifiers retain the engine's behavior. Right-click sends an owned
stop/face intention. Left-clicking a living NPC sends a host-authoritative talk
goal: the original animation/pathfinding system approaches it and invokes its
normal dialogue script. The sender cannot supply a different player's authority
or a native process handle. The initiating PC's owner chooses replies; everyone
observes. Number keys or mouse select replies, and Escape closes the conversation
only for its chooser. Escape is revision/owner validated like an ordinary reply;
observers cannot close someone else's conversation.

The native inventory panel now submits owned equipment/grid placement, Drop and
Arrange requests without optimistic object mutations. Ground item clicks use a
host approach/pickup goal; giving to another registered PC validates ownership
and distance. Item receipts wait for authoritative world application. See
`ITEMS.md` for controls, wire format, evidence and remaining item/service work.
Protocol 17 routes item use, consumables, reading and schematic learning
through the host while retaining local targeting and personal windows; see
`USE.md` for controls and acceptance limits.
Protocol 18 routes native skill controls and stealing attempts to the host,
with per-owner skill/trap feedback. The controlled fixture passes both host
directions; see `SKILLS.md` for its limits.
Protocol 19 routes the native character sheet's live allocations, refunds and
auto-level schemes to owned host edit leases. Closing accepts the current
choices; rapid reopening waits for that acceptance. Replica level-up notices
follow applied world state. See `PROGRESSION.md` for coverage and remaining work.
Normal container/dead-NPC clicks now approach and open a shared loot target,
with separate validated access for each player. `LOOT.md` describes the native
opening path, taking/putting, partial Take All and revocation/failure tests.

F12 opens the party panel during play, including during dialogue. It retains
the roster/chat, supports new chat messages, and provides Resume and Leave party.
F12 or Escape resumes when the panel is open. Character/readiness controls and
campaign start are disabled after launch. Loading waits for the whole party;
Escape/Back cannot abandon it accidentally, and Leave party explicitly cancels
the attempt. A failed session requires leaving. The panel uses the existing retail fonts and
brown/gold palette. It is local UI; opening party chat does not pause the host.

## Lifecycle and failure boundary

The lobby owns one TCP session and polls it before `coop_runtime_tick`, both
inside the menu and in the main game loop. The runtime borrows that session.
Input handlers only request launch; native character creation, save capture and
snapshot restore occur later during the runtime tick. Socket callbacks stage
bootstrap data and gameplay services process it on the engine thread.

The states include OFF, LOBBY, PREPARING, LOADING, STARTING, PLAYING, FAILED,
DRAINING, RESTING, JOURNEY and RECONNECTING.
The loaded barrier switches the installed engine handler from bootstrap to
gameplay. The immutable captured archive is retained until transfer commits.
The runtime guards native game/timer pings while loading, starting or failed.
Replicas retain their separate authority guards. During finish, the host first
lets admitted native item goals settle, then freezes simulation and drains the
final world/audio barrier. A live clock previously prevented that final world
queue from becoming empty.

Session characters/readiness are reserved for loading/play. Original owners
can return to their existing slots; unknown owners cannot join a launched party.
An ordinary-play or native operation disconnect enters RECONNECTING, freezes the world, retires old
services and synchronizes a fresh live snapshot after every owner returns.
Pause holds survive. Interrupted rest/world journeys retain completed work,
release native controls and report elapsed hours. Interrupted snapshot transfers
restart from the host's current world in a newer epoch. Chat and heartbeats remain available. See `RECONNECT.md`
for ordering, timeout, host-loss policy and native-barrier limits.

Native cross-map transfers first enter DRAINING, stop simulation and settle
world/dialogue/poses. All clients acknowledge the final revision before the host
changes maps. The runtime releases old native dialogue references, executes the
normal whole-party transfer once, captures the destination save and loads it on
every client under the next epoch. The loading party panel retains chat; controls
resume after the loaded barrier and owned-PC handshake. See `TRAVEL.md` for
validation, evidence and remaining map/service/cutscene coverage.

Unsupported disconnect barriers, terminal host shutdown, invalid replicated
state and unexpected map changes stop the controller and display the failure. The native
game and timer pings cannot continue an isolated campaign after this failure.
The runtime does not silently treat a replica as an independent single-player
session. Leaving releases native dialogue before destroying its network service,
releases bootstrap/buffers, then resets the campaign through the normal menu path.
The menu reset hook first detaches an active runtime; snapshot restore explicitly
retains the controller through its own required native reset/load.

## Movement and interaction actions

Protocol 7 introduced two intention types, retained in protocol 8:

| Type | Direction | Payload |
| --- | --- | --- |
| 35 GAME_TALK | Client → host | Epoch, command sequence, map (three u32), actor GUID (16 bytes), portable target ObjectID (17 bytes) |
| 41 GAME_HALT | Client → host | Epoch, command sequence, map (three u32), actor GUID (16 bytes), facing target x/y (two u32) |

These share GAME_MOVE's monotonic sequence, result path and limit of 30 commands
per second per connection. The host checks ownership, current map, valid state,
turn eligibility and target type/existence. Talk requires a living, available
NPC and an available conversation service. A pending talk approach reserves the
service until its animation ends or the conversation opens. Movement/stopping
are rejected while a shared conversation is displayed. Only the host enqueues
native movement/talk/stop goals. `COOP_DIALOGUE_CANCEL` is the reserved u32
option `0xffffffff` on the existing revision-bound DIALOGUE_CHOICE message.

## Verification

`tools/desktop_smoke.py --launch --controls` drives four actual normal game
executables from the main menu through host/join, character readiness, chat and
the Start campaign button. It compares initial owned identities/positions,
injects observer reply keys, advances and closes the opening conversation using
the chooser, moves every owned PC with real OS pointer input, and sends chat
through the in-game F12 panel. Screenshots still require manual visual review.
The driver uses Hyprland geometry and ydotool for pointer input because XTest
coordinates have a different scale in Wine/Xwayland windows. Keyboard input uses
the existing native/Wine paths. It refuses a detected locked desktop.

The explicit `--coop-live-probe` flag writes private runtime state/view reports
for this driver. Ordinary play does not write those diagnostic files. The
reports contain owned PC IDs/positions, role, clocks, world/dialogue revisions
and accepted/rejected action counts. Readers retry in-progress writes.

`tests/integration_bootstrap.py --mode runtime --framebuffer` exercises the same
controller in four real engines with headless SDL drivers. Its disposable fixture
creates the ready TCP roster, then invokes the runtime's ordinary deferred launch
path. It does not replace campaign values or teleport PCs to a synthetic map.
It tests the actual crash-site automatic conversation, host reply/cancel, all
four PCs' UI mouse movement and stopping, a remote NPC UI click through the
normal talk/pathfinding goal, and a second remote-owned reply/cancel. It records
six byte-identical conversation pages and requires nine accepted actions (four
moves, four stops, one talk), two rejected forged-actor/out-of-bounds NPC
requests, distinct PCs, exact final
world/story/member agreement and the final replication barrier. After recording
that state, it intentionally disconnects each local session and verifies native
timer/game pings stay frozen. An agreed final drain retains its terminal state;
unexpected loss during play still enters the failure path. The fixture then
tears down the native/network resources.

The test runs both host OS directions and supports an instrumented Linux
host/client with access, UB and leak checks. CTest also covers chooser-only
cancellation and rejects a new join during the campaign roster lock without
changing the existing revision. Earlier object/story/save/network tests remain
required regressions.

`--mode runtime-loss` repeats the actual controls and conversations, then the
host disconnects during active play before requesting any final-state barrier.
Each process must report FAILED while the gameplay service is not finishing;
native game/timer pings must leave the clock unchanged. This distinguishes the
failure guard from normal end-of-test settling. Its terminal clocks are not
required to match: connection loss can interrupt an unacknowledged world batch.
That interrupted state is frozen and reported, not claimed as a recoverable save.

## Current implementation and remaining acceptance

The earlier implementation notes above are historical. Character libraries,
starting purchases, host adapters for combat, magic, skills, commerce, crafting,
training, XP/social policy, shared movies and original ending sequences have since
been implemented; their current evidence is recorded in [STATUS.md](STATUS.md).

The latest production work implements retained dialogue choices and callbacks,
committed teleport persistence, shared-media rejoin and saved arrival FIFO order.
See [RECONNECT.md](RECONNECT.md). The planned core feature families now have
source implementations; [FEATURES.md](FEATURES.md) separates them from accepted
native behavior. Prioritize connected original campaign progression and concrete
product corrections, retaining passed matrices. Builds and focused checks follow
substantial implementation; installers remain last.

Controlled alternate endings and qualified sustained independent/fault play have
retained evidence. Linked ordinary campaign integration remains open; consult STATUS.md for its
scope rather than repeating old matrices.
Checkpoint rehosting is available; live host migration is not part of the current
implemented recovery policy. Installers remain last priority.
