# Cooperative skills and tools

The controlled four-engine fixture passes in both host directions, including a
sanitized Linux participant. All 104 composed skill frames and 72 item-use
regression frames were visually reviewed. This does not establish every
skill/service/item branch or a complete campaign.

The native skill controls retain local target selection. Sneaking, repair and
object-targeted disarming submit owned intentions to the host. Stealing opens
an authorized inventory view and routes each attempted transfer through the
native pickpocket roll. Healing and lockpicking use their owned native tools
through the item-use service. Replicas cannot execute native skill invocations.

Human characters use their own skills. The host does not substitute another
human or interpret its keyboard modifiers as another player's assistance
choice. Sneaking retains normal NPC follower behavior while excluding other
registered human characters from that toggle.

## Native execution

The host validates the persisted owner, current map, actor availability and
turn generation. Repair requires an inventory item belonging to the actor or
visible through that connection's current inventory access. Disarming retains
the native object-target metadata and spends its native four AP. Repair and
sneaking retain their native immediate paths without an added AP charge.
Location-only clicks in the skill selector retain the native no-action path.

Native success, failure, critical results, training, fate, item charges, lock
state, trap scripts, healing and repair durability remain authoritative. A
legal attempt can fail its skill roll: its ordered action receipt and its
personal native success/failure feedback describe different parts of the same
action. Failed or critical attempts can still commit their normal costs and
script effects.

The repair, lock, disarm, stealing and planting callbacks now recognize all
registered human actors. Cooperative healing also delivers the native skill
success callback. Feedback goes to the acting character's owner rather than
the host's local panel. The original message catalogue and native UI render it.
Cooperative results enter persistent native message history so a redraw or an
open inventory does not hide them. Replicas advance the local text-floater
presentation timer without executing gameplay/time events. The floater render
flag is derived from the local list when applying authoritative object state.

## Stealing access

The item service distinguishes loot and stealing views. A stealing target must
be a living nonplayer character accepted by native targeting, without the
mechanical, no-pickpocket or petrified exclusions. Opening checks native sight;
the view has a bounded distance lease. Other human characters are excluded.

Taking or planting uses the native approach and pickpocket animation. The host
waits for the actual native attempt before reporting its result. Native gold
and ammunition limits remain 100 coins and 20 rounds per attempt. Detection,
alignment changes, caught-thief scripts and possible retaliation stay native.
Stealing does not grant bulk transfer or inventory rearrangement permission.

Each admitted attempt has a local completion ticket. Its token, exact animation
ID, actor, target, item, ownership, access generation, map and combat generation
must still match before the native roll and AP cost. The host claims the callback
before entering native code. Retired or reentrant callbacks cannot roll twice.
Interruption or a fifteen-second deadline cancels the matching goal; a failed
native cancellation stops the session. Completed results survive exact request
replay without another roll, cost, transfer or feedback event.

An admitted attempt may finish during the final drain. It retains its inventory
access until completion, then closes it and drains its resulting world state
and personal feedback before shutdown. Completion tickets are not persisted;
durable saves with pending actions still need a drain or explicit recovery.

## Protocol 18

Type 53 is a 51-byte skill request: epoch, sequence and map at offsets 0/4/8;
actor GUID at 12; action and value at 28/29; portable target ObjectID at 30;
combat generation at 47. Actions are conceal 0, repair 1 and disarm 2. Conceal
requires a null target and desired value 0 or 1. Other actions require an
instance target and zero value. Prototype targets, invalid identities,
generation UINT32_MAX, unknown actions and noncanonical shapes are invalid.
Stealing uses item operation 11, not an arbitrary native skill number.

Type 54 is a sixteen-byte epoch/sequence/status/required-world-revision receipt.
The per-player sequence, exact-replay cache, thirty-request-per-second limit
and thirty-second deadline follow the other ordered action services. Receipts
wait for a fresh capture; the recipient waits for its applied revision.
Statuses are accepted native action 0, unavailable 1, ownership 2, invalid target
3, distance 4, another turn 5, stale generation 6, busy 7 and refusal 8. A native
skill-roll failure still has status 0 and separate native failure feedback.

Type 55 is a 36-byte personal feedback event: epoch, personal sequence, map and
required world revision at 0/4/8/12; actor GUID at 16; native skill-message ID
as a network-order u16 at 32; red-floater flag at 34; catalogue at 35 (0 for
skill UI, 1 for trap discovery).
The catalogue is restricted to 500/501, 505, 512, 525, 550/551 and 560/561, with
their expected colors. Catalogue 1 permits only native trap message 0 with no
red floater. Trap discovery stays authoritative and its spotted flag/marker is
shared by the party, with the notification sent to its finder. Type 56
acknowledges epoch and personal sequence in eight bytes. Each owner has a sixteen-event FIFO and a thirty-second deadline.
Presentation waits for the applied world revision. Exact event replay is
acknowledged without repeating native history or floaters. Wrong recipients,
changed or out-of-order events, invalid colours and catalogues are rejected.

Type 44 inventory-access events grow from 29 to 30 bytes. Byte 29 is mode 0 for
loot or 1 for stealing; a closed target requires mode 0. Mode participates in
exact replay validation. Protocol 18 rejects older peers during the handshake.

## Controlled acceptance

The 21-stage fixture passes in `evidence/skills-final`, after 24 native
combat stages. It covers independent sneaking, inventory mouse selection for
repair, exact replay, ownership/stale refusals, native trap spotting/disarming,
healing and lockpicking tools, stealing/planting and native worn-armor failure,
access revocation, interruption, personal feedback and an admitted attempt
during finish. Each host records 20 accepted skill controls, eight refusals,
32 native invocations during the stages and one during the finish drain. Native
passive spotting checks are counted separately. Replicas invoke none. Four
reentrant and four retired theft callbacks produce no second roll. Personal
feedback replay does not repeat history or floaters. The nearby final attempt
advances the native campaign clock and transfers its 100 coins before shutdown.
Every player also verifies local floater expiration after four seconds, including
clearing its render flag. Earlier captures exposed transient result text and
stale replica floaters; those rejected previews remain separately documented in
`evidence/skills-checks/diagnostics.json`. The accepted visual review is recorded
in `visual-review-skills.json` and `visual-review-use.json` in that directory.

The item-use regression in `evidence/skills-use-final` repeats the previous
21-stage script-order/completion fixture with this build. Thirteen portable
tests pass on Linux, Windows/Wine and ASan/UBSan (39 passes), including 20,000
mutations of both skill request and feedback schemas. Protocol 18 transport and
malformed-frame checks pass in `evidence/network-v18-final.json` and
`network-v18-sanitized-final.json`.

All eighteen broader regression scenarios pass, covering owned references,
movement/dialogue and host loss, combat loss/replay/weapons, spells, inventory,
loot/loss, travel/loading faults/stale commands, character launch, native
campaign objects/pools/story and ordinary save transfer, and native combat/enemy
behavior. The log audit checks 155 accepted engine logs with no sanitizer
findings, including eight logs from two deliberate raw-reference rejection
cases. Fifteen standalone native AP/enemy captures were also reviewed, bringing
this checkpoint's accepted review scope to 191 frames. Those standalone combat
captures retain the preceding native heartbeat speech overlap; they do not
establish dialogue layout acceptance. All 1,516 original installation assets
remain byte-identical.

## Remaining campaign acceptance

Broader AP/turn parity, NPC assistance, all trapped target and tool variants,
wide approach/path geometry, equipped/stolen-item restrictions, caught-thief
quest branches, prolonged
faults, save/reconnect recovery and desktop acceptance remain
open. Passive skill systems and NPC services need their own campaign acceptance.
`TRAINING.md` covers controlled native dialogue apprenticeship and
`PROGRESSION.md` covers live allocation; broader trainer/progression paths remain
unverified.

## Owned-companion assistance (protocol39)

Follower Skills is now a per-human saved option. Native trap disarming and lockpick
item use may select a better nearby owned NPC companion, using original skill
comparison and effects. Assistance excludes other humans, foreign/inactive/busy or
distant followers, and substitution during turn-based combat. Ctrl explicitly uses
the requesting human's skill and travels in the authenticated intention instead of
reading the host keyboard. Companion skill feedback reaches that companion's owner.
Save/travel drains include companion item/skill animation goals. Repair retains its
original direct-human selection. Runtime acceptance remains pending.

## Owner-directed native notices (protocol40)

The existing36-byte personal-feedback record also carries native spell warnings
(catalog2), item errors(3), blessings(4) and curses(5). The recipient waits for its
applied world revision and acknowledges the event once. Spell warnings include
failed energy/concentration, maintenance termination, nullification, anti-teleport
and impossible resurrection. A companion's warning goes to its human owner.
Blessing/curse notices preserve native descriptions, sounds and logbook highlights.
Only native message IDs cross the wire; arbitrary paths/text and effect mutations
do not. The bounded queue now holds64 events per owner. Runtime acceptance is open.

Native Detect Traps admission also uses the actual acting character's spell
flags. A remote human neither borrows nor loses detection because of the host's
local buffs. This source correction awaits runtime acceptance.

## Native action parity and placed devices (protocol41)

Basic/technical skill effectiveness applies the shared difficulty adjustment to
every campaign human, including native replica previews. Remote humans no longer
receive NPC-only animation completion AP charges. New ground/door/container traps
persist the placing human's campaign member, map and permanent object identity;
mechanical/magical damage credit uses that identity through save/rejoin. Delayed
explosives save their member in a dedicated native timer and use the existing
magic attacker reference, preserving native effect/source rules. Failed placement
retains the item; native placement errors reach that owner through feedback
catalog6. Legacy traps without owner data retain their historical attribution.
`evidence/native-tools-picker-recovery-v44` passes owned spike-trap and dynamite
placement, followed by paused Save and unanimous Load retaining the trap's placer
and the bomb's typed pending timer, in both host directions. Native trap use now
propagates a placement refusal instead of returning success unconditionally.
Actual saved-device damage now also passes the qualified native continuation:
a member3 spike trap deals24 damage and a member0 dynamite timer deals188/186
on Linux/Windows-Wine hosts, retaining their original placer's damage credit.
Final Save/Load retains note state and target cleanup. Evidence is
`evidence/evil-checks/core-v44-native-panels-editor-devices.json`. Controlled
high-HP targets exclude organic acquisition and combat balance; wider device
branches remain open.
