# Campaign issues

Please report reproducible campaign problems through
[GitHub Issues](https://github.com/PanBartosz/ArcanumMP/issues). Include the mod
version, location, party roles and a useful save where possible.

The original main campaign path is connected, including shared ending presentation.
Development coverage uses retained checkpoints and strengthened characters; it is
not a claim of one uninterrupted normal-party playthrough or every side quest.
Manual multiplayer play has also covered much of the first area.

No demonstrated campaign progression blocker is currently listed. Further fixes
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
