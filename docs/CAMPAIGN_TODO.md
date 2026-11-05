# Campaign issues

Please report reproducible campaign problems through
[GitHub Issues](https://github.com/PanBartosz/ArcanumMP/issues). Include the mod
version, location, party roles and a useful save where possible.

The original main campaign path is connected, including shared ending presentation.
Development coverage uses retained checkpoints and strengthened characters; it is
not a claim of one uninterrupted normal-party playthrough or every side quest.
Manual multiplayer play has also covered much of the first area.

The reported intermittent black map after original crafting remains under
observation; see the open item below. Further fixes
will follow reports from actual play. See [current status](STATUS.md),
[the player guide](PLAYER_GUIDE.md) and [contributing](../CONTRIBUTING.md).

## Completed in 0.2.0

- [x] Obtain and verify Eselter Polish translation 1.7.
- [x] Personal language selection, mixed-language dialogue/recovery and Polish
  added UI; four mixed-language engines verified, including Windows/Wine.
- [x] Empty/Pusto feedback on looted bodies and containers.

[Language setup and scoped verification](LOCALIZATION.md).

## Completed in 0.2.2

- [x] Self-contained Windows/Linux Polish setup, automatic language selection
  and retained backups. Both real imports match all 903 presentation resources.

[Setup instructions](LOCALIZATION.md#simple-setup--no-python).

## Fixed in 0.2.3

- [x] Polish Windows dialogue falling back to English despite Polish menus and
  movies. Normalize canonical DAT paths; verify both a Windows/Wine chooser
  and observer with Polish/English participants. No pack re-import is needed.

[Cause and verification](LOCALIZATION.md#windows-dialogue-correction-in-023).

## Fixed in 0.2.4

- [x] English multiplayer connection/error text and missing UI fragments: 123
  added Polish translations, presentation hooks and builtin fallback for older
  personal catalogs. Polish Linux/Windows-Wine lobby views verified.
- [x] False file mismatches from personal/generated files inside game-data folders.
  Preserve verified older save/character identities and named slots while
  retaining strict gameplay checks. Four mixed native engines join despite
  different personal files.

[File compatibility and verification](FILE_COMPATIBILITY.md).


## Open after 0.3.0

- [ ] Confirm the intermittent black client map after original-T pistol crafting
  is resolved. UI/chat remained usable; travel restored the view. Full viewport
  repaint and autosave changes are included, and native cave/Windows-Wine
  crafting checks pass, but the exact reported failure has not been reproduced.

## Added and fixed in 0.3.0

- [x] F12 gold transfer with recipient and exact amount, validated on the host.
- [x] Host periodic/travel autosave preferences, three rotating slots and capture
  without reloading clients. Manual saves remain separate.
- [x] F12 Load campaign without a quick save; unanimous named/autosave loading
  and refusal without abandoning the running game.
- [x] Native hover descriptions retained across passive refreshes; unchanged
  combat batches no longer reset cursor presentation.
- [x] Native F12 screenshot conflict moved to Print Screen; screenshot files
  excluded from compatibility. Unused prototype identifier bytes canonicalized;
  verified older save identities retained, actual gameplay fields still checked.

[Controls and scoped checks](COORDINATION.md#focused-party-controls-check-in-030).


## Corrected and added in 0.4.0

- [x] Native floating status gains/endings on PCs and NPCs, translated locally.
- [x] Repair F11 log chrome, separate F12 chat/actions and fix hidden click targets;
  theme the lobby from the main menu.
- [x] Replace custom automatic pause holds/popups with native turn-based combat,
  using the host's 5–40-tile trigger radius from a player character.
- [x] Autosave before map/world departure; loading returns to the old map without
  repeating the transition. F12/save-browser Autosaves selects the newest copy.
- [x] Personal, persistent switch for world-only shake on received critical hits.

[Behavior and focused evidence](COORDINATION.md#playthrough-feedback-corrections-in-040).
