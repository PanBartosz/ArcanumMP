# Development workflow

Keep related player-visible changes together. Preserve license notices and game
data boundaries. Build the affected targets and run checks appropriate to what
changed; use the portable suite for session/state changes and scoped native
checks for engine changes.

Use private writable game runtimes. Do not modify a user's original installation,
rebuild an executable while it is running, commit personal identities/saves, or
include proprietary game data in Git or CI artifacts. Preserve useful diagnostics
and checkpoints when investigating a report.

PRs should explain the resulting behavior, how it was checked, and any relevant
compatibility or save implications. See [contributing](../CONTRIBUTING.md),
[building](BUILDING.md) and [release preparation](RELEASING.md).
