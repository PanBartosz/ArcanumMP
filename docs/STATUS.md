# Current status

Updated 2026-11-06. ArcanumMP implements four-player LAN co-op for the original
campaign, with Linux/Windows cross-play and modern single-/multiplayer UI tools.

| Item | State |
| --- | --- |
| Compatibility | Version 0.5.0, protocol 63; preserve older saves, profiles and verified content migrations |
| File checks | Exclude native screenshots and generated/personal files; normalize unused prototype-ID bytes, retaining actual gameplay checks and proven older save identities; [details](FILE_COMPATIBILITY.md) |
| Party controls | Shared key/story-item scope (map/8 tiles), companion dialogue/healing/EQ, gold transfer, load votes and visible periodic/pre-travel autosaves; [guide](PLAYER_GUIDE.md) |
| Latest gameplay build | Optimized Linux and Windows drop-ins; source revision recorded in each package |
| Combat feedback | Native status floaters, personal critical shake, automatic native turns with host radius and no pause popup |
| Spell audio | Personal live mute for repeating spells, elemental buffs, Tempus Fugit and familiar fidget; [scope and checks](QOL_BENCHMARKS.md#spell-loop-audio-controls-in-041) |
| Interface | Persistent Auto/Latest log controls, off-turn HP/fatigue inspection, scrolling-label cleanup, saved layouts/transparency and portrait targeting and [compact UI polish](UI_POLISH.md) |
| Playability | Shared log, searchable inventory/actions, previews, labels, text/controls, extra rows, quests, recipes and pings |
| Languages and loot | Personal English/Polish selection, presentation-only Eselter 1.7 support, translated added UI and Empty/Pusto labels; [setup and scoped verification](LOCALIZATION.md) |
| Polish interface | 123 additional translations for connection states, synchronization, errors, map notes and character development; older personal catalogs inherit new builtin entries |
| Windows dialogue | Canonical DAT paths normalized in 0.2.3; Polish Windows chooser/observer verified with Wine, existing language data retained |
| Polish setup | One executable imports the adjacent Eselter package without Python, backs up previous language data and selects Polish; Windows/Linux tools included in 0.2.2 |
| Campaign evidence | Connected original acquisition/travel through the Device ending; shared slides and native menu return |
| Windows executable runtime evidence | Wine |
| Promotional media | Revised showcase/trailer under local `dist/showcase-v2/` |
| QoL sweep | Compressed world transfers, NPC deltas, duplicate-pose smoothing, clipping/text fixes and optimized builds; [measurements](QOL_BENCHMARKS.md) |

The campaign evidence uses retained native checkpoints and strengthened combat
stats, with normal owner inputs steering automated routing. It establishes the
connected original script/item/travel chain, not one uninterrupted normal-party
playthrough, normal-party balance or every side quest. Manual multiplayer play
through much of the first area has also been reported without major issues.

Known limits: names/chat use printable ASCII; a running world does not migrate
automatically to a new host. The reported black client map after crafting remains under observation; targeted
native cave and Windows/Wine original-crafting checks have not reproduced it.

The 0.5.0 playthrough corrections include host-selected map/8-tile possession
sharing and companion services for all party members, preserving the physical
leader and actual item instances. Focused four-peer Linux/Windows-Wine checks,
a real periodic timer, original Virgil dialogue, retained older saves and
scrolling comparisons are recorded in [coordination evidence](COORDINATION.md#shared-party-access-and-playthrough-corrections-in-050).

Use [the active TODO](CAMPAIGN_TODO.md) for demonstrated remaining work,
[readiness evidence](FINAL_READINESS.md) for retained results/qualifications,
[features](FEATURES.md) for implementation mapping and [the player guide](PLAYER_GUIDE.md)
for controls. [Release preparation](RELEASING.md#local-artifacts) explains the local
artifact catalog; [showcase notes](SHOWCASE.md) record video provenance.

