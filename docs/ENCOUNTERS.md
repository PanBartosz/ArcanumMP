# Native encounters, combat rewards and dead controllers

This checkpoint extends the combat transport with native campaign behavior.
It does not establish campaign-wide combat completion. `COMBAT.md` describes
intentions, AP/turn projection, input routing and receipt ordering.

## Activity around the party

The native NPC heartbeat originally used the host's local PC for its 30-tile
activity cutoff and polling delay. NPCs around a remote PC could therefore stop
running even though the remote player was beside them. `coop_campaign_nearest_player`
now supplies the nearest present owned PC in explicit host authority. It excludes
missing, off, destroyed and extinct PCs. Unconscious and dead present characters
remain activity anchors; native AI still decides which characters are valid targets.
Local and replica authority retain the ordinary local-PC anchor.

NPC heartbeat admission, heartbeat delay and NPC availability use this anchor.
Native fidget and looping animation proximity also follows the party. Sound
attenuation stays relative to the local listener; remote combat audio still needs
its own presentation transport. A threatened NPC's conversation callback now
addresses the actual registered chooser rather than the host's local PC.

Native perception, hostility checks, scripts, movement, attack goals, hit rolls,
resistances and NPC turn timing remain in the engine. This is not a second AI
implementation. The four-PC initiative union and its sector locks remain as
specified in `COMBAT.md`.

## Cooperative healing and party strength

Recruited healer AI considers the other registered humans and their followers
after its native self, owner and owned-follower priorities. Healing, poison cures
and resurrection still pass through the original AI's knowledge, skill, spell,
range and resource checks. Ownership and companion orders stay personal.
The original Tarant continuation confirms Virgil healing two non-owning humans,
including one who arrived with3 HP; it supplies no spells, healing or resources.
Wider cure/resurrection and other-owner follower cases still need acceptance.

Native flee calculations count all registered humans and their follower trees
as the campaign party. Other NPC groups retain their original calculation.
Recursive follower enumeration now advances through each descendant instead of
looping indefinitely when a companion has a follower. A controlled native continuation on Linux, Windows/Wine and whole-engine
sanitizers now checks ten actors, including two nested companions. Every actor
sees the full ten-member strength. Longer nested-party combat/recovery remains open.

## Cooperative allegiance and defense

Native faction checks recognize all registered humans and their current follower
trees as the cooperative party. Native NPC leader changes update allegiance,
including a whole nested subtree leaving or returning to a registered human.
Unrelated NPCs keep their native faction, origin and reputation rules.

Companions protect other humans and their companions across ownership. Ordinary
attacks against that party receive the native party-refusal classification.
Dynamite's bystander notification excludes all cooperative companions, while
unrelated bystanders still react. Grenade AI considers humans as well as NPCs
when checking whether an explosion would hit someone it protects. Damage and
explicit forced attacks retain native behavior.

The disposable `party-ai` engine mode covers100 allied pairs,20 unrelated-NPC
refusals, four humans/six companions, a two-level follower subtree, native leader
changes, full-party strength and the single-player boundary. Actual native
explosion notification provokes an unrelated NPC; six companions owned by other
humans acquire that NPC as their defensive combat focus, with none targeting the
placing human. All three native builds pass with clean sanitizers. This checks
native defensive intent, not a completed damage exchange or an actual grenade
throw. Grenade throw/range variants and prolonged allied combat still need
acceptance. Evidence: `evidence/native-party-allegiance-v45`.

## Companion yield during interactions

Original interaction approaches can now ask an idle cooperative NPC to move aside
through the existing PleaseMove animation. The normal body-aware route remains
preferred; a bounded fallback prefix can look past eligible idle party NPCs and
admits native yield at its first blocking body. A farther blocker is approached
through successive legal prefixes. Step execution still checks collisions;
human, unrelated and fighting/fleeing/surrendered blockers remain intact, and
active turn-based rules are unchanged. Native three-build body controls preserve
those boundaries; full distant-prefix native traversal remains unaccepted. In the
original Windows-host dungeon, this resolves Z'an Al'urin blocking the stair:
the first native Use then completes with three planned steps and all four human
landings. See `evidence/native-party-yield-v45` and the qualified Windows route
prefixes recorded in `STATUS.md`.

## Combat rewards

Damage XP and the native kill bonus now use `critter_give_combat_xp`. When an
owned PC, or an NPC whose native leader chain reaches an owned PC, earns combat
credit on the host, every living registered PC receives the same base reward.
Each award passes through that character's native difficulty/background/effect
modifiers and level recalculation. Unconscious living PCs remain eligible;
dead PCs do not. There is no distance split or last-hit race between players.

The victim's native damage-XP pool is consumed once. Its kill-worth field is
retired by the native death path. Sharing does not multiply those budgets or
change monster XP tables. Ordinary local-role rewards retain their original
behavior, including native follower credit. Direct replica XP mutation is denied.

Quest rewards retain their separate per-character implementation; they do not
call the combat-sharing helper and are not shared twice. General script/dialogue
XP, alignment and reputation now have explicit story-operation sharing in
`CONTINUITY.md`. Kill-journal attribution and wider follower morale are not
merged by this change. Their complete campaign policy remains open.

## Continuing after a character dies

The native end-death UI event used to reset the whole game when the process's
local PC died. It now leaves a registered cooperative campaign alive so other
players can continue. Dead controllers still own their original permanent PC
identity, but ordinary action validation refuses their commands.

Owned PC bodies do not enter native corpse decay on the host. An old decay event
loaded from a save is also consumed without destroying a registered PC.
Death-animation cleanup also preserves owned bodies on blocking terrain.
Ordinary NPC/item decay and blocked-tile cleanup retain their native behavior,
and replicas cannot execute decay.
This preserves the object and ownership record needed for later resurrection.
It does not implement resurrection controls, corpse inventory permissions,
party-wipe UX, reconnect or atomic cooperative saves.

## Confirming an attack already in progress

Native auto-defense can start an attack before an explicit player click arrives.
The native goal-submission API reports an identical existing goal as false. The
cooperative service now recognizes the same native attack parameters and confirms
that intent without queuing another goal or spending AP again. This query is
read-only; ordinary local/native submission semantics are unchanged. New request
sequences still receive fresh ordered receipts, while exact wire replays retain
the original cached receipt.

## Harness and scope

`tests/integration_encounter_engine.py` runs `encounter-engine` on Linux,
Windows/Wine and whole-engine sanitizers. It uses an existing crash-site lesser
boar with its original scripts, HP, inventory and XP budget. Its prototype ObjectID
number is 28447: basic prototype/description number `BP_LESSER_BOAR` 28427 plus the
native 20-ID offset. It is not the `BP_DARK_KNIGHT` description with the same raw
numeric value.

The fixture puts a remote owned PC beside that enemy and keeps the host at the
native crash-site entrance, beyond 30 tiles. All four characters use the same
legal custom fighter recipe as the connected fixture below. Auto-defense is
temporarily off until an incoming native hit is observed, then enabled for the
fight; the original option is restored afterward.
After that observation, two other owned PCs join the fight while the host stays
distant. The enemy must focus and damage the remote PC, die through native
combat, and reward the living party. Separate comparisons check each fixture
character's award against the native XP helper. A temporary leader link, restored
before AI runs, checks follower credit. Further checks cover replica denial,
corpse replay and dead-PC eligibility. A staged
old decay callback cannot destroy the host's dead PC. A separate blocked-terrain
death case preserves the corpse through native animation cleanup, then moves it
back to clear floor. Six seconds of native pings cannot trigger the single-player
world reset. A complete save bundle then restores the dead owner into a frozen
replica while retaining all four IDs,
owners, HP damage and XP. That restore is not a reconnect or rollback workflow.

`tests/integration_bootstrap.py --mode runtime-encounter` runs four actual game
engines over TCP in both host directions. Each player submits a legal level-one
human fighter through the existing custom-character lobby protocol: two Dexterity
points, one Strength point and two melee investments spend the native starting
budget. The host replays the recipe, and all participants check the initial
Strength 9, Dexterity 10, melee 2 points (base skill 8) and zero unspent points.
Native starting inventory is retained; this fixture does not equip a weapon or
implement starting purchases.

Five phases cover distant automatic aggression and incoming damage, explicit
attacks, another original enemy's turn-based turns, player attacks, shared XP,
the host character's death and a surviving controller's accepted command.
Auto-defense stays off for the incoming-hit check. The remote PC then starts
a native approach from 12–16 tiles away and submits two ordered requests while
that attack goal runs. Existing-goal confirmations are counted; auto-defense is
enabled after confirmation, and its original setting is restored at cleanup.
This prevents an early native critical kill from racing the first explicit
request. NPCs keep
their native stats/scripts/AI; nearby original creatures can join the fight.
Only the final phase deliberately kills the host character to test lifecycle
behavior. Fixture placement checks ground art, terrain and blocking occupants.
It does not simulate a network host loss.

Composed TIG frames show the separated encounter, shared turn UI and surviving
party. These are headless software-rendered game frames, not desktop input tests.
Equipped/ranged weapons, aim/ammunition, spells/skills,
resurrection, party defeat/recovery, combat feedback/audio and encounters across
all story regions remain release requirements.
Scripted body removal and disintegration also need coverage; these guards do not
establish that every campaign death path preserves ownership.

The final native evidence is `evidence/encounter-native-commit-final`; the final
connected evidence is `evidence/encounter-runtime-entrance-final` in both host
directions. `evidence/encounter-checks` retains build logs, regression commands,
portable test logs and visual review scope. Earlier encounter runs are diagnostic
or superseded, including temporary AI tracing, the over-distant placement and the
early auto-defense race. The final source contains no temporary AI tracing.
The 32 connected frames, six native encounter frames and nine native AP frames
were reviewed. AP frames remain diagnostics because the native introduction's
floating lines overlap; they do not establish dialogue layout acceptance.
