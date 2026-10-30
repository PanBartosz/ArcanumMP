# Host authority and native action adapters

Four independently owned humans use one authoritative campaign simulation.
Movement, combat, magic/item use, skills, inventory/loot, dialogue/services,
crafting, progression, followers and shared campaign controls have native host
adapters. Object batches carry their authoritative outcomes; replicas render
and request actions. Original single-player content and networking-disabled
campaign semantics remain in use. The current implementation/acceptance
inventory is [FEATURES.md](FEATURES.md); remaining full-campaign and reliability
work is recorded in [STATUS.md](STATUS.md#still-required).

The movement foundation and its original controlled checks below are historical.
See the linked capability documents for current action behavior and evidence.

## Authority boundary

`coop/authority.*` gives the engine an explicit local, host or replica role.
The ordinary game defaults to local authority. The host continues normal
single-player script semantics; legacy networking stays disabled.

Snapshot restore enters replica authority before resetting/loading the client
world. A gameplay adapter adopts host or replica authority for its lifetime.
`gamelib_ping`, `timeevent_ping` and `script_execute` reject campaign execution
on replicas. Native action adapters validate ownership and legality on the
host; local UI handlers retain presentation duties. Wider native combinations
and original-content coverage still require acceptance.

The ordinary object position setter can invoke tile scripts and map exits.
`object_move_replica` updates render/sector placement, light position and local
camera/roof state without those movement consequences. Authoritative poses use
this path. Render-owned text/overlay flags are preserved locally. They cannot
be reconstructed by copying host flags alone.

## Movement service

`coop/gameplay.*` retains the live session after bootstrap. Each remote client
first confirms its own permanent PC ID; the host maps the connection's saved
owner to its campaign member and permits movement only after the corresponding
START is queued. There are no process handles in action/pose messages.

Every movement request carries epoch, monotonically increasing command number,
map, actor GUID, target tile and walk/run selection. The host verifies the
connection's ownership, map bounds, PC availability/status, combat turn and
the engine's action eligibility, then schedules normal animation/pathfinding.
Wrong ownership or invalid destinations receive a rejected result without
executing an action. Duplicate/stale command numbers, malformed messages,
illegal directions and more than 30 requests per second disconnect the sender.
The host's own movement uses the same validation.

The host publishes all owned PC poses at most every 50 ms. Records include GUID,
tile, pixel offsets, current art/frame ID, object flags, HP damage and fatigue
damage. A client validates the entire frame before applying it. Pose revisions
increase; stale, wrong-map or mismatched-PC frames are rejected. Under queue
backpressure, newer poses replace the need to send a skipped tick; no simulation
prediction or interpolation is implemented yet. The adapter stops
on unexpected map/roster/connection changes and requires resynchronization.
The runtime now drains a normal cross-map transfer before replacing the adapter
with a fresh snapshot/handshake under the next epoch; see `TRAVEL.md`.

The diagnostic finish barrier first drains object batches, publishes a final
pose revision, receives all clients' acknowledgments, and only then allows the
host probe to finish. Protocol 8 also uses this barrier for travel, with the next
epoch in GAME_FINISH. It is not a cooperative save transaction or reconnect policy.

## Verified fixture and limits

Run `tests/integration_bootstrap.py --mode movement` with the same four private
runtimes as the bootstrap test. The actual host enters the campaign, then moves
the party to the empty retail shopping map for a controlled eight-second test.
All four processes submit an out-of-bounds request and an attempt to move
another owner's PC, then an authorized movement to a separate target. The host
accepts four valid actions and rejects eight invalid actions. All four PCs walk
or run through the original pathfinding/animation engine.

Each client deliberately corrupts its own local pose once. Subsequent host
updates repair it. Each process must observe multiple tile changes; all must
end with the same exact positions, offsets, art IDs and damage fields. The
driver requires a fresh result, successful exit, PASS markers, final barrier,
different owned local PCs, matching inventory/stat/equipment records and the
byte-identical bootstrap archives. It runs both host OS directions.

The probe calls `gamelib_ping` on all participants and checks that simulation
advances time locally only on the host. Clients receive the host's displayed
clock through story batches. The instrumented Linux engine runs as both host and
client with ASan, UBSan and leak checking; use `--sanitized-runtime` without
screenshots. Ordinary visible runs capture all eight game windows for review.

The same fixture now creates and transfers an item, updates PC stats/name,
sparse script data and a PC quest entry, resets an item field, and creates then
deletes another item. All four processes must agree on the final object graph
and mutation values. Full member inventories are compared as well as the
explicit mutation report, catching duplicate/stale reference problems.
It also exercises shared quests/rumors, full quest rewards once per PC, global
script/quest/area/rumor/generator/newspaper state, repaired client corruption and
identical clocks/journal order. One client deliberately delays its world ACK
across finishing to check the final revision boundary. See `STORY.md`.

This fixture does not prove combat, NPC behavior, arbitrary campaign object
creation/deletion, interactive inventory actions, dialogue, story quests,
shared pause/rest, interruption/recovery, arbitrary network faults
during actions, long-session behavior or full campaign completion. A separate
retail dialogue fixture now verifies shared chooser/observer conversations;
see `DIALOGUE.md` for its coverage and remaining services. Other requirements remain
part of the unchanged objective. The service is now wired to interactive movement/stop/NPC mouse input and
the lobby launch button. The separate runtime fixture exercises this path at
the crash site; see `RUNTIME.md`.
