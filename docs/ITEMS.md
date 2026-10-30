# Authoritative item interactions

`items.*` adds a discrete request/receipt service for ground pickup, owned-item
placement/equipment, dropping, giving to another registered PC and inventory
arrangement. Protocol 28 also carries native door, scenery and scripted-wall
use through this ordered service. The host executes the native inventory/script functions. Replicas
keep dragging and panel navigation local, then display the resulting object
batch. This remains an item-action subset, not complete inventory gameplay.

## Ordering and authority

Each participant has one outstanding item request and an independent, strictly
increasing sequence. A bounded per-peer record retains the last exact request
and its receipt. Repeating that request returns the cached outcome without
running native code again; reusing its number with different bytes is invalid.
The service limits new requests to 30 per second per peer. Transport buffers
remain bounded by the existing session service.

The message callback validates/stages bytes. Native operations run later in
`coop_gameplay_poll`, outside socket/input callbacks. The host validates the
sender's profile/PC binding, actor GUID, map, availability/status and combat turn.
Shared dialogue and gameplay finishing prevent new item execution, except
the chooser may arrange their own inventory during barter.

Owned-item operations require the item to belong to the acting PC. Giving also
requires another active registered PC within eight tiles. Pickup requires an
available ground item within 30 tiles, then uses the normal approach/pickup
animation goal and GET script. The completion receipt waits until the item is
owned by the actor; merged gold/ammo use the native quantity outcome. Failure,
interruption or a 15-second pickup deadline cancels a remaining pickup goal.
Gold/ammo pickup and cancellation still need dedicated fault fixtures.

Equipment uses native slot/type/size/hand/hex checks and swap rollback. Grid
placement validates an available cell and preserves the old location on failure.
Dropping and giving retain native DROP/TRANSFER scripts and inventory rules.
Whole-item giving can merge stacks through the native engine. Protocol 20 adds
partial gold/ammunition gifts through the native quantity picker, with host
stack/balance/capacity bounds. Arbitrary bag/loot stack splitting remains open.
NPC service transactions use `services.*`; see `COMMERCE.md`.
Protocol 16 routes consuming/reading/throwing through the separate
ordered action service; see `USE.md` for exercised and unverified branches.

## Cooperative keys

The native door/key check supports the cooperative campaign registry rather
than relying on the disabled legacy network's key-sharing option. Another active
human within eight tiles can lend a key, including a key stored in a native key
ring. Active owned followers within eight tiles can also supply their keys, even
when their human owner is farther away.
The item remains with its owner; no key is copied, granted or consumed by sharing.
Unrelated NPCs and distant/incapacitated lenders are excluded. Ordinary single
player and the original legacy-network path retain their native behavior.

The original bridge/camera continuation now exercises party gold pooling, the
normal1000-gold Lukan dialogue payment/key grant, the original bridge gate and
native key checks at five/nine tiles. Key possession, payment, camera loot/gift
and their native saves are actual ordinary-party state. The exact remote gate
activation still needs a clean receipt; an already-open gate and a refused late
Use do not establish that interaction. Broader key-ring/follower variants remain
open. The camera hand-in and Tarant continuation now have qualified native evidence
in `STATUS.md`; the final city party retains the original key through Save/Load.

## Receipt and world application boundary

A successful operation requests a world capture after execution. Its receipt
includes the resulting world revision. When a capture finds no field changes,
its capture serial still advances; this supports successful no-op placement or
arrangement without inventing a field mutation. An older pending batch must
finish before that requested capture can occur.

A replica waits for the receipt's world revision to be **applied**, rather than
merely seeing WORLD_BEGIN, before clearing its pending action. This keeps the
inventory cursor and next request aligned with authoritative object references.
Native rejection receipts have revision zero and return a native item reason or
a specific ownership/target/distance/interruption message. A 30-second unresolved
local request fails the gameplay connection through the existing freeze boundary.
This is not reconnect/recovery or a durable save transaction.

The normal finishing/travel barrier also waits for the item service to settle.
Inventory UI releases references and clears its display drag before gameplay
destruction or a map replacement.

## Native script callback lifetime

Pickup, inventory transfer, insertion/removal and merchant buy callbacks can
consume an item, move it themselves or replace the native world. The inventory
path retains permanent identities and re-resolves them before continuing after
those callbacks. An item already moved by its GET/TRANSFER script is not inserted
a second time. Consumed INSERT_ITEM objects receive no later inventory feedback;
REMOVE_ITEM consumption stops the enclosing transfer. Commerce revalidates both
participants before charging and avoids touching a consumed source item after a
successful scripted hand-in. These checks retain native callback outcomes and
do not roll back original story side effects.

Gold/ammunition INSERT_ITEM callbacks also revalidate the source stack and both
inventories before debiting or crediting. Quantities are re-read after the callback;
consumption or reparenting stops the transfer. Stack creation/transfer failure is
reported. Barter rechecks identities after payment as well as after item transfer.
A REMOVE_ITEM callback consuming its already-detached object cannot remove it
from the same inventory twice. That narrow reentrancy guard retains the native
parent/triggerer context and permits legitimate removal after reinsertion.
Seven native destruction controls pass in both host directions with no parent-
mismatch or sanitizer errors: `evidence/native-currency-removal-guard-v44`.

## Scripted drop and equipment changes

Native DROP, WIELD_ON, WIELD_OFF and REMOVE_ITEM callbacks now revalidate the
original item/parent identities, owner and inventory location before continuing.
A consumed or transferred object is not inserted, dropped or invoked again by
the caller. The recursive-removal context covers unequipping as well as the
final removal hook, preventing a consuming WIELD_OFF from removing the same
inventory entry twice. Original native callbacks and their triggerer remain in
use.

Equipment swaps re-read the occupied slot after callbacks and preserve a
remaining detached item with native bag insertion. If a callback has filled the
bag, ordinary native ground/junk-pile placement preserves that item. No existing
item is overwritten. Unequip and native equipment-effect refresh also stop if
the original object changes. Inventory insertion skips a stale INSERT_ITEM
callback after WIELD_ON consumes or transfers the item. A junk-pile insertion
that consumes the dropped item no longer schedules decay against it.

Item-spell activation re-resolves the whole request after equipping its weapon:
map, turn, source, target, ownership and resource checks apply to the resulting
native state. A successful equip's postcondition bounds this to one additional
validation pass, before the spell is submitted or charged. All three builds pass. The callback-only continuation accepts 15 native controls
in both host directions with eight clean logs, including consuming DROP,
WIELD_ON/OFF, REMOVE_ITEM and refresh hooks, replacement equip and a normal
equip/unequip/drop. Evidence: `evidence/native-equipment-callbacks-v45`. Actual
scripted owner transfer/full-bag and item-spell target/map/turn mutation variants
remain unaccepted.

## Item use, enchantment callbacks and service failure (protocol47)

Native object use retains the original actor, item, parent and target identities
through SAP_USE. A consuming script completes without a second default effect;
a transfer or map/turn replacement also stops that old default. A script that
suppresses default and consumes its item still charges the surviving original
actor's native AP. Successful magical use and item-used triggers recheck the
item/parent before consumable cleanup, including explicit tile use. Rearranging
an item within its bag alone does not cancel use.

Enchantment slot iteration stops after identity, ownership, equipment slot or
map replacement. Unwear/drop effects recheck after removing maintained effects;
combat equipment iteration checks its original actors between items. Attack
attribution comes from each attack event's actor, so later equipped items and
nested notifications cannot lose or inherit a global attacker.

Merchant quote/payment continuations now retain the original map, actors and
substitute stock inventory. A refused scripted payment cannot roll back through
removed actors or a new map. Ammo rollback requires the exact expected before/
after totals; ordinary items must still belong to the intended recipient.
Identify/repair UI refresh also checks identities after payment callbacks.
These guards preserve script side effects rather than undoing original quests.
Current native acceptance is recorded in STATUS.md; broader transfer/enchantment
and service-failure variants remain open.

## Original inventory UI

The native paperdoll, bag grid, fonts and controls remain in use. Cooperative
dragging holds a local presentation reference; it does not remove the object
from its owner's inventory. Releasing onto a bag cell or equipment slot requests
placement. Right-clicking equipment requests unequip; right-clicking a wearable
bag item requests equip. The Drop and Arrange controls submit host requests.
Escape cancels a held cursor or closes inventory without changing item ownership.

Dragging out through the local map lens keeps the item on the cursor. A normal
world click on another registered PC requests giving; a ground click requests
dropping at the acting PC. Ordinary world clicks on items request pickup. The
giving service is verified through its command API; this full lens/world drag
gesture still needs a desktop input fixture. Wearable hotkey changes use the same
service. Other hotkey/item/spell paths are not exhaustively guarded yet.

Applied batches refresh inventory slot indices and cancel a drag whose object
vanished or changed owner. An inherited `inven_ui_update(NULL)` branch matched
an absent target before the refresh-all case, leaving the owner's slot table
stale on clients. The target-specific branch now requires a non-null object.
Cooperative panel creation also skips native inventory repair mutations, and
does not clear an authoritative stolen-item flag merely by displaying it.

The local PC's ordinary inventory uses this adapter. Protocol 10 adds shared
container/dead-NPC access, taking/putting, target-grid
placement and partial Take All; see `LOOT.md` for current behavior and evidence.
The earlier sections describe the protocol 9 item foundation. Protocol 20 adds
barter, follower inventories, NPC identification/repair and schematic
combinations through the service controller; see `COMMERCE.md`. Protocol 18's
native stealing and personal skill feedback are described in `SKILLS.md`.
`USE.md` describes
the protocol 16 item-use addition.

## Protocol evolution and current format

All scalars are big-endian. No native structs, handles or pointers cross the wire.

| Type | Direction | Exact payload |
| --- | --- | --- |
| 42 ITEM_REQUEST | Client → host | Epoch, item sequence, map (u32 each); actor GUID (16 bytes); operation (u8); item ObjectID (17 bytes); recipient campaign member (u8); inventory location (i32) — 51 bytes |
| 43 ITEM_RESULT | Host → client | Epoch, item sequence, status, required applied world revision (u32 each) — 16 bytes |

Protocol 10 appends a u32 loot access generation to ITEM_REQUEST (55 bytes).
Protocol 12 appends expected turn generation (u32 at offset 55), giving 59 bytes.
A stale generation returns unavailable without executing the item action; loot
closure may still release its access lease.
Its LOOT_ACCESS view event and operations 5–10 are specified in `LOOT.md`.
Protocol 18 adds operation 11 and a view-mode byte for native stealing; see
`SKILLS.md` for roll completion, personal feedback and the remaining acceptance.
Protocol 20 adds operation 12, give amount. The location field carries a positive
signed-32-bit amount for owned gold/ammunition or one ordinary item. The host
verifies sender/actor/recipient ownership, distance, availability, stack bounds,
native removal/insertion rules and receiving balance overflow.
Protocol 28 adds operation 13, world use. The target field contains the door,
scenery or scripted wall ObjectID; recipient and access generation are zero and
location is -1. Payload size stays 59 bytes. The host admits an active owned PC
on its current combat turn and validates target type, visibility/status, map,
30-tile distance, polymorph and native action-point availability. Beds retain
the shared-rest route. The ordinary `AG_USE_OBJECT` approach, interaction path
flags, key checks, locks, scripts and door animations execute on the host.
Protocol 33 tracks tile movement during container/world-use approaches: 15
seconds without progress or 90 seconds total ends the goal. Initiating clients
allow 105 seconds for its ordered result instead of expiring at 30 seconds.
The exact native goal is tracked through completion, with dispatch-time turn
generation and actor checks. Protocol 29 performs the final check before
native action-point consumption and rejects unavailable targets. Receipts wait for the resulting world revision.
Already admitted goals finish during shared travel drain. See `history/CAMPAIGN_PROGRESS.md`
for script ownership and campaign-exit behavior.
Operation numbers are pickup 0, placement/equipment 1, drop 2, give 3 and arrange
4. Arrange uses a null ObjectID and location 0/1 for its native orientation.
Placement accepts a bag cell 0–119, a wear slot 1000–1008, or -1 for unequip.
Item identities must be GUIDs or validated positional identities; out-of-map
coordinates are rejected before native object lookup. Status zero is success;
1–19 retain native ItemCannot reasons. Service statuses 1000–1005 mean
unavailable actor/action, ownership, target, distance, native refusal and
interruption, respectively. Item sequencing is separate from movement results.

## Evidence

`tests/integration_bootstrap.py --mode runtime-items --framebuffer`, using two
Linux and two Windows private runtimes, runs both host OS directions. The fixture
uses the ordinary campaign controller and real preset inventories. Native TIG
input drives equipment right-clicks, Drop, pickup targeting, Arrange and grid
dragging, including closing inventory through its normal primary mouse button.
Giving and forged actor/target requests use the public command adapter.

Each of four players completes ten stages: unequip, equip, drop, pickup, give,
rejected actor forgery, rejected access to its given-away item, arrange, drag a
received weapon to cell 80, and rejected out-of-bounds positional item identity.
Assertions cover 28 successful native operations, 12 rejected requests, exact
last-request replay without a second execution, a single outstanding request,
staging without optimistic field mutation, preserved IDs and final circular item
ownership. Final inventories and story bytes match in all four processes.

`evidence/items-actions-final/{linux-host,windows-host}` records the final run,
with whole-engine ASan/UBSan/leak checks on one participant. Inventory and game
frames from every participant are captured through the composed TIG framebuffer;
manual review covers all 16 frames. These are native UI events and framebuffer
captures, not a claim of OS desktop item-input acceptance or physical Windows QA.

`items-grid-actions` is an earlier ten-stage pass. `items-initial` and
`items-grid-diagnostic` exposed the stale owner-grid refresh. The subsequent
`items-grid-refresh-fix` completed all seven original action stages but failed
its final screenshot setup because the diagnostic finish barrier blocked opening
the read-only inventory view. `items-inventory-view-fix` passed that seven-stage
fixture. Failed trials remain recorded rather than counted as passing evidence.

Portable world-network tests now check an explicitly requested capture and the
applied-revision boundary. Ordinary movement/dialogue, travel, network framing
and save/object restoration remain required regressions. Dedicated tests for
pickup contention, disconnect during an item action, cursed/full inventories,
stack merges/splits, combat AP costs and actual story-item scripts remain open.

## Scripted examination

Protocol 31 adds operation 14 for native SAP_EXAMINE. The authenticated owner,
epoch/map/turn and target lifetime are checked before running any script. Ground
targets must be within 30 tiles; inventory targets must belong to the actor or
an opened loot target within three tiles. There is no AP charge. The same ordered
receipt/world barrier captures script side effects. Local descriptions remain
immediate; native hover refreshes throttle identical requests to one per 500 ms.
Shared script print/float feedback is preserved in the saved conversation journal.
The retail Black Mountain Clan pillar has a four-engine remote inspection fixture
in `history/CAMPAIGN_PROGRESS.md`.

## Personal quickbar ownership

Protocol38 separates each owned character's shortcuts from the host's global
native UI save. The ten numbered slots and two recent actions persist locally in
`arcmp-quickbar-<profile><character>.bin`, with explicit bounded fields, stable item
IDs/prototypes and a checksum. Bindings restore after the player's owned PC is
selected in the new snapshot. Missing/damaged preferences use defaults; they do
not prevent campaign loading. Legacy item slots import from that character's
actual inventory rather than another player's UI layout.

Dragging an item, skill, spell or item spell onto the bar creates a local shortcut.
Dragging between slots swaps shortcuts. It does not drop, reinsert, hide or move
an inventory object on a replica. The native inventory continues to hold the item.
Activation still validates ownership and uses the existing host action service.
An enchanted weapon's item-spell shortcut requests targeting locally; the host
performs the native weapon swap before casting, with normal removal/hex/space
checks. A missing item can rebind to an owned item of the same prototype, and
stack quantities refresh from replicated inventory.

For a legacy item physically stored in an old native quickslot, drag it into a
free inventory space before removing its last shortcut. This avoids making that
legacy inventory object inaccessible. Recent actions retain independent item
references and use their actual native item icons, including after save/load.
All12 slots reconcile consumed/replaced stacks; removed spells clear their
shortcuts. Native snapshot loading cannot store a partially loaded host bar over
the player's preferences.

`evidence/native-preferences-roundtrip-v44` passes both host directions with four
distinct characters: native inventory-to-slot drag, both recent item references
and icons, Fate activation, rapid personal options and shared Save/Load retaining
their individual bars and exact Fate cost. Wider skill/spell/item-spell gestures,
transport retry and consumed-Fate races remain unaccepted.

## Partial loot and drops (protocol39)

Gold/ammunition transfers between owned inventory and a currently leased corpse or
container now use the original quantity picker. Dropping those stacks also lets the
player choose an amount. Take All retains whole-stack behavior. The exact access
lease and source ownership are rechecked with available quantity, native removal,
capacity and overflow. Stealing retains its original bounded attempt. A partial
drop allocates its ground stack before subtracting its source, then uses native
placement and decay. This closes the earlier partial loot/drop implementation gap;
native runtime acceptance remains pending.
