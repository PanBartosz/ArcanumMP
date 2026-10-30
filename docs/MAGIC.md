# Cooperative native spellcasting

## Effect and reference corrections — 2026-10-26

The persistent fire callback had its real-time branch in the wrong place:
enemies received no damage while the caster and their followers did. Retail
`Arcanum.exe` at `0x42A884–0x42A8D9` confirms immediate enemy damage in real time,
double damage once per turn in turn-based combat, and exemption for the caster
and their followers. The corrected callback restores those branches and extends
that exemption to the cooperative party. Unowned environmental fire still
damages independent critters. This is specific to persistent fire callbacks;
other area-spell friendly-fire rules are unchanged.

Animation goals now update persistent identities whenever runtime door,
collision, throw, projectile or parent references change. Summoned effects
receive their caster across the whole goal stack. Projectile creation and
redirection retain their new identities for serialization and handle recovery.
Native safe-reference saving refreshes a live object's identity/location while
preserving stored identities for unloaded objects. Recycled magic object nodes
clear their old identities, and null validation no longer dereferences null.

One combined ASan/UBSan native check covers four casters, four protected humans
plus a follower, real-time and once-per-turn enemy damage, environmental fire,
retargeted references and full campaign snapshot restoration of four redirected
projectiles with two goal levels each. All three targets build. The composed
post-restore frame was inspected. This is controlled native evidence, not a new
multi-engine or complete spell-casting/UI acceptance run. Exact provenance:
`evidence/effect-references-v50/acceptance.json`.

## Original implementation baseline

Protocol 14 routes casting and maintained-spell cancellation through the host.
The original spellbook, hotkeys, targeting cursor and maintenance bar remain the
input/display surface. The host runs native goals, fatigue/AP rules, resistance,
effects, damage, healing, summons and interruption. Replicas install the resulting
objects and presentation state; they do not invoke a second spell simulation.

## Requests and native validation

`magic_state.c` defines the 86-byte portable request. It carries the shared
epoch, ordered sequence, map, owned actor GUID, cast/cancel operation, spell,
explicit force/location flags, source/target ObjectIDs or tile coordinates,
cancellation token and expected combat generation. It never carries native
pointers, free-cast/unresistable flags, effect values or arbitrary item triggers.

`magic.c` stages input outside socket callbacks, permits one outstanding request
per controller, checks exact replays, rejects conflicting replays, applies the
existing 30 requests/second bound and waits for a fresh applied world revision
before exposing a receipt. Accepted intent does not promise a completed impact.
Finishing/travel drains this service with combat, items, dialogue and world state.

`engine_magic.c` validates the owned active PC, generation and turn. A PC must
know the requested spell; an item source must belong to that PC and actually
offer the explicitly activatable spell. Prototype IDs and invalid placements
cannot select a source or target. Inventory targets are restricted to the acting
PC's inventory. Native targeting constraints, range, LOS, casting requirements,
energy and available native run slots still govern submission. Native validation
and resistance rolls execute once. Protocol47 reports blocked native line of
sight as status12, before resource consumption; the native invocation still
enforces its own LOS rule. Spell and item refusals enter the owning player's
message history and survive full redraws. An unforced aggressive object-targeted spell
cannot select another party character; native area-spell friendly fire remains
native behavior.

Native `TGT_NON_PARTY_CRITTERS` checks now also recognize the cooperative party,
including independent PCs. This affects the cursor and native targeting queries,
as well as the explicit host-side friendly-target refusal.

## Maintained spells and derived effects

Native effect/cause arrays and spell flags already belong to the object schema.
Their authoritative updates carry buff/debuff stats and flags without replaying
effect callbacks on clients. The `ACS3` subsystem record now appends an `ACM1`
maintained-spell view with up to 512 entries, bounded by the native run pool. Each
entry contains a token, caster GUID, spell and optional target ObjectID. The full
subsystem envelope is bounded to 64 KiB. All references resolve before world
installation; the view does not become a client-side native run table.

Native IDs are small reusable slot numbers. A separate process-local 64-bit
allocation token changes on allocation and native save/map restoration. It does
not change native serialization. Cancellation requires both the token and its
caster's ownership, so an old action cannot cancel a later run that reused the
same slot. Epoch validation also applies across shared map/snapshot transitions.

Each controller sees its own maintained spells in the original bar, with stable
positions while other entries end. Hover uses the authoritative spell/target;
click sends cancellation and waits for the host. Native five-slot capacity is
applied consistently to all human controllers. The original UI's missing
full-bar check and unchecked slot bounds have been corrected. No spell icon,
fatigue or effect is removed optimistically.

An item-spell weapon equip now finishes before the request is resolved again.
The native WIELD callbacks may consume/transfer equipment or change the map,
turn, target or resources. The second validation checks that resulting state
before submitting or charging the spell; a stale equip/target cannot be reused.
The successful equip postcondition bounds this to one additional pass.

Native invocation, component processing and interruption entry points reject
replica mutation. Native save restoration can still load subsystem storage for
the snapshot parser, while live presentation comes from the authoritative view.
Shutdown now clears the interface-created flag and freed UI storage when its
windows are destroyed, preventing a later menu teardown from showing freed
maintenance windows.

## Native spell and map restoration (protocol47)

Full-save spell tables parse into separate storage before replacing the live
pool. Counts/extents, slot identity, spell/action ranges and active flags are
validated. Failed records release their partially loaded object lists. Cached
map imports parse and plan every required ID, including collisions with inactive
reserved IDs. Failed capacity, scheduling or file consumption retires only newly
installed runs and preserves the cached input. Existing spell state stays in
place. Successful imports receive new allocation tokens and restore native timers.

Cached animation goals and map timers have corresponding staged readers. Native
map-opening failures propagate to cooperative frozen failure and durable Recover
party; partial native world/sector changes are not rolled back in memory.
Repository create/remove/rename resolve case after adding their physical root,
matching reads on Linux and permitting actual cached-file consumption.
Controlled native cache/table/case checks pass all three builds, and the connected
four-owner Save/Load/Teleport controls pass both host directions. Whole-map
failure/recovery and wider maintained effects remain in acceptance. Exact records
are in STATUS.md.

## Runtime fixture

`tests/integration_bootstrap.py --mode runtime-magic --framebuffer` extends the
24-stage combat fixture with sixteen spell phases. Lobby recipes use five legal
first-tier spells. The later controlled fixture deliberately raises attributes,
HP/fatigue capacity and selected spell knowledge; this is not a legal level-one
balance or natural-encounter test. Script-free paralyzed wolf targets isolate
native spell behavior.

The phases exercise healing, shared Strength of Earth effects and individual
maintenance bars, forged actors, unknown spells, unforced ally targets, stale
turns and another player's source. They end and recreate buffs, require actual
native slot reuse, reject old cancellation tokens, replay the last request,
cast Harm and tile-targeted Fireflash, create/end Orcish Champions, spend native
turn AP, resurrect the host character remotely and resume its movement.

Final member state and canonical subsystem bytes must match in all four engines.
Phase completion waits for native goals/projectiles and effects, independently
of the command receipt.

`evidence/magic-final/{linux-host,windows-host}` passes all 24 preceding combat
stages and all sixteen spell phases with one whole-engine ASan/UBSan participant
in each host direction. The host executes 41 accepted requests and 24 refusals;
each controller also exactly replays its stale-cancellation refusal without
changing those counts.
Recreated buffs reuse native slots while their cancellation tokens change.
All four turns consume four native AP, and all four final member reports and
canonical subsystem bytes agree. The host dies and is resurrected by a remote
controller before its own movement succeeds again.

The fixture calls the native spell-selection/target-application API and the
maintenance-bar callback. It does not establish desktop mouse/keyboard gesture
acceptance. Fifty-six composed frames cover the earlier combat stages, buff
bars, settled summons, spell turns, resurrection and final state. Transient
native spell FX at that checkpoint were shown only on the host. The later
overlay correction below restores received art on replicas. Protocol 15 later
adds positional sound sharing, documented in `AUDIO.md`. The original
checkpoint's screenshots establish the
maintenance controls, native summoned objects and resulting poses, not matching
spell animation/FX presentation. Artificial advanced-character HP/fatigue
displays are also outside numeric HUD acceptance.

Earlier `magic-*` diagnostics are retained separately from `magic-final`.
`magic-first` exposed cooperative party filtering and the freed-window shutdown
fault. `magic-party-targets` exposed a blocked fixture cast; explicit native LOS
checks now choose target placement. `magic-goal-diagnostic` reached an invalid
object target for a tile-targeted summon, and `magic-summon-tiles` exposed the
harness's premature AP assertion. `magic-ap-settled` passed both hosts before
summon capture was moved after animation settling. The failed/interrupted
`magic-visible-targets` and `magic-los-checked` base-combat setup runs are not
acceptance evidence; the base harness now bounds its host goal wait and reports
native goal/turn/position diagnostics instead of waiting indefinitely.

The final source also passes the repeated `magic-commit-runtime` spell run,
weapon/native-controller/original-enemy tests and the remaining action,
dialogue, loot, travel, character and ordinary save regressions. The accepted
set contains eighteen engine drivers plus six additional movement repetitions;
all use the documented sanitizer options without suppressions. One initial
movement/world run rejected an item deletion because an unchanged object still
referenced it. No root cause was established at that checkpoint. The subsequent
reference-lifecycle investigation in `WORLD.md` identified fixture raw disposal
after NPC pickup; native gameplay destruction now fixes that case while the
network guard retains its rejection behavior. Failed evidence remains retained.
The spell checkpoint's total visual review is 71
frames including nine native controller and six original-enemy diagnostics.

## Replica overlay visibility correction

Native overlay/underlay artwork and frames already belonged to ACW2 object
records. Replicas retained cleared local `OF_HAS_OVERLAYS`/`OF_HAS_UNDERLAYS`
bits, so the native renderer skipped the received artwork. Replica attachment
now derives those bits from the installed arrays, using the same layer scan as
native overlay changes. Host callbacks, damage, animation goals and timers are
not replayed. No wire format or protocol-version change is required.

`objects` mode adds 48 restoration cases per engine: all four PCs, with and
without separate pose fields, foreground/background/underlay layers, highest
native slots, native art/frame values and removal against deliberately stale
visibility flags. The pre-fix engine fails the empty-layer case. Linux,
Windows/Wine and ASan/UBSan pass the corrected fixture, including the existing
unchanged-record check. Ordinary save transfers remain covered separately.

`evidence/overlay-magic-final/{linux-host,windows-host}` repeats all 24 base
combat stages and sixteen spell phases in four actual engines. Each process
checks all four PCs' art/visibility consistency throughout the spell fixture,
observes its own overlays, and observes six or seven transitions back to no
overlay. The report's `overlay_frames` counter counts polls with active artwork,
not distinct art frames. A new capture during Minor Healing shows native blue casting effects
in every engine. Summon captures also show the purple native effects on
replicas. The fixture still records 41 host executions and 24 refusals, with
zero replica executions. Sixty-four composed frames cover combat, active
effects, buffs, summons, spell turns, resurrection and final state.

These are TIG framebuffer captures with SDL dummy/software drivers. They do
not establish desktop input or physical Windows acceptance. Views are taken at
different times; they do not establish frame-exact animation synchronization.
Overlay frames still use the 200 ms world capture and acknowledgement barrier;
very short effects can be missed. Smooth transient/projectile presentation and
complete sound coverage remain open; protocol 15's positional service now has
separate measured-PCM acceptance in `AUDIO.md`. Advanced fixture HP/fatigue numbers retain the
earlier numeric-HUD exclusion.

## Remaining acceptance

The full campaign goal remains open. Every spell/technology/item-use branch,
modal follow-up choices such as teleport destinations, remote inventory/service
targeting, complete item mana/recharge/consumable coverage, prolonged upkeep and timed expiration,
dispel/reflection/hostile caster behavior, cross-map spell persistence,
smooth and complete transient spell FX/projectile/sound presentation, desktop
spell gestures,
cooperative active-combat saves/recovery need broader coverage. Native execution of a spell does not establish its complete UI flow.
Protocol 17 retains a separate item-use operation with native scripts/consumption
and personal reading; `USE.md` records its controlled coverage.

## Caster-owned Teleportation (protocol39)

The original known-destination world map now opens for the casting human, even
when a remote player used a consumed scroll. The native movement callback registers
an authorized pending choice; its token, owner, source map, spell and deferred-PC
charge are replicated and saved. Selection validates the current actor, known area,
anti-teleport rules and fatigue before the existing four-human party transition.
Cancel does not charge deferred fatigue, and reopening/rejoining never repeats the
original item Use. Other humans receive the chosen destination through party chat.
College mastery now discounts fatigue admission consistently with actual charging.
The ten-stage `evidence/native-tools-picker-recovery-v44` continuation passes
both host directions. The remote caster opens the native map from a blocked
walking-travel sector, cancels with Escape without fatigue cost, reopens cached
assets, saves/loads the pending choice, and selects Shrouded Hills with the native
mouse-up handler. All four humans arrive through one committed destination/charge.
Forced native mode changes no longer cancel the saved choice; only explicit
cancel gestures do. The fixture supplies spell knowledge and a known destination.
Consumed-scroll recovery, exact cost variants and hostile casting still need
broader acceptance.

## Individual spell feedback and party visibility (protocol40)

Native spell failures/maintenance notices now reach the actual caster or owned
companion's human through acknowledged personal feedback. Presentation follows
its applied world revision. Native invisible party members remain translucent
and visible to their own party; the renderer preserves enemy detection rules
and does not change authoritative flags. Replicated local spell-flag changes
invalidate the local view, including detection changes. Native runtime acceptance
of these additions remains pending.

Detection admission now reads the actual acting character for trap spotting;
one human cannot borrow the host's Detect Traps flag. Environment-spell
cancellation checks its caster. Tempus Fugit activation reads campaign-party
flags for the global native speed rule, independent of the current hosting
character. The installed spell targets Self and All Party Critters, and native
effects/speed values stay authoritative. Runtime acceptance remains pending.
