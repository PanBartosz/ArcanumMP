# Object replication

The host publishes canonical object records alongside PC poses and the campaign
state described in [STORY.md](STORY.md). This guide describes representation,
authority and the scope of earlier native checks. Current integrated campaign
coverage and remaining work are recorded in [STATUS.md](STATUS.md).

## Representation and authority

`object_state.*` is a portable codec with no engine dependency. It uses explicit
big-endian values and permanent identities, never process handles or native
struct padding. `object_fields.inc`, compiled inside the engine's private
`obj.c`, translates native object fields without exposing their memory layout.

Every overridden normal field is represented. A complete record replaces the
previous set of overrides, so fields and sparse-array entries can also be reset
or removed. Unchanged values inherited from the same content-matched prototype
remain inherited. Supported values are 32/64-bit integers, strings, scalar and
array references, sparse integer arrays, script slots and PC quest arrays.
Script records preserve flags, counters and script number; quest records
preserve days, milliseconds and state, omitting native padding.

Runtime pointers, light handles, render caches and other transient properties
stay local. Native overlay/underlay art arrays, including their current frames,
and light art/color fields are ordinary replicated values. Text flag bits remain
local. Overlay visibility bits are rebuilt from the installed art arrays when
reattaching a replica; retaining the previous local bits hid newly received
effects and could leave removed layers visible. PC poses preserve these derived
bits until the corresponding world record installs its art. The same native
layer scan serves ordinary overlay changes and replica attachment, without
starting client animation goals or invoking spell callbacks. Attachment avoids
creating a flags override when the derived value is unchanged.
References are exported as permanent IDs without freezing the live object.
Obtaining an ID may assign one through the engine's ordinary ID mechanism.

The host tracks exact canonical bytes, producing a baseline and then changed
records and GUID deletions. The tracker is scoped to one map. It compares state
every 200 ms after the preceding batch has been acknowledged by every peer.
PC location, offsets, current art ID, flags, HP damage and fatigue damage remain
on the 20 Hz pose path. An explicit record option excludes those fields from PC
world records, preventing a slower world transfer from rolling movement back.
Overlay frames consequently retain the 5 Hz world cadence and acknowledgement
barrier. Effects shorter than a capture interval can still be missed; this is
not a complete transient effect, projectile or sound presentation service.

## Applying a batch

The client validates the entire envelope, object schemas, sorted fields, IDs,
prototypes, coordinates and references before replacing existing normal fields.
Missing GUID objects are allocated with their supplied identity, then references
are resolved after the whole new object graph exists. Missing positional objects
must be found through the engine's sector lookup. Wrong prototypes, duplicate
IDs, dangling references and attempts to delete campaign PCs are rejected.
References from unchanged objects also prevent deleting an object still in use.

On validation failure, staged allocations are discarded and newly allocated bare
objects are removed. Existing normal fields have not been replaced. This is not
a transaction over every engine subsystem: positional lookup can load sectors,
ID assignment can update lookup metadata, and bare-object allocation has pool
side effects.

Successful application detaches changed objects from local render/sector state,
installs their normal fields, removes deleted objects and rebuilds presentation.
It does not replay inventory actions, scripts or AI. The ordinary host engine
still executes those consequences once. Replica authority suppresses campaign
simulation; direct UI action routing remains required before interactive play.

`world_net.*` sends one immutable bounded batch at a time using BEGIN, contiguous
CHUNK and ACK messages. Protocol 59 optionally compresses the immutable transfer
with zlib, while retaining the same canonical state and checksum. Incompressible
batches stay raw. A client bounds decompression, requires exact output/complete
stream consumption, checks CRC and applies a complete batch during
polling, then acknowledges it. Socket callbacks only receive bytes. The host
never advances to another batch until all peers acknowledge the current one.
The diagnostic finish barrier first drains world changes, then acknowledges the
final PC poses. It does not implement cooperative saves or recovery.

Limits are 16 MiB per decoded batch and per tracker record cache, 131,072 tracked object
identities per map, 256 KiB per object, 512 fields, 4,096 entries per sparse array,
keys up to 65,535 and 4,096 bytes per string. A capture failure ends this adapter;
the tracker must not be reused after a partially failed capture. Transfer work is
limited to 24 chunks per peer per poll, with the existing socket queue bounds
and a 30-second progress timeout. See `PROTOCOL.md` for the byte formats.

## Identity and cache corrections

CE originally generated GUIDs from `rand()` seeded with whole seconds. Native
processes launched together could reproduce the host's existing IDs when
allocating a replicated object. Its temporary GUID replaced an existing lookup
entry; the legacy ID-removal routine retains aliases. The four-engine comparison
caught an inventory reference resolving to the newly created weapon.

GUID generation now uses OS entropy, independently of the gameplay random seed.
Creating an instance with a supplied permanent ID registers that ID directly,
without first registering a temporary GUID. The legacy removal behavior is
otherwise retained. A separate regression found that restoring a requested free
pool slot assumed it was already at the top of the free stack. The request now
moves the actual slot to the top before allocation. The object probe verifies
that subsequent allocation preserves the restored object's identity and fields.

Replica light creation sets a render ownership bit used during destruction.
Clearing render flags after creating lights lost that bit and leaked light and
palette allocations. Replica attachment now clears stale flags before rebuilding
lights, preserving the ownership bit. Whole-engine leak checks cover this path.

## Verification and remaining boundaries

`tests/integration_campaign.py --object-fields` adds whole-engine object checks
to the normal creation/travel/save suite. At the retail crash site, it exports,
stages, resolves, installs and re-exports 7,846 loaded objects (2,202,558 encoded
bytes), comparing exact canonical bytes. It restores a 2,233,958-byte world
baseline, rejects truncation and still-referenced deletion without replacing existing fields, verifies an
unchanged capture is empty, and exercises new-item recreation and deletion.
The current combined baseline is 2,244,087 bytes including the 10,125-byte story
payload. It also exercises the GUID and free-slot regressions. Use a separate instrumented
runtime for access, UB and leak checking; no findings are suppressed.

`tests/integration_bootstrap.py --mode movement` additionally mutates the real
host's shopping-map fixture over eight seconds. It creates a weapon with custom
fields and a sparse script counter, changes a PC's strength/name and another PC's
quest entry, transfers the weapon into the second PC's inventory, resets an item
field, and creates then physically deletes another item. Four actual engines must
agree on item ID, parent, fields, script counter, quest state, world revision,
transferred byte count and deletion count, as well as all PC/inventory/equipment
records and final poses. Both Linux and Windows/Wine host directions are run.

CTest's `object-state` covers canonical encoding and 30,000 mutated records.
`world-net` uses real sockets with deterministic engine-independent byte fixtures
to check bounded queue backpressure, delayed acknowledgment, ordered batches,
the settle barrier, wrong directions/offsets/sizes, CRC failure and timeout.
These complement the real-engine tests; they do not substitute for them.

## Native destruction and the owned-item regression

Protocol47 additionally shares callback identity/map/epoch capture across native
script dispatch, destruction, inventory sweeps, stack transfers, crafting and
device placement. Nested script replacement survives outer write-back. A
recursive DESTROY script executes once while native cleanup completes once.
Inventory sweeps visit original permanent identities once, recheck actual
ownership after callbacks and detach children from logically destroyed owners.
Physical disposal stops if callbacks leave inventory entries. Only unowned,
newly staged crafting/device objects use direct disposal on staging failure.
The current26 callback controls pass both host directions; commerce and native
tools continuations also pass. See `evidence/native-world-consumption-v47.json`
for scope. This does not establish arbitrary script transaction rollback or
all map-change/reparenting variants.

The intermittent deletion failure retained in `evidence/magic-regression-movement`
was reproduced in `reference-stress-1`. All three replicas identified the same
NPC's field 238, `OBJ_F_CRITTER_INVENTORY_LIST_IDX`. The fixture created a weapon
on the ground, waited two seconds, then called low-level `object_delete` directly.
Native NPC AI could acquire that weapon during the wait. Raw pool disposal left
the host's NPC inventory with a dangling permanent GUID; the host itself retained
that reference, and the replicas correctly rejected the resulting deletion.

The deterministic `movement-owned-delete` fixture transfers the temporary item
to an existing NPC before destroying it. It publishes owner/item identities and
requires every engine to verify that the NPC no longer references the item,
while the normal owned movement, inventory/story mutations, replica repair and
delayed final ACK still pass. The fixture now uses native `object_destroy`,
which runs destroy scripts and removes the item's inventory relationship.
Only after successful destruction does it explicitly dispose the pool entry
to exercise a wire deletion without waiting for sector unload. The ordinary
movement fixture uses this same cleanup. No network reference check is relaxed.

`tests/integration_references.py` runs both host directions with four actual
engines and whole-engine ASan/UBSan. It also runs the explicit
`movement-owned-delete-raw` negative control: deliberately bypassing native
destruction must produce the inventory-reference rejection in all three
replicas. `evidence/reference-owned-raw-delete` retains the initial deterministic
reproduction; `reference-owned-native-destroy` retains the first corrected run.
`reference-lifecycle-final` contains the reproducible negative/positive pairs.
The raw control is an explicit diagnostic mode, never an ordinary gameplay path.
Three additional ordinary movement repetitions in `reference-movement-final-*`
and the Linux/Windows/Wine/sanitizer `reference-campaign-final` object/ownership/
transition/save suite also pass. The eight positive lifecycle frames were
reviewed for the owned PCs and intact HUD/world; inventory cleanup is established
by the native state assertions, rather than a screenshot of an inventory panel.
All 45 final engine logs have clean access, UB and leak checks without
suppressions. This identifies and fixes that fixture failure; it does not prove
all campaign object and sector lifecycle paths.

Static positional records remain cached when their sector unloads; absence is
not treated as their deletion. GUID absence currently produces deletion. Sector
streaming, visibility/interest policy, off-map references, long sessions and
arbitrary campaign creation/deletion paths still need design and verification.
The tracker deliberately stops at unexpected map changes. The runtime's travel
barrier destroys it before native map loading and creates a fresh tracker after
the shared destination snapshot commits; see `TRAVEL.md`.

`STORY.md` covers the added global script/quest, shared discovery/rumor/journal,
generator/newspaper and clock state. This checkpoint does not replicate time
queues, combat scheduler, sounds, spell effects, animation goals, dialogue,
movies or every subsystem. Object fields alone cannot
reconstruct those systems. The separate adapter in `ITEMS.md` now provides owned
equipment/grid/drop/pickup/giving requests with applied-revision receipts.
Object batches alone do not establish complete interactive inventory commands,
trading/crafting, NPC behavior, combat, quests completed through real dialogue,
reconnection or campaign completion. Windows execution here uses Wine; testing on real Windows is out of scope.

Protocol 29 canonicalizes carried item location and offsets to zero during
export. Native inventory records can retain their last ground coordinates from
another map, including coordinates outside a smaller destination's bounds.
Parent and inventory references retain placement; world/ground positions remain
subject to the existing map bounds. Export does not mutate the host's native
record. A whole-engine property contrasts carried and ground exports while
checking that the original native coordinates remain unchanged.
