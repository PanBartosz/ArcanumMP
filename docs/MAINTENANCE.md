# Repository maintenance

## What belongs in Git

Keep source, tests, CMake/tooling, player/developer documentation, notices and
project-owned HUD assets. Preserve the original generated PNGs, prepared BMPs
and prompts: the source art is useful for later revisions even though only BMPs
are embedded in the game.

Original game data, executables/build caches, private profiles, identities,
saves, diagnostic captures and release/video archives are ignored. The source
repository can be checked without any of those local directories:

```sh
python3 tools/check_repository.py
```

This checks tracked file paths/sizes, common credential patterns, portable public
entry points and local Markdown file links. It does not treat a link to an
ignored local file as a publishable documentation link, or a snapshot as a
new active task list. It checks file targets, not historical heading fragments.

## Preserve useful local work

| Directory | Retention policy |
| --- | --- |
| `build/` | Keep active game/ASan/Windows builds, pinned dependencies and referenced baselines |
| `runtime/` | Preserve private profiles, identity/configuration, saves, native caches and isolated diagnostic states |
| `evidence/` | Preserve accepted/failed diagnostics, native frames, logs, recovery checkpoints and capture sources |
| `dist/` | Keep current drop-ins, referenced baselines and current promotional media easy to find |
| `dist/archive/` | Retain older release archives/checksums for regression and rollback |
| `docs/history/` | Preserve milestone findings, completed work and historical constraints |

Do not run `git clean -fdx`: ignored files include valuable campaign work and
private saves. Do not delete a runtime, checkpoint or old executable merely
because it is not tracked. Follow symlinks and active process references before
removing a build or extracted package.

An extracted release staging tree can be removed when its complete file set
and every file's bytes match a retained, readable archive. Preserve the archive
and checksum and record how to restore it. Unexpected or changed files mean the
tree must be kept for review. Do not substitute a metadata-only inventory for
an actual copy of a unique save, diagnostic or source file.

## Housekeeping on 2026-10-30

- Kept all campaign evidence/checkpoints, private runtimes, current builds,
  current drop-ins, videos, source artwork and regression baselines.
- Removed 52 extracted release trees only after checking their complete
  contents byte-for-byte against the retained archives.
- Moved 106 older archive/checksum files to `dist/archive/`.
- Removed a retired tooling download/cache after privately archiving and
  verifying its scripts and metadata. That archive is local and ignored.
- Reclaimed about 10.1 GiB. Detailed paths, checksums and retention decisions
  are in `evidence/housekeeping-2026-10-30/cleanup.json`.

Retired installer sources/fixtures and exhausted delivery plans are preserved
under history rather than offered as current tools. The sole active backlog is
[CAMPAIGN_TODO.md](CAMPAIGN_TODO.md); current build state is in [STATUS.md](STATUS.md).

Validation used a fresh source-only copy without Git metadata, game data or local
builds: configuration/build and all 30 portable tests passed. The source check
validated 414 local Markdown links. All four retained pre-ending recovery saves
still match their recorded SHA-256. Logs and the preservation audit remain in
`evidence/housekeeping-2026-10-30/`.

## Publication boundary

The clean checkout contains about 86 MB of source/assets before this cleanup's
small documentation additions. It does not require the much larger local
working directories. No source file/history object larger than 4 MB was found
in the initial inventory; the largest are preserved UI source images. No retail
archive, movie/audio file, game executable or credential-file path was found
in the tracked history inventory. The targeted current-file credential scan
reported no matches. Git history is retained; no history rewrite is needed for
the identified cleanup work.
