# Changelog

Published player releases and earlier engineering milestones are listed below.
All players should use matching executables and game data. Current compatibility is
**protocol 62**.

## Unreleased

### v0.4.0 — combat feedback and corrected party tools

- Show native, locally translated status floaters above PCs and NPCs as effects
  apply/end, including poison, fatigue and encumbrance outside combat.
- Replace automatic custom pauses with native turn-based combat. The host
  chooses triggers and a 5–40-tile radius from a player character. These events
  neither freeze the session nor open F12. Pause changes combat timing.
- Add a personal, persistent switch for brief world-only screen shake when the
  local character receives a damaging critical hit. UI placement stays fixed.
- Save before map/world departures. The departure snapshot omits its pending
  transition, so loading returns to the old map without repeating the trip.
- Add F12 Autosaves and visible save-browser shortcuts; select the newest
  automatic save directly while preserving the ordinary unanimous load vote.
- Theme the lobby from the main menu. Separate campaign actions from the chat
  heading, fix overlapping hidden hit areas, and size/clip log chrome independently
  of 150% body text. Existing layouts and preferences remain usable.
- Polish translations for all new controls and feedback; update every player
  together. Existing saves, profiles and language packs are retained.

### v0.3.0 — party gold, autosave and in-session loading

- Add F12 → Give gold with a recipient picker, exact amount and 25/100/All.
  Host validation preserves ownership, proximity, funds and overflow checks.
- Add host-configurable periodic saves (Off, 2/5/10/15/30 minutes) and saving
  after map/world travel. Three slots rotate; automatic capture no longer
  reloads clients. Load them through the host catalog and a unanimous vote.
- Enable F12 → Load campaign without requiring a quick save; named and automatic
  slots remain available during the campaign. A refusal preserves current play.
- Preserve native hover descriptions during passive interface refreshes; avoid
  refreshing unchanged combat cursor state on every world update.
- Fully invalidate resized world buffers after native inventory/schematic views,
  including zoomed rendering. Keep the original crafting path in the focused check.
- Move native screenshots from F12 to Print Screen, avoiding a conflict with
  the party panel. Exclude native screenshots from file identity. Normalize only unused bytes of
  validated prototype ObjectIDs and zero-initialize new native identifiers.
  Retain verified older save identities; real gameplay changes remain checked.
- Translate the new controls and feedback into Polish. Update all participants
  together; campaign saves, player profiles and language packs remain in place.

### v0.2.4 — Polish multiplayer messages and reliable file checks

- Add 123 Polish translations for connection states, refusals, reconnection,
  synchronization errors, map notes, character development and remaining UI
  labels. Translate feedback callbacks and shared-story overlays; older personal
  UI catalogs inherit missing builtin entries. Keep player chat/name input raw.
- Ignore settings, logs, backups, temporary files and native generated map/tile
  caches inside game-data repositories. Replacing drop-ins or changing personal
  files does not create a false game-data mismatch.
- Preserve older saves, named slots and saved/exported characters when the old
  manifest proves the retained game data is identical. Gameplay differences,
  ownership and corruption still block incompatible loads/connections.
- File-mismatch feedback points to the two manifests for diagnosis. Verified
  with four mixed-language Linux/Windows-Wine lobby engines; protocol remains 60.
  [File checks and troubleshooting](docs/FILE_COMPATIBILITY.md).

### v0.2.3 — Polish dialogue on Windows

- Normalize resource-path separators before Windows DAT archive lookups. Polish
  dialogue now resolves on clients where the translated interface and films
  already worked but conversations fell back to English.
- The native dialogue check now compares canonical/native resource reads and
  rejects an untranslated Polish retail page. Verified with Polish Windows/Wine
  chooser and observer, Polish Linux host and an English Linux observer.
- Existing language packs, saves and preferences stay usable. No translation
  re-import is required; compatibility remains protocol 60.

### v0.2.2 — Polish setup without Python

- Self-contained Windows/Linux tools import Eselter 1.7 from the adjacent
  download, verify its checksum and select Polish for the next launch.
- Existing language data is backed up; saves, identities and shared gameplay
  data remain unchanged. The Windows tool handles component paths and Unicode.
- Included beside the game in drop-ins and available as separate small downloads.
- Game engine binaries are unchanged from 0.2.1; compatibility remains protocol 60.

### v0.2.1 — translation text lifetime

- Keep cached translated lines valid during long sessions, including when a UI
  draw uses several translated strings together. Adds a regression beyond the
  former 8,192-line cache boundary.

### v0.2.0 — personal languages and empty loot

- Independent language selection in the original options and playability
  settings. English and Polish players can share the same campaign.
- Presentation-only support for Eselter's Polish translation 1.7, including
  original-game text, fonts, interface art and available media; third-party
  language assets are imported separately.
- Polish text for the added menus, tools, help, conditions and combat feedback.
- Dialogue resource references survive transport, transcripts and recovery;
  native service prices retain their authoritative amounts. Older saves remain
  readable and language choice stays outside the gameplay fingerprint.
- **Empty / Pusto** hover and interaction labels on looted bodies and containers,
  updated as items are removed or put back.
- Update all players together. [Language setup and verification](docs/LOCALIZATION.md).

### v0.1.0 — first public release

- Four-player campaign co-op with Windows/Linux cross-play and shared dialogue,
  travel, saves, combat, spells, skills and loot.
- Customizable HUD, combat history, portrait targeting, zoom/pan, inventory/action
  search, quest notes, crafting help and display/control preferences.
- Polished interface, fresh gameplay trailer/showcase and a GitHub Pages website.
- Earlier protocol entries below are engineering milestones, not public releases.

### UI polish — protocol 59

- Compact, measured combat/hover cards with clear headings and native colors.
- Roster-sized turn strip, refined party portraits and recessed health/fatigue bars.
- Narrower combat log with wrapped entries and click-through to full details.
- Responsive extra action rows, quieter empty slots and retained native icons.
- Compact window controls, softer selection, improved spacing and objective sizing.
- Saved layouts remain readable; reset HUD positions to adopt the new defaults.
- [Visual comparisons and scoped checks](docs/UI_POLISH.md).

### Protocol 59 — QoL and performance

- Compressed shared-world transfers with bounded decompression, canonical CRCs,
  unchanged application/ACK barriers and raw fallback for incompressible data.
- Changed-only NPC poses, periodic refresh, per-client retries/fairness and
  player-pose priority over cosmetic traffic.
- Duplicate samples no longer restart movement blends; unchanged pixels avoid
  redraw work. Unchanged zoomed views retain their composed world image.
- Correct partial-window redraws and screen-edge/fractional-scale clipping.
- Consistent wrapped/scaled text measurement, content-sized hover panels and
  shortcut hints that follow saved bindings.
- Optimized Linux/Windows builds retain debug symbols and assertions.
- [Paired benchmarks and scoped verification](docs/QOL_BENCHMARKS.md).

### Protocol 58 — HUD layout and presentation

- Saved panel placement, resizing, visibility and individual transparency.
- Visible combat-log toggle and HUD editing controls.
- Attacks and targeted abilities through turn-order portraits.
- Native speech colors and reduced overlay/rendering overhead.
- Embedded themed frames, controls, turn cards and condition badges;
  assigned actions retain original spell, skill and item icons.
- Gameplay showcase and trailer, with a revised smooth camera/spell edit.
- Repository/documentation cleanup for future GitHub publication.

### Protocol 57 — playability tools

- Shared combat log with details, filters and export.
- Inventory/action search, equipment comparison and marked junk sales.
- Interaction labels and combat previews.
- Independent text/contrast settings, auto-pause and command remapping.
- Extra action rows, personal quest tracking/notes and crafting help.
- Temporary multiplayer world/map pings.

### Protocol 56 — display and party feedback

- Relationship circle colors, mouse-wheel zoom and widescreen options.
- UI size, character conditions on portraits and walk/run interaction settings.

### Protocols 53–55 — gameplay corrections and campaign connections

- Cross-owner real-time attacks, NPC animation, bubbles, cursor and dialogue fixes.
- Clickable party portraits, turn order, hover help, debugging immortality and
  middle-button panning.
- Dense-area replication and experience-gauge bounds fixes, and a poison/travel
  crash correction.
- Connected original acquisition/travel through the Device ending with shared
  ending presentation and native menu return.

See the [feature inventory](docs/FEATURES.md),
[readiness qualifications](docs/FINAL_READINESS.md), detailed
[protocol guides](docs/README.md).
