# Shared campaign conversations

Retail dialogue is evaluated only on the host. The
initiating PC's owner chooses replies; every participant receives the same NPC
text, offered replies, selected reply and conversation outcome. Ordinary NPC
approach/talk clicks and scripted conversations use the same native adapter.

## Native quest conditions and transactions

Party-companion conditions (`pa`, including Schuyler's Magnus branches) recognize
active NPC followers of every cooperative human, regardless of which human is
speaking. Waiting and mind-controlled companions remain excluded. Follower
command conditions (`fo`) still require the actual owner. Level conditions
(`leN` / `le-N`) now compare the actual level against inclusive minimum/maximum
bounds; the previous negation hid Thaddeus and Boil guard options at higher levels.
The combined four-owner native ASan/UBSan check is recorded in
`evidence/delivery-2-dialogue/acceptance.json`.

Clicking an NPC now approaches a reachable tile within the native speaking
distance, instead of requiring adjacency. This matters for Pelonious's original
Schuyler ritual: his chair blocked the old automatic approach even though a
player who moved closer could speak normally. Talk retains its native distance
admission; item Use and Loot retain their separate interaction checks.

Negative money conditions (`$$-N`) mean at most N gold across the chooser and
their own followers. Previously they compared a nonnegative balance with a
negative number, hiding original insufficient-funds choices. The total uses a
64-bit accumulator. Payments first check that total and then transfer each
donor's actual contribution, stopping at the exact price. Other humans' money
is untouched. Failed transfers stop subsequent reward/quest actions and use
the existing Recover party failure path.

Dialogue local counters are four independent bytes. Reads and writes now mask
the selected byte; adjacent counters cannot change a condition or be overwritten.
Reaction bounds parse their numeric operand and retain native racial modifiers.

Item hand-ins and rewards preserve the original full-inventory fallback, but
revalidate the item after its native transfer/removal callbacks. An item consumed
or redirected by its script is not touched again. Action chains stop when their
participants or native world are replaced; entry text is not evaluated against
the obsolete participants.

The installed inventory has 568 negative-money conditions and 506 rows using
local-counter conditions/actions. A combined native diagnostic checks all four
owners, two funded followers each, exact/insufficient payments, rewards, reaction
modifiers, counter isolation and both ordinary and consuming item handovers.
Linux, ASan/UBSan and Windows/Wine pass. These are three independent controlled
engine scenes, not a four-process network session or ordinary campaign run.
Redirected-item and participant/map replacement guards are source-reviewed;
they were not separate runtime permutations in this pass. Retained reports,
frames, build/source hashes and reproduction instructions:
`evidence/dialogue-semantics-v50/acceptance.json`.

## Engine boundary and presentation

`dialog_ui_start_dialog` is also the script callback, so host script-initiated
conversations enter the same adapter as ordinary NPC conversations. The host
keeps the native `DialogState`, dialogue file, conditions, randomization, reaction
tracking, actions and continuation script. It resolves the initiating PC through
the persistent campaign registry to the current connection. Clients never load
a native dialogue state or evaluate its actions. Legacy networking stays disabled.

One shared conversation is active at a time. Protocol 31 retains up to 32
script-initiated starts in FIFO order using permanent PC/NPC identities, map,
script number, continuation line and starting dialogue number. Identical pending
heartbeat requests are coalesced. A reentrant native start is queued as well.
The next encounter starts only after the prior closure and every observer ACK;
its original PC owner chooses. A queued callback whose map or speaker no longer
exists is discarded as an obsolete native request. Queue exhaustion still fails
synchronization. Normal manual
talk admission reports busy while a conversation or queued start owns the panel.

Protocol45 also claims an opening callback for the duration of its entire
scripted conversation chain. Removing it from the FIFO cannot admit the same
heartbeat opening again between two NPC speakers. The key includes NPC, map,
script, line and page; another observing human does not create a second copy.
Explicit continuation lines retain their native meaning. Once all callbacks
and observer acknowledgments drain, later conversations can open normally.
The bounded opening list survives recovery in `ACDLGR04`; versions01–03 remain
readable. This corrects the original Crash Site stranger/Virgil exchange, whose
heartbeat otherwise kept inserting its opening between every scripted reply.

Save/rest/travel controls remain busy while dialogue is pending. A native script
party teleport requested behind queued conversations is retained until their
closures drain, then enters the normal shared map barrier. The dialogue queue
and the active native continuation are saved in `arcmp-dialogue.bin`. Recovery
restores the retained page or panel without rerunning its Talk/entry effects.
This also applies while rest, loading or travel has temporarily unbound the
dialogue controller: native starts enter the recovery queue instead of opening
a host-local panel. A conversation requested during rest stops the rest, then
opens for its original owner after the party has synchronized. Duplicate starts
remain coalesced, including across this handoff.
Floating and printed script lines use the same campaign boundary even when the
controller is detached. They retain their original recipient, speaker and text
in the version05 recovery sidecar, interrupt rest, and are delivered before
queued conversations. A notice stays retained until all observers acknowledge
it. Its saved journal index prevents duplicate history on reconnect; two
separate identical script emissions still produce two entries. The original
vicinity rules remain in effect, so distant ambient NPCs are not broadcast.
Each new conversation has a nonzero increasing ID. Every publication, including
closure and immediate reopening, receives a separate increasing page revision.
The native host entry is released normally when the conversation ends. Leaving
the adapter releases native dialogue, speech and presentation resources.

Protocol44 also retains the preceding selected response and native panel mode:
barter and companion inventory, passive character sheets, maps, crafting,
identification and repair. The original owner receives the restored native
panel; others receive its shared observer view. Service preparation, item
identification and stock/gold movement are not repeated on restoration. The
existing transcript page is reused only for this recovered publication; later
identical conversations still add normal history. The decoder accepts the two
earlier saved dialogue formats with defaults for their absent fields.

The cooperative panel uses retail fonts, the existing brown/gold palette and
the normal HUD. NPC text and options occupy wrapped, separate lines rather than
overlapping text bubbles. Observers see the same options and an explicit
observing label. Only the chooser's current page accepts mouse selections or
number keys 1–5. Wheel, Up/Down, Page Up/Down and Home/End scroll long pages;
Left/Right or the Previous/Next controls browse the retained campaign pages.
Old pages cannot submit choices. History and scroll position are local and do
not replay scripts or speech. F8 opens the saved conversation journal outside a conversation; Escape or F8
closes it. It retains NPC pages, offered/selected replies, closures and scripted
floating/printed hints across maps, native saves, voted loads and snapshot
restoration. Pages cannot execute choices in journal mode. Limits are 8,192 pages
and 8 MiB, dropping the oldest complete pages when either bound is reached.

The display adapter resolves permanent PC/NPC identities before presenting an
active page. If an object is still arriving through world replication, it waits
without acknowledging presentation. Replica presentation adds no animation
goals, script callbacks or NPC reactions. Speech uses the host-selected speech
ID and initiating PC. Native option evaluation remains independent of the host's
own local PC and camera.

## Delivery and authority

`dialogue.*` is an engine-independent service multiplexed by `gameplay.*`.
Messages are staged during socket polling; presentation and native choices run
later during the service poll. The service captures the roster revision and
fails if membership changes. The established connection supplies the sender;
neither a PC handle nor a claimed player slot is accepted in a choice request.

Publications are immutable and queued in order, with at most 32 pages of at most
8 KiB each. No page is coalesced away. Each client acknowledges after complete
CRC/schema validation and successful presentation. The host waits for every
participant's ACK before processing the next choice. A slow observer therefore
holds progression rather than losing a page. Delivery acknowledgment does not
mean that a human has finished reading; history permits revisiting prior text.

A choice carries the expected conversation ID, exact page revision, option and
monotonic request sequence. The host checks the current chooser, active native
mode and option bounds immediately before running the callback. Syntactically
valid observer, stale-page and invalid-option requests receive rejection without
executing an action. Replayed sequences, malformed frames, wrong senders and
bad stream ordering fail the connection. Requests are limited to 30 per second
per connection. Backpressure retains the pending page
and result. Thirty seconds without page-transfer/presentation progress fails
the dialogue service; the gameplay adapter reports resynchronization required.
The final gameplay barrier drains conversation pages as well as world batches
and refuses further native choices while finishing.

## Protocol 6

All integers are big-endian. The normal `ACMP` frame and 1,024-byte payload limit
are unchanged. Types 36–40 are reserved for conversations. Protocol 7 uses type 35 for talk
intentions and reserves choice option `0xffffffff` for chooser-only cancellation.

| Type | Direction | Payload |
| --- | --- | --- |
| 36 DIALOGUE_BEGIN | Host → client | Epoch, page revision, byte count, CRC-32 (four u32) |
| 37 DIALOGUE_CHUNK | Host → client | Epoch, page revision, exact offset (three u32), 1–1012 bytes |
| 38 DIALOGUE_ACK | Client → host | Epoch and presented page revision (two u32) |
| 39 DIALOGUE_CHOICE | Client → host | Epoch, request sequence, conversation ID, expected page revision, option index (five u32) |
| 40 DIALOGUE_RESULT | Host → client | Epoch, request sequence (two u32), accepted (u8) |

The host's own choice goes through the same request validation and deferred
callback. One pending request per connection prevents duplicate execution while
waiting for presentation or output queue space.

`ACD1` is an explicit portable view, at most 8,192 bytes:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | `ACD1` |
| 4 | 4 | Conversation ID, nonzero |
| 8 | 4 | Page revision, nonzero |
| 12 | 4 | Active, 0 or 1 |
| 16 | 4 | Native presentation mode, 0–10; choices require mode 0 |
| 20 | 4 | Chooser connection slot, 0–3 |
| 24 | 16 | Initiating PC's permanent GUID |
| 40 | 17 | NPC's portable GUID or positional ObjectID |
| 57 | 4 | Speech ID, i32 at least -1 |
| 61 | 4 | Option count, 0–5; zero for closed pages |
| 65 | 32 | NUL-terminated chooser name, remaining bytes zero |
| 97 | Variable | NPC reply, selected PC reply, then each offered option |

Each variable string has a u16 byte length and that many non-NUL bytes, at most
999 bytes. Tabs/newlines and retail extended text bytes are accepted; other C0
controls are rejected. Decode validates exact length, enums, identities, string
termination and padding before replacing its output. Native pointers, dialogue
action strings and condition expressions never cross the network.

## Verification and limits

CTest's `dialogue` suite exercises canonical bytes, a 10,000-record mutated
corpus, four real TCP sessions, maximum-size text, queue/backpressure behavior,
a delayed observer presentation ACK, exactly-once choice execution, observer and
stale/invalid choices, closure/reopening order, queue limits, timeouts and CRC
failure. The portable suites run natively, under Windows/Wine, and with ASan/UBSan.

`tests/integration_bootstrap.py --mode dialogue --framebuffer` loads the normal
crash-site campaign into four actual engines. It starts Virgil's retail dialogue
through the normal NPC script using a remote-owned PC, injects number-key input
through the actual TIG event/UI path on chooser and observers, and verifies one
host action. It then closes that conversation, starts another with the host's
PC, and verifies a second choice. Every process must record all five identical
portable pages, including the intervening closure, and matching final world and
story state. Both host OS directions run, with an optional instrumented Linux
host/client. Character-dependent options may differ between the two sessions;
all four participants within each session must agree exactly.

The crash-site fixture also exposed lazy static ObjectIDs: a spatial lookup
could find scenery before its native positional ID was materialized. Object
matching now resolves the public canonical identity before comparing it to the
host record. The four-engine crash-site replication exercises that boundary.

Dialogue construction receives the current monotonic time, so a first page
published before the first poll gets a fresh deadline after a long lobby or
character wizard. A portable regression starts at 120 seconds and verifies both
the initial page and a real 30-second stall. See `CHARACTERS.md` for the desktop
failure that exposed this case.

Protocol 31 adds mode 10 for host-selected floating/printed script lines. These
inactive, option-free events share ordered delivery and ACKs, but do not replace
the current conversation, its chooser or its expected page revision. `reply`
carries the hint and `selected` carries its speaker name. Presentation creates
local text/speech without executing NPC goals or script callbacks. A missing
already-destroyed speaker still leaves readable journal text. The native script
adapter uses the invocation's registered owner for gender/conditions and closes
loaded dialogue resources on absent-PC/attachee paths.

`ACJ1` is the transcript sidecar (`Save/Current/arcmp-conversations.bin`): magic,
page count and record-byte count (three u32), records, then CRC-32. Each record
has a u32 total record size, a nonempty NUL-terminated speaker of at most 127
bytes, and one canonical `ACD1` page. Integers are big-endian. Decode validates
all records before replacing the journal. Legacy saves without a sidecar start
with an empty transcript. Normal native save archives carry it to every owner.

The full dialogue/campaign contract remains open. Shared pause, services,
cutscenes and reconnect have separate adapters and controlled fixtures; see
`SERVICES.md`, `MEDIA.md` and `RECONNECT.md` for their exact limits. Additional
NPC-initiated branches are additional coverage opportunities. The active campaign
and platform requirements are maintained in [STATUS.md](STATUS.md#still-required);
an exhaustive retail conversation graph is not a separate release requirement.

## Character sheets and maps

Protocol 32 presents native mode 5 as the initiating player's passive NPC
character sheet; observers retain the shared dialogue status. Mode 6 opens the
chooser's native map with the same observer policy. Closing either panel queues
the authenticated cancellation until an outstanding dialogue result clears;
transport backpressure cannot strand a hidden conversation. Native failures to
open a panel retain the shared panel and its normal chooser cancellation.

Selecting a journey from a dialogue-opened map first closes the exact shared
conversation. Once its closure is acknowledged and queued encounters drain,
the ordinary unanimous travel proposal is submitted. A map/epoch change or
reconnect discards that local pending destination. Party gathering, combat and
route validity still use normal shared travel rules. These panels never run a
second native dialogue state or allow observers to execute choices.

A locally busy control submission retains the queued destination for a later
tick. Submitting any party operation also requires its control view to match the
current map/epoch, so input just after save cannot send the previous context.

## Companion inventory from the native portrait menu

Protocol 36 connects the companion menu's Inventory action to the authenticated
follower controller. Only that companion's owning human can open it. The host
checks the native inventory restrictions and reserves the NPC's normal reaction
context, then publishes a mode-3 service conversation. Everyone observes the
same owner/context; only the owner receives the native exchange window and can
request quotes or transfers. Existing follower transfers retain their zero
price and original item scripts.

This entry owns no quest dialogue file. Closing, reset and object destruction
balance the native reservation and skip file unloading for this entry. Ordinary
quest conversations retain their loaded files and normal teardown. Opening the
menu cannot replay the companion's recruitment/reward dialogue.

A pending arrival can wait for an already-open exchange. Closing that exchange
remains available even while new gameplay/service actions are blocked, so the
arrival and the panel cannot wait on each other indefinitely.

Recovery also copies ordinary FLOAT/PRINT publications still awaiting delivery
ACKs before retiring their transport epoch. It keeps the existing journal index
for a host-presented hint and retains later identical emissions as distinct
events. Newly emitted hints wait behind this recovered FIFO. Focused portable
checks exercise undisplayed, host-presented/unacknowledged and already-retained
publications; the full native mid-hint disconnect is not separately rerun.
