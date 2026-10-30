# Commerce and schematics

Protocol 20 routes NPC identification, repair, merchant purchases/sales, follower
inventory transfers and schematic combinations through the campaign host. Party
gold and ammunition gifts use the existing ordered item controller. Native
inventory and schematic windows provide local presentation; only the dialogue
initiator opens an NPC service window. Other owners keep the shared transcript
and see which service the initiator is using.

## Player controls

Hover an item to request a price, then click to accept the returned offer.
Merchant sales and purchases retain native Haggle, reaction, stolen-item,
buy-list and equipment restrictions. An ammunition stack opens the native
quantity picker on a left click; after choosing an amount, the host's total
price appears in the inventory panel. Click the item again to confirm. A right
click trades one unit. A changed item, price or conversation requires a fresh
offer. Insufficient buyer funds refuse the transaction.

The quantity picker accepts ordinary number keys as well as keypad digits;
Escape cancels it. Decimal entry clamps before integer overflow. NPC service
controls receive input while the underlying shared dialogue remains reserved.

An owned NPC follower uses the same window for free inventory transfers. Gold
and ammunition support quantity selection. Follower cycling is hidden during a
shared conversation; close it and speak to the desired follower. Another PC's
follower inventory is refused by the host.

Drag owned gold or ammunition onto another party PC to open the native quantity
picker and send that amount. Other items transfer whole. Both PCs must be active
and within eight tiles. Quantities, remaining stacks and receiving balances are
validated on the host. Canceling the picker changes no item flags or inventory.

Press `T` for the owned PC's schematic book. NPC schematic dialogue opens that
NPC's book for the initiator. Combine sends the recipe and crafter identity to
the host. Knowledge, component availability and technical expertise are checked
there. Components and products belong to the primary crafter, matching native
rules. Full inventories leave products on the ground, as in single player.

Closing an NPC window waits for an outstanding request's applied result before
canceling the shared conversation. The session's finish barrier also drains
service requests. Personal schematic books close during campaign teardown.

## Service wire format

Types 59 (`SERVICE_REQUEST`) and 60 (`SERVICE_RESULT`) use explicit big-endian
fields and permanent object IDs. No native pointers, item values, skill results
or product prototypes are accepted from a client.

| Request offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Epoch |
| 4 | 4 | Independent strictly increasing service sequence |
| 8 | 4 | Map |
| 12 | 16 | Owned PC GUID |
| 28 | 4 | Conversation |
| 32 | 4 | Conversation page revision |
| 36 | 1 | Quote (0), buy/transfer (1), combine (2) |
| 37 | 17 | Item ObjectID; crafter ObjectID for combine |
| 54 | 4 | Accepted quote token, only for buy/transfer |
| 58 | 4 | Quantity, 1–1,000,000 |
| 62 | 4 | Recipe, only for combine |

Quote/buy requests require nonzero conversation/page and zero recipe. A combine
requires quantity one and a positive signed-32-bit recipe; personal crafting
uses zero conversation/page, NPC crafting requires the current schematic
conversation/page. The host verifies the chooser, actor, NPC and native service
mode (barter 3, schematic 7, identification 8, repair 9).

The 24-byte result contains epoch, sequence, status, required applied world
revision, quote token and total price, all u32. Successful offers carry a
positive token; follower transfers have price zero. Other results carry zero
token/price. Statuses 0–13 are success, unavailable, ownership, conversation,
target, already identified, broken, undamaged, money, changed, limit, missing
components, insufficient expertise and unknown schematic.

Party item operation 12 (`GIVE_AMOUNT`) keeps the existing 59-byte request.
Its inventory-location field carries a positive signed-32-bit amount. Only
owned gold/ammunition or a single ordinary item may use it. Recipient membership,
distance, stack bounds, removal restrictions, receiving capacity and integer
overflow are checked before transfer.

## Authority and ordering

Each owner has one pending request and an exact last-request replay cache.
Requests stage outside socket callbacks. Execution is claimed before invoking
native item/skill/script functions; a quote token is retired before purchase
revalidation. Successful operations capture a fresh world revision. Feedback
waits until the recipient has installed it, so gold, components and products
are visible before completion is announced. Exact replay returns the original
receipt without performing a second action. Admission is bounded to 30 requests
per second and pending receipts time out after 30 seconds.

Prices and quantities are recalculated at purchase. Repair preserves native
training/effectiveness and durability behavior; identification uses native
spell pricing and effects. Barter checks permanent identities and ownership
again after quote scripts, and caches ammunition type before a transfer can
destroy its source stack. Partial ammunition capacity uses native weight
rounding and receiving-stack overflow checks. Native inventory callbacks now
check permanent identities after GET/TRANSFER/BUY_OBJECT/INSERT_ITEM/REMOVE_ITEM;
scripted consumption or world replacement cannot leave later UI/price code
using the old item or participants.

Crafting allocates every product before consuming components, cleans up failed
allocations, distinguishes two separate nonstacking components, and consumes
ammunition using its type. Native public identification, repair and schematic
entry points refuse execution on replicas.

## Acceptance

`runtime-services` is the combined four-engine commerce fixture. It exercises
all four owners, both host OS directions, native retail dialogue for Gypsy
identification, blacksmith repair/barter and Magnus schematic actions on a
controlled NPC, native inventory/quantity/book input, exact balance/component/
product changes, replay, foreign ownership and unavailable actions. Linux
ASan/UBSan participates with Windows binaries running under Wine. This does not
establish physical Windows or full-campaign acceptance.
`runtime-commerce` limits diagnosis to the commerce/crafting branch for one
owner; `runtime-followers` focuses on NPC crafting and free follower transfers.

The larger-batch cadence and remaining project scope are in `WORKFLOW.md` and
`OBJECTIVE.md`. The batch passes 152 stages per host direction: 36 executed host
service actions, eight party transfers, nine exact service replays and eight
refusals per owner; replicas execute none. Forty-five portable checks, the mixed
TCP probe and four selected inventory/loot/dialogue/save follow-ups pass.
Thirteen representative accepted frames and two diagnostic frames were reviewed;
40 accepted logs contain no sanitizer findings and 1,516 retail assets are
unchanged. `STATUS.md` links the reports.

The original Linux full run passed before a fixture-only cleanup correction.
The original Windows gameplay stages passed, but canonical capture after detach
read the replica's inactive native combat counter and was rejected. Reporting
now retains authority, as the battle harness does. The affected Windows full
direction was repeated and a nine-stage Linux control forces a nonzero counter.
No production feature code changed for that correction; local build manifests
pin both instrumented versions. A separate mistaken item assertion was corrected
by revalidating retained Linux outputs and running the missing Windows direction.

This is controlled service coverage, including Healing Salve and master repair.
Found recipes, alternate component combinations, allocation-failure cases,
lower-tier repair outcomes and original-location merchant/quest coverage still
need broader campaign acceptance.

## Payment completion and interrupted trades

Identification and repair now confirm their fee before changing identification,
damage or durability. They reject negative/unaffordable fees, revalidate the
item's owner after native payment callbacks, and return completion to the service
adapter. A refused payment no longer grants the service or reports success.
Native repair effectiveness and durability rules still apply after payment.

If a payment changes balances or replaces a participant before failing, the
existing Recover party failure path preserves the last durable checkpoint.
Barter also verifies a failed payment's rollback: the original item owner or
ammunition quantities and both balances must be restored. A consumed item or
refused rollback cannot silently leave a partial trade in a continuing campaign.

The focused native continuation adds eight negative-fee, insufficient-funds,
vetoed-payment and successful-service assertions to the existing callback checks.
Windows/Wine hosting with Linux and sanitizer replicas passes all 34 checks.
The Linux host also completed all 34 assertions, but its final report still
expected 26 and failed; that reporting constant is corrected without replaying
Linux merely for a green report. The final rollback confirmation is source-
reviewed, not a newly exercised rollback-failure scenario. Exact scope, logs and
build provenance: `evidence/service-payments-v50/acceptance.json`.

## Native panel recovery acceptance

The qualified protocol44 continuation accepts seven native panel types through
transport loss: identification, repair, merchant barter, follower crafting and
inventory, passive sheet and NPC map. All four engines retain normalized views
and unchanged journal counts; only the original chooser receives the native
panel. Ordinary service transactions and sheet/map closure follow recovery.
The current Windows/Wine-host full continuation also passes five native
callback destruction controls and exact service replays. Linux acceptance uses
retained prefixes; see `evidence/evil-checks/core-v44-native-panels-editor-devices.json`
for exact coverage and fixture qualifications. Original acquisition and wider
service branches remain open.

Gold/ammunition insertion callbacks now stop after source consumption/reparenting
and revalidate both inventories before native quantity updates. Barter rechecks
item/participant identities after its payment callback too. The seven native
controls, including recursive REMOVE_ITEM consumption, pass both host directions;
see `evidence/evil-checks/core-v44-ordinary-recovery-currency.json`. The earlier
five-hook acceptance remains retained above.
