# Contributing to ArcanumMP

Read the [current status](docs/STATUS.md), [active TODO](docs/CAMPAIGN_TODO.md),
[build instructions](docs/BUILDING.md) and [workflow](docs/WORKFLOW.md) first.
Keep changes focused on requested player behavior or a reproducible defect.
Record new demonstrated bugs in the existing TODO; avoid parallel progress lists.

## Working on a change

1. Make related corrections together and keep unrelated formatting or generated
   artifacts out of the change. Preserve upstream notices and the license.
2. Build the affected targets. Run the asset-free tests for session/state changes
   and relevant native checks when engine behavior changes.
3. Reuse unaffected evidence. A documentation or capture edit does not require
   another full multiplayer campaign run.
4. Explain the resulting behavior, validation and material limits in the PR.
   Include save/protocol implications when changing persistence or wire formats.

The root CI workflow checks documentation/source hygiene and the portable CTest
suite on Linux. Native integration runs need locally owned game data and private
runtimes; no original game data belongs in Git or CI artifacts. Record whether
Windows executables were run through Wine. Installers, Windows VM work and a
real-Windows acceptance gate are outside this project's agreed scope.

## Useful bug reports

Include the exact build/protocol, operating systems, host/client role, party
size and combat mode. Describe steps, expected behavior, actual behavior and
whether other players see the same issue. Add the campaign location/quest and
the most recent working save when relevant. Screenshots, logs and a minimal
reproduction are more useful than another long automated run without a cause.

Keep player identity files and personal credentials private. Do not attach
retail DAT archives or other original game data. If you prepare a diagnostic
state, describe altered stats, supplied items, moved actors or script changes.
Distinguish a staging/controller error from a demonstrated gameplay defect.

## Repository layout

- `src/coop/`: multiplayer services, native adapters, HUD and engine probes.
- `upstream/arcanum-ce/`: imported engine with this fork's integration changes.
- `tests/`: portable tests and opt-in native integration tools.
- `assets/ui/`: prepared HUD bitmaps, preserved source images and prompts.
- `tools/`: builds, runtimes, packaging, asset preparation and video production.
- `docs/`: current guides; `docs/history/` retains historical findings.

Builds, runtimes, saves, evidence and release/video files stay local. See
[maintenance](docs/MAINTENANCE.md) before deleting local artifacts and
[release preparation](docs/RELEASING.md) before distributing a build.
