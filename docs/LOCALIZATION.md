# Personal languages and Polish translation

Each player can choose a language independently, including during a campaign
played with friends who use English. The language choice is local and takes
effect after restarting the game. Everyone still needs the same ArcanumMP build
and matching original game data and gameplay patches.

Open **B → Settings → Language (restart)** or the original **Options → Audio**
tab. With the Polish interface these are **Ustawienia → Język**. Choose
**Original (English)** to return to the original text. Preserve
`arcmp-language.cfg` and your `languages/` directory when updating the game.

## Polish data

ArcanumMP supports **Eselter 1.7**, the Polish translation package updated on
16 April 2023. The source is the author's
[ModDB download](https://www.moddb.com/mods/spolszczenie-arcanum/downloads/arcanum-uap20pl).
Its filename still contains `UAP2.0.2PL.1`; the author's download page identifies
the package as version 1.7 and includes both stock and UAP components.

| Download | Verified value |
| --- | --- |
| Filename | `Arcanum_UAP2.0.2PL.1.exe` |
| Size | 192,014,985 bytes |
| Author's MD5 | `0dc1b2b8e6e561609a3aa83b669f4d75` |
| SHA-256 | `294a1c05a88c8f910a7d23327a4efebe0605f3026294c0a77c53185828e850f2` |

The engine includes the Polish catalog for ArcanumMP's added menus, tooltips,
party controls, conditions, combat feedback and playability tools. The original
game's translation, fonts, menu artwork and recordings come from the separate
author package. These third-party assets retain their existing terms and are
not included in the public engine repository or executable downloads.

If you already have the prepared presentation pack, copy its **`languages/`
directory** beside `arcanum-mp` / `arcanum-mp.exe`. Then choose **Polski** and
restart. Copy the folder without changing the common game's `data/`, `modules/`
or `.dat` files. The game refuses a new language selection when its text pack
is absent; if a previously selected pack is removed it starts in English.

### Simple setup — no Python

Download the **Polish Setup** for Windows/Linux from the
[0.2.2 release](https://github.com/PanBartosz/ArcanumMP/releases/tag/v0.2.2),
or use the helper already included in the new drop-in's `bin/` directory.

1. Close the game and put **`ArcanumMP-Polski.exe`** / **`ArcanumMP-Polski`**
   beside the game executable.
2. Put the author's **`Arcanum_UAP2.0.2PL.1.exe`** in the same folder.
3. Run **ArcanumMP-Polski**, wait for the success message, then launch the game.

The helper embeds its extractor, verifies the author package and prepares the
presentation pack. It automatically selects Polish for the next launch.
It preserves an existing language folder as a backup. Saves, identities and
common gameplay data stay unchanged. Each player keeps their own language.
See the [Polish instructions](../tools/polish_setup/README.md).

The standalone setup also works with 0.2.1. Version 0.2.2 packages the same game
engine as 0.2.1, together with the new setup utility.

**Windows dialogue fix:** update the game executable to **0.2.3** or newer if
Polish menus/films work but conversations remain English. The installed language
data is already suitable; it does not need to be imported again.

### Developer alternative: preparing the pack with Python

Extract the download using [innoextract](https://constexpr.org/innoextract/),
then run the supplied Python importer. The following example uses the stock
game variant; select `--variant uap` only when the entire party already uses
matching UAP gameplay data.

```sh
innoextract --collisions=rename --lowercase --output-dir polish-extracted Arcanum_UAP2.0.2PL.1.exe
python3 tools/import_language.py --source polish-extracted/app --game /path/to/Arcanum --language pl --name Polski --variant stock --encoding cp1250
```

On Windows use `innoextract.exe` and `py` in place of `innoextract` and `python3`.
The importer is included under `tools/` in the drop-in bundles. It requires
Python 3.8 or newer. Extraction does not run the downloaded program.

The stock import selects 903 presentation resources: 712 dialogue files,
94 message files, 45 interface/font files, 5 movies and 47 recordings. It ignores
executables, maps, scripts, prototypes and rules, and strips translated dialogue
conditions and actions. `pack.json` records the selected resources and hashes;
`SOURCE.txt` retains provenance. Existing language folders are never overwritten.

**Nie uruchamiaj całego spolszczenia na jednej z kopii wspólnej kampanii.**
Użyj przygotowanego folderu `languages/pl`: polskie teksty i grafiki pozostaną
lokalne, a zasady gry i dane kampanii będą takie same u wszystkich graczy.

## What stays shared

Dialogue choices, conditions, effects, quests, items and saves use the common
original campaign. A shared conversation carries the original text and resource
identifiers. Each participant resolves the identifiers using their own language
pack, including the transcript and restored conversations. Quoted service
prices preserve the amount chosen by the native game.

Language folders and personal settings are excluded from the gameplay content
fingerprint. Language changes do not require a new character or campaign.
Update all executables together: version 0.2.0 uses network protocol 60.

Unknown or incompatible translated entries fall back to their original text.
Old transcript entries saved before resource identifiers were recorded retain
their recorded text. Names, chat and personal notes remain as the player wrote
them; their existing input limitations are unchanged.

## Additional languages

The same mechanism discovers `languages/<code>/` folders with a `name.txt`,
`mes/` and `dlg/` directory. Codes use lower-case ASCII letters and hyphens, up
to 15 characters, for example `de`, `fr` or `ru`. Import extracted presentation
data with the corresponding `--language`, `--name` and `--encoding` options.
Other translations must be supplied separately; they are not bundled.

Text resources and bitmap fonts must agree on a code page. Supported UI catalog
encodings are CP1250, CP1251 and CP1252, selected in `encoding.txt`. Polish
defaults to CP1250, Russian to CP1251 and other languages to CP1252. The original
bitmap renderer still requires a matching font; this is not a Unicode font
replacement.

An optional `ui.tsv` translates ArcanumMP's added interface. Use UTF-8 and the
same JSON-quoted English-key / tab / JSON-quoted-translation layout as
[`assets/localization/pl.tsv`](../assets/localization/pl.tsv). Preserve printf
placeholders, their order and widths, and dialogue `@pcname@` / `@npcname@`
tokens. Translated gameplay instructions are never executed.

## Verification for 0.2.0

- Asset-free checks cover personal choice/restart behavior, code-page output,
  invalid resources, placeholders and price formatting, dialogue transport,
  retained references and older transcript/recovery formats.
- Four real engines completed two retail Virgil conversations over TCP: Polish
  Linux host, English Linux chooser, Polish Linux observer and English Windows
  client under Wine. All four recorded identical canonical transport bytes;
  Polish and English displays differed as expected. Both choices executed once.
- Native captures were reviewed for Polish settings, inventory and dialogue,
  including accented glyphs and a native service price.
- Native empty-loot checks exercised last-item removal, putting an item back,
  hidden/non-droppable items, pending container spawning, living/dead critters
  and the same read-only query in replica mode. Hovering the empty body displayed
  **Pusto** without requiring Alt.

The scoped evidence is retained locally under `build/localization/`; no campaign
route replay was needed for this change. This does not certify every line of
the third-party translation or every community patch combination.

## Standalone setup verification for 0.2.2

Both executables imported the real Eselter 1.7 archive and produced all 903
presentation resources byte-for-byte equal to the previous importer. The Linux
run had no Python or external extractor on PATH; Windows ran under Wine. Checks
covered accented/spaced game paths, launch from an unrelated working directory,
missing/invalid inputs, retained previous data, automatic personal selection,
unchanged game-data/identity markers and temporary-file cleanup. Import unit
checks also cover component selection and publication rollback. The Windows
extractor's component-path and legacy path-length cases are handled explicitly.
No game-engine or campaign retest was needed: the game binaries are unchanged.

## Windows dialogue correction in 0.2.3

Canonical text references use `/`, whereas the Windows DAT index uses `\`.
Windows file-system reads accepted both separators, but archive searches compared
the path literally. Reading the original dialogue template therefore failed,
and local presentation retained the host's English text. Translated UI messages
and loose movie/font resources use different reads, explaining the partial
localization. The common TIG path conversion now normalizes both spellings to
the native separator before indexing and lookup.

The reported behavior was reproduced on the previous executable. The corrected
four-engine session used a Polish Linux host, Polish Windows/Wine chooser,
English Linux observer and Polish Windows/Wine observer. Both Windows clients
resolved the same Polish retail replies as the host; the English observer kept
English, canonical transport bytes matched and both choices executed once.
The native dialogue probe now checks resource-read parity and a translated
Polish first page, so its previous English-only Windows coverage cannot silently
pass this case. Retained evidence: `build/localization/windows-dialogue-before/`
and `build/localization/windows-dialogue-after/`.
