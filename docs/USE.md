# Native item use and personal reading

Protocol 17 retains operation 2 in the 86-byte ordered magic request. Item use and
spell casting share a per-player outstanding request and exact-replay cache.
The item request supplies an instance ObjectID and an object, tile or unspecified
target; it cannot choose a native script, spell, charge count, skill or animation goal. Spell and
token fields must be zero. An unchanged replay returns the original result
without repeating scripts, AP costs, item destruction or schematic learning.

The host checks persisted character ownership, map, turn generation, current
turn, item parent and usable flags. Another action prevents starting a use.
Native intelligence and AP checks apply before dispatch. For object use,
SAP_USE runs before default-effect validation. A script can suppress the default
or change its metadata. A consumed/transferred item or replaced map/turn stops the old default. The
remaining allowed default then checks the current item parent,
effect, native resources, target and range. A refusal can follow committed script
side effects; this preserves native order and does not roll back the script. Registered human characters retain
control of their own tools; automatic selection of another human as the best
lockpicker is bypassed. Native NPC tool assistance remains unverified.

Ordinary object use retains SAP_USE, including scripted potions. Explicit tile
use retains the native tile path, which does not dispatch SAP_USE. Trap devices,
nearby-use items and tools retain native approach and skill goals. Throw flags
select the native throw goal. These paths are wired, but only the branches in
the fixture below have acceptance evidence. Plain item approaches use a local
completion ticket: their receipt waits for the native use callback or a refusal.
Interruption, ownership loss, map/turn changes and a 15-second deadline cancel
the matching native goal. Skill subgoals and throws still acknowledge native
intent rather than every later outcome. The finish/travel drain lets registered PCs' accepted native use/skill/throw goals
end on the host before it freezes simulation, seals audio and settles the
world capture. New intentions are refused during that drain. Spell impacts and
all maintained-effect travel behavior still need broader acceptance.

Item spells execute directly from the owned item on the host. They do not
reenter the host's local spell UI or submit another player's request. Native
triggered enchantments run through their ordinary item-used notification once.
A refused direct invocation retains its consumable. Native item mana and
consumption rules remain responsible for charges and destruction.
The native cost-query path now leaves charged-item flags and mana unchanged;
only an actual charge commits those writes. Live native spell allocation checks
capacity and refuses a full pool; item invocation also checks capacity before
consumption. Cached map spell loading now stages and checks every record and available native
ID before installation. Pool pressure refuses restoration and retains its input
instead of terminating the process. A native map failure freezes cooperative
play for durable checkpoint recovery; this is not a transaction over the whole
engine. See MAGIC.md and STATUS.md for the current acceptance scope.

The inventory Use box, usable-item right-click and ordinary item hotkeys start
this path. Target selection and cancellation run locally. The self/radius
activation path calls the item handler; the previous native port called the
spell handler with unrelated UI state. Replicas cannot run either low-level
native item-use function or schematic learning.

For the exercised controls, drag an owned usable item onto the inventory's
hand-shaped Use box, then select its target. Right-clicking an unworn usable
item also starts use. Ordinary item hotkeys retain native targeting. An ordinary
use without a target keeps the native null target; Shift explicitly selects the
owner where the item permits it. Canceling target selection sends no use request
and retains the item. Item-use refusals enter native message history so a full
redraw does not replace them with an older notification. Protocol47 also retains
spell refusals in that history and identifies blocked native line of sight
separately from other invocation refusals.
Reading opens a personal window while campaign time continues; learning a new
schematic consumes it, and an already-known schematic remains in inventory.

## Native completion identity

Each pending plain-use goal carries a process-local positive token in its unused
skill-data field. The host matches that token, the exact animation ID, actor,
item, target, map and turn generation before invoking native use once. A retired
token cannot borrow a replacement goal, even when its other native fields match.
The host claims dispatch before entering native scripts, so a reentrant callback
cannot run the use twice. Completed tickets remain tombstones until replacement
or teardown. These tokens are never supplied by a peer. Existing goal/save
structure sizes are unchanged.

The ticket registry is not persisted. Durable cooperative saves while actions
are pending need a drain or explicit recovery design; loading an unmatched
positive token suppresses dispatch rather than repeating an unknown action.
Native cancellation failure fails the session and stops simulation.

## Personal views

The host runs book scripts and schematic learning. It queues a personal view
for the character's owning connection, leaving other players' windows alone.
The native book/note/newspaper/telegram/image/plaque UI renders locally. Native
schematic success/already-known feedback also goes to its owner. Reading does
not create a second simulation on a replica.

Type 51 has exactly 40 bytes: epoch at 0, personal sequence at 4, map at 8,
required world revision at 12, reader GUID at 16, native written type at 32,
variant at 33, two zero reserved bytes at 34, and nonnegative native message or
schematic number at 36. Types are 0–6. Variant 1 means Vendigroth newspaper for
type 2 and already-known schematic for type 5; it is otherwise forbidden.
Numbers above INT32_MAX minus 10,000 are rejected. Epoch, sequence, revision and
reader identity must be valid. Native pointers, paths and message text do not
cross the wire.

Each recipient has a bounded 16-event queue. Only its head is sent, after a
fresh world capture. A client stages the event, waits until the required world
revision is installed, presents it once and sends type 52 (epoch/sequence).
An exact applied replay is acknowledged without reopening or toggling a closed
book. Changed replays, wrong recipients, reserved fields and out-of-order
sequences are invalid. A 30-second deadline or queue overflow fails the session
instead of silently losing a personal view. Shutdown drains these events and
closes a remaining reading window before map/session teardown.

## Scope

The runtime-use fixture builds on the 24 combat stages and exercises retail
healing/fatigue potion scripts, a Strength of Earth scroll, native healing salve,
stale turns, foreign item ownership, exact replay, independent reading and
schematic learning. Additional checks cover the inventory Use gesture, a native
hotkey, target cancellation, depleted native charges, reading-event replay and
campaign time while a reading window is open. Additional stages exercise a
retail potion script that suppresses deliberately invalid default metadata,
null-target reading, interruption of four concurrent approaches, retired-token
callbacks, reentrant completion during a live use, full-pool invocation refusal and a combat-policy change while four
item approaches are pending. Script-boundary counters verify the acting PC and
exact dispatch counts; replica script counts must remain zero.

The final 21-stage four-engine run is in `evidence/use-parity-final`, in both
host directions with a sanitized Linux participant. Each host records 48
accepted uses, 24 refusals and 57 exact script-boundary dispatches; replicas
record none. All 72 composed frames are reviewed in
`evidence/use-parity-checks/visual-review-use.json`.

The eighteen regression scenarios in `evidence/use-parity-checks/regressions.json`
also pass, covering references, runtime controls, combat, spells/audio, inventory,
loot, travel faults, characters and native campaign/save behavior. The accepted
147 engine logs have no sanitizer findings; fifteen additional native combat
frames were reviewed. All 1,516 original installation assets remain unchanged.

The shutdown fixture queues a real native potion-use goal and immediately
requests finish; the final replicated state must show its healing and
consumption on all four engines.

The newspaper fixture uses retail Vendigroth article 1280. The bare newspaper
prototype points at decoration message 0 (`EXTRA!`), which is not an article
with a numeric font prefix. The initial capture with that default produced
arrow-like glyphs and missing columns; the retail archive inspection is
retained in `evidence/use-checks/newspaper-content-diagnosis.json`.

This does not establish every scripted quest item, tool skill, trap, thrown
device, charged equipment hotkey, recharge, teleport follow-up, service,
crafting or barter flow. Those require further fixtures and campaign runs.
Script order and null targets now retain native semantics in the controlled
fixtures; every quest-specific script and map-changing item still needs coverage.
Party reading history, opt-in rereading and reading-view fault recovery also
remain open; a personal window alone does not establish complete story sharing.
The four-process captures use SDL dummy/software rendering and Wine; physical
Windows and desktop interaction remain open. The full campaign objective is
unfinished.
