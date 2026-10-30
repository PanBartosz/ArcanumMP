# Editing and saving lobby characters

The lobby now keeps the last accepted custom character for each local player
profile. Create character opens a fresh wizard. Once a custom character is
selected, that button becomes Edit character; New character starts over, and
Load saved selects the profile's saved build. Choosing a preset leaves the
saved custom build available. These controls are available before campaign
launch. Starting purchases are available through Equipment; see below.
Wider multiplayer level-up acceptance remains unfinished.

Edits are a local transaction until accepted. Cancelling preserves both the
accepted lobby selection and its saved file. Accepting submits the new recipe
and saves it. Clients wait for roster confirmation before selecting or editing
another character or marking Ready. Every accepted selection still clears the
party's readiness. Preset selection also waits for roster confirmation before
another selection or Ready. A failed disk save leaves the selected online character
usable and displays that the draft could not be saved.

## Native editor and point limits

The wizard starts with a fresh generic PC. Its identity is seeded from the
accepted draft, with points unallocated. When the allocation screen opens, the
native editor first records this character's original attribute/skill/spell/
discipline floors. The draft's purchases are replayed after that record. This
ordering lets the player refund old purchases while retaining all native point
costs, requirements and minimum accepted values. Loading the entire build before
opening the editor would incorrectly make old purchases non-refundable.

Returning from allocation to identity preserves the current working recipe.
Name and portrait changes preserve allocations. Changing race, gender or
background resets allocations when the player enters the point screen, using
the new identity's native starting state; a message explains the reset. Cancel
still restores the accepted lobby selection. A fresh New character has no
inherited draft or point floors from an earlier character.

Closing this lobby wizard discards its native feedback history as well as its
visible message, so a rejected refund cannot reappear on the campaign HUD.

The host continues to validate all builds through the existing native replay
before creating any campaign PC. Editing and saved-file loading provide no
money, inventory, XP, scripts, training or campaign object identities. See
`CHARACTERS.md` for ACH1 and the creation authority boundary.

## Saved file

The private runtime stores one file per profile:
`arcmp-character-<32 lowercase profile hex digits>.draft`.
The profile comes from `arcmp-player.id`; changing a nickname keeps the same
draft. The current implementation retains the last accepted custom build for
that profile. The Characters button opens eight named slots plus Last accepted. Saving an
accepted build updates its matching name/race/gender/background slot or fills an
empty one. A full library preserves existing slots; Save here explicitly replaces
the selected slot. Select loads a saved build, New starts fresh and Edit selected
opens the native editor. Remove slot retains a `.deleted` backup.

Import/export uses the file path field in that panel. Export writes a shareable
creation recipe without the player's ownership identity. Import accepts a share
file or a legacy/current draft from another profile, validates the content and
native rules, and saves a new local envelope for the current profile. Import
never changes `arcmp-player.id`, campaign ownership or a live character.

Protocol 30 writes `ACDRAFT2`: 236 bytes, with big-endian integers. Legacy
172-byte `ACDRAFT1` files remain readable with no selected purchases.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | ASCII ACDRAFT2 |
| 8 | 32 | Retail content manifest SHA-256 |
| 40 | 16 | Player profile identity |
| 56 | 176 | Canonical ACH2 creation recipe |
| 232 | 4 | CRC-32 of the preceding 232 bytes |

Loading checks exact length, version, checksum, recipe shape, game content and
profile before changing the output. The engine then checks native build
legality before selecting it in the lobby. An invalid, unsupported or mismatched
file is retained and reported; the current selection is preserved. These files
contain creation choices. Campaign saves, live-PC ownership, reconnect and
recovery continue through their separate campaign systems and remain open work.

Saving creates an exclusive file with a random temporary suffix, writes and
flushes the complete envelope, closes it, then replaces the canonical path.
POSIX uses `rename`; Windows uses `MoveFileEx` with replacement and write-through.
An interrupted temporary file is ignored on load. Pre-commit failures preserve
the canonical file and clean up the temporary file. This gives atomic record
replacement; filesystem and power-loss durability still depends on the host
filesystem.

Only the character recipe crosses the existing lobby protocol. Draft envelopes
stay local. Share files use `ACBUILD1`: 8-byte magic, 32-byte content ID, 176-byte ACH2
recipe and a 4-byte CRC (220 bytes). Profile envelopes stay local; the protocol-30
lobby carries only the bounded recipe. Slot files append `.slot-01` through
`.slot-08` to the profile's canonical draft filename.

## Verification

The portable `draft` CTest uses real files to check replacement of an existing
draft on Linux and Windows/Wine, profile/content mismatch, every truncated
length, extra bytes, CRC and shape rejection, preservation of the output on
failure, ignored interrupted files, invalid-save and failed-replace preservation,
temporary-file cleanup and POSIX permissions. All nine portable tests run with
native, Windows/Wine and ASan/UBSan builds. Each of the 172 byte positions also
has a bit flipped independently to check corruption rejection; a checksum-valid
unsupported header is rejected separately.

`tests/integration_characters.py` retains the 1,512 native creation recipes per
platform and adds a native editor fixture. Actual TIG mouse/text events reopen
a draft, refund HP/dexterity, reject refunds below the original floors, spend on
strength, cancel without changing the file, edit name/portrait, preserve purchases
across Back, accept the replacement, reset points after a background change and
cancel that reset. The resulting saved envelopes agree byte-for-byte between
Linux, Windows/Wine and the whole-engine sanitizer build. The fixture uses
explicit synthetic profile/content keys for this portable-file comparison.

`tests/integration_bootstrap.py --mode runtime-characters --framebuffer` now
creates, edits and saves all four characters before submitting their final
recipes. Host creation and snapshot loading must reproduce those edited bytes.
Both host directions also retain invalid-build refusal/repair, owned controls,
chooser/observer dialogue and failure-freezing checks.

`tools/desktop_smoke.py --custom-characters --drafts --launch` drives four actual
game windows with OS input. It cancels an edit and checks the saved bytes are
unchanged, accepts a point reallocation and name/portrait change, switches to a
preset and reloads the saved build, leaves the lobby, restarts every process,
and loads each build using its real profile/content envelope. The launched
campaign must contain the revised strength value for each owned PC. The new
character controls, native editor, ready/chat roster and opening shared dialogue
are captured for visual review. On the host, the fixture also temporarily blocks
the canonical path with a directory, requires a reported save failure, restores
the preserved original file and reloads it before the successful edit. This is
exercised with both a Linux and a Windows/Wine host.

The passing evidence is `evidence/drafts-native-verified` (12 composed frames),
`evidence/drafts-party-verified/{linux-host,windows-host}` (40 composed frames)
and `evidence/drafts-desktop-{linux,windows}-verified` (42 OS captures). A preset
desktop regression adds eight captures in `evidence/drafts-preset-desktop-verified`.
All 102 frames were visually reviewed, including byte-identical comparisons against
previously reviewed frames recorded in
`evidence/drafts-verified-equivalent-review.json`. The earlier
`drafts-party-first` and `drafts-party-final` runs retain the stale editor-warning
HUD found during review; the verified run demonstrates the history-clearing fix.
Portable test/build logs are preserved in `evidence/drafts-checks`.

Earlier remaining-work entries are superseded by protocol 30 for starting
purchases and multiple saved builds/import/export. Still open:
editing fault cases during host loss, custom content/portrait variants, native
level-up authority, campaign save/recovery controls
and the complete campaign playthrough.

## Starting equipment

Equipment opens a local transaction for the selected custom character. The
native shop vendor and character are cloned into disposable previews, using
fixed stock and starting-inventory seeds while preserving the gameplay RNG.
Prices use native haggle/reaction rules, with native money, stock, item eligibility,
removal, carrying weight and inventory-room checks. Buy/Sell views, quantity,
Undo and Reset build at most sixteen ordered trades. Accept submits the recipe;
Back discards preview changes. Identity/point edits retain purchases when still
legal and clear them if the new build cannot replay them.

The host validates the full transaction before campaign creation, then replays
it on fresh shop/PC previews and transfers the resulting starting inventory and
gold to the owned PC. No client price, balance, native object or generated item
ID is trusted. Every player gets a separate copy of starting stock. Presets keep
their native loadout. Equipment editing is limited to the pre-campaign lobby;
campaign trade continues through its host-authorized service.

Protocol-30 acceptance is recorded in `STATUS.md`. `runtime-preparation` uses
real native mouse events for library export/remove/import/selection and equipment
cancel/accept, then launches four owned PCs and compares host-created inventory
against each preview using a canonical prototype/quantity/gold fingerprint. Its `--preparation-resume` continuation skips completed UI
work while retaining both host directions of movement/dialogue/final state.
A focused whole-engine sanitizer shop property also exercises ordinary sales,
ammunition split buy/sell, fixed stock/RNG preservation and invalid requests.
Editing a library selection or its equipment waits for the host's roster ACK.
