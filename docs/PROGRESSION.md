# Cooperative character progression

The controlled four-engine fixture and all eighteen broader regressions pass.
All 112 composed progression frames were visually reviewed. Full campaign
acceptance remains unfinished; this is not a campaign release.

The native character sheet retains its ordinary layout. During a cooperative
campaign, its allocation controls submit owned intentions to the host instead
of changing local fields. Attributes, HP/fatigue points, basic/technological
skills, college spells and technological disciplines use native engine rules.
Auto-level scheme selection also runs on the host, restricted to schemes with
matching native names and rules; manual allocation is scheme zero.
Other human sheets and follower inspection are passive. Lobby creation remains
local until its existing bounded creation recipe is accepted by the host.

Opening an active sheet requests a host edit lease. The host records the current
allocation baseline and returns a monotonically increasing lease ID, scoped to
that owner and gameplay epoch. Refunds cannot cross that baseline. Closing the
sheet accepts its current allocations, matching native behavior; it does not
roll them back. Reopening records a new accepted baseline. Closing during a
pending change waits for its outcome before releasing the lease. An immediate
reopen waits for that release before recording its new baseline.

Each edit identifies its lease, selected ability, direction, observed base value
and available point balance. The host validates the persisted owner, current
map, actor availability, lease and those observed values. Changed state requires
a fresh attempt. The client does not submit XP, levels, identity, effects,
training, arbitrary values or a replacement character object. Costs,
effective-stat/level prerequisites, bounds and refunds are host operations.
Basic-skill refunds retain the native rule of refunding internal points until
the displayed skill changes, with the host baseline as their lower bound.
Point spending does not heal existing damage.

The service stages network requests outside callbacks, supports one pending
request per owner, caches exact replay, and requires fresh capture and applied
world revision before an outcome is presented. New requests are bounded to
thirty per second and unresolved actions have a thirty-second deadline. Travel
and finish wait for pending outcomes; the gameplay epoch discards edit leases
when replacing a map. Leases are permission state, not durable save state.

Replicas derive a local native level-up notification after their owned PC's
authoritative level increases in an applied world batch. This does not award XP
or execute native leveling on a replica. Reapplying the same level does not
repeat the notice. The host retains its native local notification.

## Protocol 19

Type 57 is a 44-byte request. All integer fields are network order:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 / 4 / 8 | 4 each | Epoch / sequence / map |
| 12 | 16 | Owned actor GUID |
| 28 | 1 | Open 0, close 1, edit 2 |
| 29 | 1 | Attribute 0, HP 1, fatigue 2, skill 3, college 4, discipline 5, auto-level scheme 6 |
| 30 | 1 | Selected index |
| 31 | 1 | Increase 1, refund 2 |
| 32 | 4 | Lease |
| 36 / 40 | 4 each | Expected current value / available points |

Open requires a zero lease and all selection/value fields zero. Close requires
a nonzero lease and otherwise zero selection/value fields. Edit requires a
nonzero lease, a valid selection/direction and nonnegative signed-32-bit values.
Native stat indices 0–7 select attributes; skill indices 0–11 are basic and
12–15 technological; colleges have 16 indices and disciplines eight. HP/fatigue
require index zero. For scheme selection, index is the desired scheme 0–199,
direction must be 1, and expected value is the current scheme. Selection spends
no points. Unknown and noncanonical shapes are rejected before native code.

Type 58 is a 20-byte receipt: epoch, sequence, status, required world revision,
and current lease as five u32s. Status is accepted 0, unavailable 1, ownership 2,
lease 3, changed state 4, insufficient points 5, maximum 6, native requirements
7, or accepted baseline 8. Protocol 19 rejects older peers.

## Acceptance work

Fourteen portable tests pass on Linux, Windows/Wine and ASan/UBSan (42 passes),
including 20,000 mutated progression requests. All three game builds compile.
Protocol 19 mixed-process and malformed-frame checks pass in
`evidence/network-v19-final.json` and `network-v19-sanitized-final.json`.

`evidence/progression-final` passes 41 progression stages after 24 combat stages
with each host platform, including a whole-engine sanitized Linux participant.
Each host executes 136 accepted controls and 40 refusals; replicas execute none.
Every owner checks two exact replays and ten refusals, existing damage survives
point allocation, and each replica presents its single owned level increase.
The fixture covers all six allocation types, native XP granting one earned
point per PC, skill/spell/discipline refunds, ownership/stale-state/lease/cost/
maximum/baseline refusals, learning the first two college spells and refusing
the level-five third spell at level two, scheme selection/refusal/manual reset,
closing during an edit and immediate reopening with a newly accepted baseline.
Preparation deliberately controls stats and point balances for specific cases;
this is not an organic campaign progression or balance test.

The initial run exposed fixture coordinates missing the panel's 41-pixel offset.
Later vision checks rejected missed circular tab clicks and college selection
queued before its panel was raised. Inputs now use button centers and separate
ticks. Successful edits also replace prior refusal text with persistent native
feedback. Earlier previews and deliberately interrupted next-regression runs
remain documented in `evidence/progression-checks/diagnostics.json`; accepted
review is `visual-review-progression.json` in that directory.

The same builds pass the 21-stage skill and item-use fixtures in both host
directions. The eighteen broader regressions cover reference rejection,
ordinary controls, host loss, combat replay/weapon/spell/item/loot behavior,
travel and travel faults, character launch, original enemies, normal campaign
saves in both OS directions, and standalone native combat/encounters. All 163
accepted engine logs are free of sanitizer findings, including eight expected
raw-reference rejection logs. The review covers 199 composed frames: 112
progression, 72 item-use and fifteen standalone native frames. Native standalone
combat retains its preceding heartbeat speech overlap. Standalone encounter
information depends on uncontrolled hover/history, so those frames do not prove
owned-HUD parity; the four-engine captures exercise each owned HUD.

Records are under `evidence/progression-checks`: `commands.json`,
`regressions.json`, `accepted-log-audit.json` and `visual-review-*.json`.
`evidence/retail-assets-progression-final.json` confirms all 1,516 original
installation assets remain byte-identical after these runs. Frame captures use
the native TIG framebuffer with SDL dummy/software; Windows runs under Wine.

Still required: additional native prerequisite and effect/cap variants,
dedicated editor save/transition/fault checks, prolonged
editor use, desktop input, and integration with training,
services, crafting and full campaign progression. Native dialogue apprenticeship
has separate controlled acceptance in `TRAINING.md`. This is not a complete
campaign multiplayer release.

## Authoritative fate selection

Protocol38 adds `COOP_PROGRESS_FATE` to the ordered progression service. The
native Fate menu submits the owned actor GUID, fate index, activate/deactivate
direction, expected pending-fate mask and expected point total. It uses no
character-editor lease. The host validates availability, ownership, alive state,
map and exact expected state before native activation or cancellation. The full
heal fate retains the original HP/fatigue/poison effects and spends one point.
A still-pending fate may be cancelled for its normal refund; a fate already
resolved by another action fails its expected-state check and cannot refund.

The menu reflects authoritative replicated flags instead of changing replica
stats. During transfer/rejoin the native Fate window closes. Original native
fate mutations and resolution reject replica authority and invalid indices;
single-player behavior retains local authority. The implementation allows an
owned living unconscious character to request its native healing fate.
The current source is implemented; native multiplayer acceptance is deferred
until all planned core functionality is in place.
