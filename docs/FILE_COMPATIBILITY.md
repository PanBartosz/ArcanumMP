# Multiplayer file compatibility

Close the game and replace the supplied executables/libraries to update it.
Keep your saves, player profile, character drafts, settings and language packs.
Everyone should use the same ArcanumMP release and gameplay patches. Reinstalling
Arcanum is not required for an ordinary update.

## What must match

The multiplayer check compares SHA-256 hashes of archive/loose game data in a
canonical, case-insensitive path order. Scripts, rules, prototypes, campaign
maps, dialogue scripts and patches remain checked. The identity depends on file
contents, not installation paths, timestamps, operating system or the drop-in
executable's filename. The network protocol separately checks engine compatibility.

Personal settings, saves, profiles, language folders, audio/video and `tigcache`
are excluded. Version 0.2.4 also excludes configuration files, logs, backups,
temporary/hidden files and OS metadata inside `data/` and `modules/`. It excludes
the native generated `art/tile/tilevariant.dat` and map preprocessing files
directly inside a repository's `maps/` folder. Actual map resources in each map's
subdirectory remain checked.

Version 0.3.0 additionally excludes native `screenNNNN.bmp` screenshots at
the repository root (`data/` or `modules/<module>/`). Images inside art or map
subdirectories remain checked. A supplied player manifest contained three such
screenshots that were absent from the comparison installation, while all game
archives matched. Taking a screenshot previously changed the campaign identity. Native screenshot
polling also still used F12, conflicting with the party panel. It now uses Print
Screen, so normal party-panel visits no longer create image files.

Old native prototype generation also left uninitialized bytes inside serialized
`ObjectID` headers. For a validated version-0x77 prototype header, 0.3.0 normalizes
only the padding and unused union payload of its blocked/integer identifiers
before hashing. The prototype number, type and every gameplay field remain
checked. New native identifier construction zero-initializes those bytes.
Prototype SHA-256 entries therefore describe canonical meaningful data rather
than the raw file bytes. The supplied manifests also differed in 307 equal-sized
prototype hashes; manifests alone cannot establish that every difference was
padding, and genuine prototype differences still require investigation.

## Keeping older saves

On the first 0.3.0 launch the cache is refreshed once. The game compares the
previous `arcmp-content.json` with the newly verified manifest after excluding
the files above. For older prototype hashes, the service also compares the old manifest with
the freshly hashed raw files. Only a proven match of every retained file lets it remember
the retired identity in `arcmp-content.compat`. This lets older campaign saves,
named save slots, saved characters and character exports remain readable.
Nothing is rewritten or deleted from the original saves.

Keep that compatibility file with the installation. These local aliases never
relax network admission: participants still need identical current gameplay
fingerprints. Real changes to scripts, maps, prototypes or patches invalidate
the migration. Ownership and save-corruption checks remain active.

## Investigating a remaining mismatch

1. Check that everyone updated the executable and uses the same gameplay patches.
2. Keep `arcmp-content.json` from each game folder. Each entry gives the relative
   filename, SHA-256 and size; compare missing/extra paths and differing hashes.
   These manifests contain no save payloads or chat.
3. Share both manifests with a bug report so the differing resources can be
   identified. Keep saves, `arcmp-player.id` and character files in place.

Ordinary resource additions, removals and changes trigger a background rehash.
If an external tool changes bytes while preserving both size and modification
time, close the game, remove only `arcmp-content.cache`, then restart. Do not
delete the previous manifest or `arcmp-content.compat` during an upgrade.

## Scoped verification

The production fingerprint service is checked on Linux and Windows through
Wine. Cases cover changed launch-time settings/logs/caches, backup files,
identical recopies, old cache migration, preserved retired identities and
rejection of real prototype/rule/script/map/patch changes. Additional cases
verify native screenshots, padding-only prototype regeneration, migration from
raw prototype hashes, and continued rejection of changed prototype IDs/fields
and image resources. Portable tests also
load an older checkpoint/character through a verified alias and locate its named
save slot, while retaining ownership checks.

A native four-player LAN lobby uses two Linux and two Windows/Wine engines,
Polish and English interfaces, and deliberately different personal files inside
their game-data folders. The old service gives them four different identities;
the corrected service gives them one identity and all four join, choose
characters and become ready. No campaign route replay is part of this check.
