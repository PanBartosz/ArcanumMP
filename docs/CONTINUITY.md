# Story rewards, live journals and companion orders

Protocol 23 adds typed companion orders alongside the existing host-owned
campaign simulation. The same batch closes gaps in story rewards and local
journal presentation. This is a development checkpoint; `STATUS.md` records
the accepted fixture scope and remaining full-campaign work.

## Story rewards and personal consequences

Quest acceptance, completion, botching, rumors and discoveries already share
their native state. Each living PC receives the normal full quest and combat
XP award through its own difficulty/background/effect modifiers. Their existing
award paths remain separate, so sharing a story reward cannot multiply those
awards a second time.

Native dialogue `xp` and script `GIVE_QUEST_XP` now award their base amount once
to each living registered PC. Each actual native action is one award; an
intentional repeated script action is still another award. Quest completion
retains its existing terminal-state protection. No reward is executed on a
replica while applying host state.

Dialogue and script reputation grants/removals now describe the whole party's
story outcome. Relative dialogue alignment changes apply the same adjustment
to each PC's current value; absolute/minimum/maximum operations retain those
operations for each PC. Script alignment adjustments use the same policy.
The dialogue parser now reads the operand after `<`/`>` instead of interpreting
the comparison prefix as zero. Native follower attitude checks still run when
alignment changes.

Direct reputation APIs, race/background reactions, personal injuries,
inventory rewards, money, fate points and per-PC script variables retain their
native personal semantics. Crime scripts that use these shared story actions
also follow the shared policy. This defines story awards at the dialogue/script
call sites instead of globally broadcasting every stat or reputation mutation.
Wider crime, faction and follower-morale branches need campaign acceptance.

## Live journal

An open native journal reloads its selected tab's presentation data every
200 ms on both host and replicas. It redraws only when displayed IDs, states,
timestamps, fonts, intelligence-dependent text or kill statistics change.
Ordinary campaign-clock replication cannot repeatedly redraw the book.

Refresh retains the current tab and bookmarks the first displayed entry. Page
boundaries are recalculated without drawing intermediate pages, including
clamping after removal. The native open/close controls, fonts and sounds remain;
updates do not play page-turn sounds or close the book. New follower ownership
also updates native portraits even when the owner previously had no portrait.
Multi-page edits and every journal tab still need broader native acceptance.

## Companion controls

Right-click one of your native companion portraits. Walk and Attack enter the
normal targeting mode. Stay Close, Spread Out, Back Off and Wait submit host
orders. When a companion is waiting, the same menu entry becomes **Follow me**.
Inventory and the passive character sheet retain their existing native/service
paths. Dialogue recruitment assigns the follower to the initiating PC.
Waiting NPCs retain an owned portrait even though the engine removes them from
the active follower list. That presentation list does not count them toward
active follower capacity or carry them along on native party travel.

The host resolves permanent actor/follower/target identities, then checks:

- The connection owns the commanding PC and the NPC follows that PC.
- Both can act, remain on the active map and are within ten tiles.
- The combat generation still matches; turn-based orders require the NPC's turn.
- Walk destinations are within bounds, nearby, unblocked and pathable; native
  animations and AP rules govern movement.
- Attack targets are valid; other human PCs cannot be ordered as targets.
- Wait, resume and dismissal preserve native mind-control/following conditions.

The service accepts typed NPC identities rather than parsing displayed names.
Two companions with the same name cannot redirect an order. It stages work
outside socket callbacks, remembers exact request replays and waits for a
post-operation world capture before returning a receipt. The issuing replica
also waits until that revision is applied. Pauses suspend its action deadlines;
map/ending barriers drain outstanding receipts.

## Wire and native fixture

Messages 66/67 carry a 76-byte request and 16-byte result, using big-endian
integers. The request contains epoch, sequence, map, commanding PC GUID,
companion ID, optional target ID, optional tile X/Y, order, force flag and combat
generation. Generation zero is valid before the campaign's first combat change.
Non-targeted orders require zero target/coordinates/force. The result contains
epoch, sequence, status and applied world revision. Per-owner sequences, an
exact last-request cache, one pending request and a 30/s admission bound match
the existing action services.

`runtime-continuity` combines four native owners and four native followers,
quest journal updates while open, dialogue XP/alignment/reputation actions,
native script XP, terminal quest replay, companion formation/wait/resume/walk/
back-off, cross-owner and stale-generation refusals, exact replay and replica
reward guards. Its small synthetic `.scr` file exists only in each private
runtime during the fixture and exercises the real file interpreter. It does
not claim completion of the retail quests whose journal IDs it controls.

`runtime-companion-ui` starts at the companion stages and uses actual portrait
context-menu clicks for formation and Wait/Follow me. Attack/dismissal and all
original-location recruitment branches remain outside the initial fixture.

## Native companion Inventory (protocol 36)

Order 8 opens the original companion exchange from the Inventory context-menu
entry. Its wire shape remains 76 bytes, with empty target/destination and false
force; the codec rejects extra operands. Host checks authenticate the sending
human, the companion's current leader, map, distance and combat generation.
The native inventory restrictions still exclude animals, mechanical and summoned
companions. A mode-3 conversation shares observation and keeps the native
exchange window, quotes and item transfers with that owner. Closing, reset and
object destruction release its native reaction context without unloading a
quest dialogue file. It does not execute recruitment/reward dialogue to open
an inventory panel. Other players' requests cannot borrow this service lease.

## Native group and named commands (protocol41)

F1–F6 and the original broadcast entry now submit one ordered host command.
The request carries the sending human's cursor target/location, never the host's.
Native # commands address all nearby owned NPCs; a native named command addresses
that follower. Orders reuse the single-companion ownership, distance, target,
turn and refusal policy with original spoken responses. Human Join/Disband is
excluded so a broadcast cannot change persistent party ownership. Personal
Follow permits a nearby active critter on the requesting human's turn. Waiting
NPCs are included by retained ownership, since native Wait removes them from the
active follower array. Its native timer now includes the NPC object reference;
the previous uninitialized parameter crashed while scheduling charismatic-owner
Wait. `evidence/native-tools-picker-recovery-v44` passes group Wait, actual F4
Spread Out and Follow in both host directions. Wider gestures, refusal paths and
saved waiting timers remain open.

## Permanent slots after lobby departures (protocol 50)

A campaign may start with slots 0 and 3, or 0/2/3, after earlier guests leave.
`coop_campaign_count()` is the population, not the highest occupied slot plus
one. `coop_campaign_next_slot()` visits occupied permanent slots without changing
identities or skipping later owners. Shared story consequences, keys, healer AI,
rest, sector loading, native action settling, combat mode and time-spell checks
use that rule. Runtime landing plans compare only occupied positions; travel,
checkpoint headers/catalogs, autosave, reconnect and defeat checks retain the
same roster. Checkpoint loading also compares empty slots with its header.

`--coop-engine-probe=sparse-party --coop-probe-members=9` exercises actual local
TCP lobby departures followed by native gameplay/save/load for slots 0/3.
Masks 13 and 11 cover 0/2/3 and 0/1/3. These are single-engine gameplay checks,
not multi-engine network acceptance. The full evidence and unexercised runtime
branches are listed in `evidence/sparse-party-v50/acceptance.json`.
