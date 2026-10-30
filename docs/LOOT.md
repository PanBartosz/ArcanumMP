# Shared container and corpse looting

The item service now supports host-authoritative access to containers and dead
NPCs. Multiple registered PCs can open the same target and see its current
inventory. Every transfer checks the current owner on the host; simultaneous
requests for one item can transfer it only once.

Protocol 18 extends access events with a mode byte for native stealing attempts;
see `SKILLS.md` for that implementation and its acceptance work. Barter, follower
inventories, NPC identification/repair services, complete item use and a whole
campaign remain unfinished.
The fixture exercises a retail corpse and native chest prototypes at the crash
site; it does not prove every story container or corpse script.

## Native opening and local presentation

A normal click on a container or dead critter submits LOOT_OPEN. The host checks
the acting PC's ownership, map, state, target type and a 30-tile approach limit,
then queues the native AG_USE_CONTAINER goal. Its ordinary movement/use path
charges the native action cost and reaches the loot callback. Both native loot
callback entry points route through the cooperative service, including requests
initiated by host scripts. A remote PC never opens a loot window on the host.

Protocol 33 preserves the native Use interaction-path flag for cooperative
LOOT_OPEN. The approach solver tries alternate native radial positions when
the nearest one is blocked; a display case beside a wall must not end the use
goal just because that first approach tile has no route.

The callback stages a permanent target identity. The next item poll runs native
visibility, lock/UNLOCK and USE-script validation using the correct acting PC.
Container opening and spawn-once generation remain host operations. The service
arranges the target inventory and schedules the native source timer when its
first viewer opens it. Each new viewer receives its own validated access; an
already granted target can be presented again without rerunning its USE script.

The host publishes an access generation, target identity and required world
revision. The receiving player waits until that revision has been applied,
then opens the original two-panel loot UI. Panel construction skips native
opening, loot generation, inventory repair and decay mutations. Other players'
windows are not opened or switched by this presentation event. Shared dialogue
defers a new loot window until conversation presentation has ended.

A native goal that ends before the callback returns an interruption, or a
specific locked/jammed/magically-held result when that state applies. A native
USE/UNLOCK refusal is also returned to the initiating player. Open requests have
a 15-second deadline without tile movement, with a 90-second absolute approach
limit and a matching 105-second initiating-client receipt deadline. Scripts that
refuse after changing the campaign
still require a new world capture before their receipt becomes actionable.

## Access and transfers

Each connection has at most one current loot target and a monotonically
increasing access generation within its gameplay epoch. Access is bound to the
host's existing connection/profile/PC mapping. These generations are ordering
tokens, not secret credentials. The current map epoch still guards every frame.

TAKE, PUT, target-grid MOVE and TAKE_ALL require that generation, a living active
actor, the same target, current item ownership and a distance of at most three
tiles. New disabling states, a target that disappears or becomes alive, moving
away, a newly applied lock condition, explicit close and gameplay finishing
revoke access. A stale close is a harmless no-op and cannot close a newer grant.
Taking another PC's items or using an old grant is rejected before transfer.

The host calls the native removal/insertion and TRANSFER-script paths. Taking
into an equipment slot validates its requirements and reports partial completion
if the item enters the bag but equipping fails. A script can change campaign
state even when refusing; receipts for such operations wait for a new capture.
As with other item operations, clients keep dragging local to presentation and
wait for an applied world revision before sending their next request.

TAKE_ALL snapshots up to 4,096 permanent item identities and processes at most
eight per peer per poll. Each item is resolved and rechecked immediately before
transfer. An item already taken by another participant is skipped. A failed
capacity/type/script check leaves that item in the target; successful transfers
remain committed and the receipt reports partial completion. The operation stops
if access or action authority changes. It is not an all-or-nothing transaction.

Container viewers share a balanced native item-decay hold. Closing one viewer
does not close the container for the others. The last viewer releases the hold,
closes non-sticky container art and runs native empty-container cleanup. Client
panel destruction does none of these campaign mutations. Map/service teardown
releases remaining access before its objects are deleted. Host loss uses the
existing frozen failure state; reconnect and recovery remain pending.

## Controls

The original inventory/loot panels, target scrollbar, paperdoll and fonts remain
in use. In a loot window:

- Right-click a target item to take it; right-click an owned bag item to put it.
- Drag between panels to take/put into a selected slot. Dragging within the
  target grid requests placement under the same access grant.
- Take All requests the bounded transfer operation; Arrange affects the acting
  PC's own bag. Followers and other service modes remain unavailable here.
- Escape cancels the held item cursor or closes the window and releases access.

Applied object updates refresh both panels and cancel a drag whose item changed
owner. When a newer access event is waiting for its world batch, mutating mouse
events cannot fall through to the original local drag/removal path. The party
panel and shared conversations retain their existing behavior.

The command fixture covers target/owned right-click transfer and Take All. Full
drag gestures between loot panels, corpse equipment slots, target scrolling and
OS desktop input still need dedicated acceptance coverage.

## Protocol evolution

ITEM_REQUEST (type 42) grows from 51 to 55 bytes: append access generation (u32)
after the inventory location. Earlier item operations retain their field meanings.
Their unused access field is ignored. Protocol 12 further appends expected turn
generation (u32 at offset 55), giving 59 bytes. LOOT_CLOSE can release a lease
from an old turn; other item actions require the current generation.
ITEM_RESULT (43) remains 16 bytes.

| Operation | Number | Item field | Location |
| --- | --- | --- | --- |
| LOOT_OPEN | 5 | Container/dead NPC ObjectID | Unused |
| LOOT_CLOSE | 6 | Unused | Unused |
| LOOT_TAKE | 7 | Item currently in the granted target | Bag/wear slot or -1 |
| LOOT_PUT | 8 | Item currently owned by the actor | Target bag slot or -1 |
| LOOT_TAKE_ALL | 9 | Unused | Unused |
| LOOT_MOVE | 10 | Item currently in the granted target | Target bag slot |
| STEAL_OPEN | 11 | Living NPC ObjectID | Unused |

LOOT_ACCESS (44, host to one client) is exactly 30 bytes in protocol 18: epoch (u32), access
generation (u32), required applied world revision (u32), portable target ObjectID
(17 bytes), mode (u8: loot 0, stealing 1). A null target requires mode zero and
revokes access. Positional target coordinates and map
are validated before native lookup. Newer access states supersede older pending
presentation; intermediate windows need not open when access has already ended.

Additional result statuses are 1006 access denied/expired, 1007 partial transfer,
1008 locked, 1009 jammed and 1010 magically held. These extend the status values
in `ITEMS.md`. Request sequence/replay rules still apply to all loot operations.
Stealing uses native attempts rather than unrestricted transfers. Take All and
rearranging the target's inventory are refused in stealing mode. See `SKILLS.md`.

## Verification

Run `tests/integration_bootstrap.py --mode runtime-loot --framebuffer` with two
Linux and two Windows private runtimes; add `--sanitized-runtime` for one Linux
engine. Both host directions run automatically. The fixture creates two retail
chests, marks an existing retail corpse, and inserts independent tagged items,
a deliberately overweight item and 43 gold. It uses the ordinary runtime
controller and actual native inventory/animation functions.

All four PCs complete 19 coordinated stages over the live session. They share
the same chest, compete for one item, right-click individual items out and back,
reject stale grants, use Take All, close/reject expired access, attempt a jammed
chest, open the corpse and compete for a second item, reopen/revoke on a new jam,
move away and reopen, then reject an old close against a newer grant.

`evidence/loot-access-final/{linux-host,windows-host}` verifies exactly one winner
for each contested item, exactly one instance of all 19 tagged objects, expected
final ownership, no transfer of the overweight item, gold conservation, shared
final inventories/story bytes and balanced native item-decay state.
Closing the host's view first also keeps the container open and its decay hold
active while the three remote viewers deliberately wait before closing.
Open-loot snapshots and final game frames are recorded for all four participants.
These are composed TIG frames with SDL dummy/software drivers, not desktop
captures or physical Windows acceptance. Whole-engine ASan/UBSan/leak checking
remains enabled on the instrumented participant.
All eight open-loot frames and all eight final game frames from that final run
were reviewed: the original two-panel layout remains readable, each player's bag
is distinct, and all views share the target's inventory.

`--mode runtime-loot-loss` disconnects the host after all four windows have opened,
before item transfer. Each process must freeze in FAILED without a gameplay
finish barrier, then release all native access holds on teardown. Interrupted
story clocks are not claimed to agree. Evidence: `evidence/loot-loss-final`.

The first trial, `loot-initial`, passed the first eight stages and stopped on the
generic interruption result for a jammed target. `loot-jammed-feedback` passed
all 19 stages with a specific result. These remain recorded separately from the
final run. `loot-items-regression`, `loot-travel-regression`,
`loot-runtime-regression` and `loot-campaign-regression` retain the earlier item,
map-load, interaction, object/subsystem and save-transfer coverage.

Still required: actual quest-item scripts across the campaign, pure locked/key
and magical locks, trapped/container guard behavior, theft and services, item
use, combat/AP policy, stack quantity UI, prolonged large inventories, pending
transfer interruption/recovery and a documented manual campaign playthrough.
