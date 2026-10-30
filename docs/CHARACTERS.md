# Lobby character creation

The lobby's Create character button opens the original identity and character
point screens. Players choose a name, portrait, gender, race, background,
attributes, HP/fatigue points, skills, spells and technological disciplines.
The connection continues to poll while the wizard is open. Accepting returns to
the lobby with the selected character; cancellation retains the previous lobby
selection. Players still need to mark Ready after accepting.

This is an implementation checkpoint. Protocol 30 adds starting purchases; full campaign acceptance remains
unfinished. Live point allocation now has a controlled four-engine
fixture in `PROGRESSION.md`, with wider progression acceptance still required.
Accepted drafts can now be edited, saved
per profile and loaded after restarting the game; see `DRAFTS.md`. Custom
characters receive the native background's starting outfit/items and gold when
the host creates them. The Equipment panel uses the original vendor stock and native prices/rules
with host-replayed trades; see `DRAFTS.md`. Preset selection remains available alongside creation.

## Authority and launch

The client edits a temporary local PC using the native wizard. It submits a
bounded creation recipe rather than an object or save archive. The recipe
cannot specify money, inventory, effects, training, XP, level, script IDs,
ownership or campaign globals. The host creates these from its matching retail
content and ordinary engine rules.

Before opening the wizard, the lobby requests Not ready and waits for the
host's roster acknowledgment. If campaign launch wins that race, creation does
not open. Character changes invalidate the party's readiness as before. A
disconnect closes an active wizard and returns to the failure/party UI. Native
callbacks cannot start a solo campaign from this creation flow.

The session library validates representation and bounds. Before creating the
campaign, the host engine validates every selected prototype and replays
each custom recipe on a disposable, bare generic-PC pool instance. These objects
never enter sectors, generate inventory or execute object scripts, and are
deallocated after the check. Invalid choices leave the host in the lobby with a
creation error; the menu's local PC and the campaign registry are preserved.

Replay uses the retail playable race/body/gender rules, enumerates legal native
backgrounds and portraits, and applies their native effects. It then spends the
initial character points through native attribute costs and skill, spell and
discipline requirements. Base values cannot fall below their initial accepted
levels. HP/fatigue additions cost points. The final unspent balance must match
exactly. Native effective-stat requirements and race/background modifiers remain
in force. No forced spell acquisition or raw field-array import is used.

After validation, normal player creation constructs each owned PC. The same
recipe is replayed, native starting inventory is generated, and ordinary
equipment selection runs. The existing owner/ObjectID registry and initial
snapshot carry the resulting PCs to every participant. Custom PCs use the
generic PC prototype (16066), so the host's custom primary character does not
activate a retail preset's special campaign flags. The recipe is lobby metadata;
normal engine save fields contain the resulting character after launch.

## Protocol 11

The CHARACTER payload remains 36 bytes for a preset. A custom selection is 148
bytes: generic PC prototype (u32), character name (32 bytes), and ACH1 (112 bytes).
Its duplicated names must agree. Each ROSTER player record grows to 183 bytes:
the existing 70 bytes, a custom flag, and 112 recipe bytes. Preset recipe bytes
are zero. The complete roster is 737 bytes, within the existing 1,024-byte frame
budget. Profile keys remain absent from broadcasts.

ACH1 uses big-endian integers and canonical zero padding:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 4 | ASCII ACH1 |
| 4 | 1 | Playable race, 0–7 |
| 5 | 1 | Gender, 0–1 |
| 6 | 2 | Background number |
| 8 | 4 | Portrait number |
| 12 | 2 | Base HP points |
| 14 | 2 | Base fatigue points |
| 16 | 2 | Unspent character points |
| 18 | 16 | Eight base attributes in native Stat order, u16 each |
| 34 | 16 | Twelve basic and four technological skill point counts |
| 50 | 16 | Sixteen spell-college levels |
| 66 | 8 | Eight technological discipline degrees |
| 74 | 32 | Printable ASCII name, NUL terminated with zero tail |
| 106 | 6 | Reserved, zero |

Wire bounds precede all native enumeration/access. They are deliberately wider
than normal starting builds; passing them does not establish a legal build.
Only native replay establishes legality. Names cannot be empty or all spaces.
The original wizard uses its 23-character name-entry limit.

## Verification and reproducible checks

`tests/integration_characters.py` runs the retail rules and native wizard on
Linux, Windows under Wine and a whole-engine ASan/UBSan/leak-check build:

~~~sh
python3 tests/integration_characters.py \
  --linux-runtime runtime/campaign-probe-linux \
  --windows-runtime runtime/campaign-probe-windows \
  --sanitized-runtime runtime/campaign-probe-asan \
  --output evidence/characters-native-rules-final
~~~

The rules fixture enumerates all 12 legal race/gender combinations and 504
legal background combinations from this installation. It checks baseline,
native skill/spell/discipline/HP/fatigue spending, and an attribute increase.
Each accepted recipe is encoded, decoded and replayed; a forged extra unspent
point must be rejected. Additional checks reject invalid portrait/background/
gender choices and an invalid party before replacing the menu PC. The UI
fixture uses actual TIG mouse/text events to cancel, enter a name, choose an
identity, allocate HP and dexterity, accept and return to the main menu.

The portable `character` CTest checks canonical byte order, every truncation,
reserved/string padding and 10,000 mutated records. The socket `session` test
checks custom roster propagation, readiness reset, changing back to a preset and
locked selection rejection. The later `draft` file tests bring the portable
suite to nine tests on Linux, Wine and
ASan/UBSan builds.

`tests/integration_bootstrap.py --mode runtime-characters --framebuffer` creates
four different characters through the native wizard, submits them over TCP and
starts the ordinary runtime in both host directions. A forged point balance
must first return the host to the editable lobby without disconnecting; the
fixture repairs that draft, readies the party again and starts successfully.
Each participant's applied
recipe must equal its own submitted bytes after host construction and snapshot
loading. The fixture then exercises owned movement/stop, remote NPC approach,
chooser/observer dialogue, cancellation and frozen host loss. Composed TIG
identity/allocation/game frames are retained separately from desktop captures.
Evidence: `evidence/characters-launch-final` and
`evidence/characters-native-rules-final`. All 32 desktop captures, 24 final
four-process captures and six standalone wizard captures were visually reviewed.

`tools/desktop_smoke.py --custom-characters --launch` drives the main-menu lobby
and all four native wizards with OS input, then checks the ready/chat roster and
ordinary campaign launch. Actual game-window captures are recorded for both
host directions. Windows validation here uses Wine; testing on real Windows is out of scope.

The longer desktop fixture exposed a pre-existing dialogue timer defect: the
first page could time out immediately when the process had already spent more
than 30 seconds in menus. Dialogue construction now receives its actual initial
monotonic time. The portable test starts a queued conversation at 120 seconds,
verifies its first poll succeeds, and still rejects a genuine 30-second stall.

The first desktop input trials are retained as failures. SDL traces showed a
compositor cursor warp reaching the application between button press and release.
The harness now flushes actual virtual-pointer motion before clicking. A separate
focus assertion stopped one trial before delivering a click to a different
window. These failures are not counted as successful game-input acceptance.

Protocol 30 adds starting purchases and multiple saved drafts/import/export.
Still required: wider loadout/content acceptance and host-loss fault cases during editing,
custom content/portrait variants, custom-PC save/recovery product controls,
level-up authority and whole-campaign acceptance.

## Protocol 30 extension

`ACH2` is 176 bytes. It retains ACH1 offsets 4–105, stores a trade count at 106,
reserves 107–111 as zero, then stores sixteen 4-byte entries at 112: prototype
u16 and signed quantity i16, big-endian. Positive quantities buy; negative sell.
Zero quantities, -32768, zero prototypes, more than sixteen entries and nonzero
unused entries are rejected. Native replay checks legality and availability.
The custom CHARACTER message is 212 bytes; each roster player is 247 bytes and
the full roster is 995 bytes, within the 1,024-byte payload limit. Legacy ACH1
loads only from saved draft compatibility; all live peers require protocol 30.
