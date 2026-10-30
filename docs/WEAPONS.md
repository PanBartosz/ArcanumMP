# Equipped weapons and attack choices

The cooperative combat service uses native weapon selection, range, attack
goals, AP costs, ammunition, hit rolls, critical effects, projectiles and damage.
This checkpoint adds attack choices and equipped-weapon coverage; it does not
establish complete campaign combat acceptance.

## Intent and native execution

Protocol 13 keeps the 51-byte combat request and encodes an explicit force bit,
native body location and attack mode in its value byte. The host validates
ownership, turn generation, target and range before constructing native goal
parameters. Selecting a body location or mode does not imply a forced attack.
An existing goal is confirmed only when its body location and mode also match.
Receipts retain the existing capture/applied-world boundary.

The ordinary click captures that controller's auto-attack choice: one attack
with approach, or repeating attacks with approach. Shift-click uses the native
attempt goal without approaching and does not repeat. Comma selects head,
period selects leg, and slash selects arm, matching the native cursor labels.
The neutral torso/default location retains native random hit-location selection.
Turn-based timing still permits one native attack per submitted intent; it does
not turn repeating mode into an automatic turn spender.

Two previously unused high bits of the native animation run flags retain the
per-goal mode. The native animation structure and serialized layout are unchanged.
The repeat decision applies both to ordinary attack continuation and to the
separate bow/pistol/rifle frame rewind in real time. Applying it only to the
former allowed a one-attack request to keep firing until ammunition ran out.
Local/native default-mode calls retain their prior behavior. This does not add
per-controller automatic-defense or automatic-weapon-switch preferences;
those native settings still belong to the host simulation.

Arrows and bullets remain native inventory quantities; the mod does not invent
a reload or magazine system. Native returning weapons use their normal in-flight
flag and return path. Native disposable item throws cannot carry called body
locations; such a request is refused before removing the item. Their wider
aiming/stack/throw UX remains open.

Replica world application now refreshes the original HUD counter after object
fields and inventory references are installed. Previously the inventory had the
new weapon/ammunition while the HUD could still show the old gold counter.
No ammunition is spent optimistically on a client.

## Four-engine fixture

`tests/integration_bootstrap.py --mode runtime-weapons --framebuffer` extends
the existing 24-stage combat fixture with ten weapon phases. Four actual engines
run over TCP with Linux and Windows/Wine hosting in turn; one Linux participant
uses whole-engine ASan/UBSan. The fixture uses custom human PCs, then deliberately
raises stats/skills for controlled combat. It creates four script-free,
paralyzed, high-HP/fatigue-immune wolf targets. It does not claim legal level-one
builds, natural enemy AI or encounter balance. The original-enemy/legal-character
fixture remains separate in `ENCOUNTERS.md`.

The host creates unmodified retail sword, bow, revolver and boomerang instances
and supplies ammunition through native inventory functions. Players equip them
through real ordered item requests. Phases cover:

- A native mouse click per player with auto-attack off, followed by an idle/
  projectile barrier and a quiet interval. Each actor resolves one attack;
  bow and revolver consume one unit of their own ammunition.
- Shared turn-based combat, called shots for head/leg/arm, and an unforced
  called-shot attempt against an ally that must be refused for every controller.
  Native animation completion governs end-turn retries.
- A remote repeating bow request while the host's auto-attack setting is off,
  followed by an accepted remote stop and a projectile barrier.
- A remote sword Shift-click at range that cannot move or hit, then a normal
  sword request that must approach through native movement and reach melee range.
- An empty bow equipped through the item service, followed by a Shift-click
  that cannot create ammunition, move the actor or damage the distant target.
  Native automatic weapon switching remains in effect.

Optional native attack telemetry records the body location before native rolls
and the resolved outcome afterward. A native critical failure can clear aiming
flags and redirect damage to the attacker; the harness preserves that result
instead of mistaking it for lost request data. Telemetry is inactive outside an
explicit harness and sends no native structures over the network.

Final equipment IDs, quantities, target damage and canonical story bytes must
agree in all four engines. The fixture's phase barrier waits for native actor
goals, returning weapons and live projectiles; an accepted intent is not treated
as completed damage.

The accepted run is `evidence/weapons-hud-final/{linux-host,windows-host}`.
Both host directions pass all 24 base stages and ten weapon phases, including
clean whole-engine access, undefined-behavior and leak checks. All forty composed
frames were reviewed: base turn/realtime combat, equipped single attacks,
weapon turns and final state. The bow/bullet counter icons and decrementing
quantities are visible. Separate native controller and original-enemy regressions
add nine AP and six encounter frames on Linux, Windows/Wine and sanitizers.
The 55-frame review scope is retained in `evidence/weapons-checks/visual-review.json`.
The native controller's overlapping introduction floaters remain excluded from
layout acceptance, as do the artificial weapon fixture's HP displays.

Earlier `weapons-first`, `weapons-mode-diagnostic`, `weapons-once-fixed` and
`weapons-native-aim` runs are retained as failed diagnostics. They exposed the
native frame-rewind defect and harness mistakes around critical-failure telemetry
and repeat-stop monitoring. `weapons-stop-fixed` passed before the HUD correction;
it is superseded by the accepted run above.

## Remaining acceptance

The native fixture covers these four weapon families and a controlled ammo
exhaustion case. Advanced ammunition consumers, expert bow double shots,
disposable thrown stacks, explosives, line-of-fire branches, shields/armor,
enchantments, weapon damage/breakage, normal armed campaign encounters and combat
equipment AP costs still need dedicated coverage. Smooth transient-projectile
presentation, sounds/floaters, desktop gestures, spells/skills, resurrection,
active-combat saves/recovery remain open.
Headless composed frames establish only their documented rendering scope.
