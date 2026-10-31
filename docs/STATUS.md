# Current status

Updated 2026-10-31. ArcanumMP implements four-player LAN co-op for the original
campaign, with Linux/Windows cross-play and modern single-/multiplayer UI tools.

| Item | State |
| --- | --- |
| Compatibility | Version 0.2.0, protocol 60; update all players together; existing campaign saves/settings remain readable |
| Latest gameplay build | Optimized Linux and Windows drop-ins; source revision recorded in each package |
| Interface | Saved layouts/transparency, themed art, portrait targeting and [compact UI polish](UI_POLISH.md) |
| Playability | Shared log, searchable inventory/actions, previews, labels, text/controls, extra rows, quests, recipes and pings |
| Languages and loot | Personal English/Polish selection, presentation-only Eselter 1.7 support, translated added UI and Empty/Pusto labels; [setup and scoped verification](LOCALIZATION.md) |
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
automatically to a new host. No known campaign blockers are currently listed.

Use [the active TODO](CAMPAIGN_TODO.md) for demonstrated remaining work,
[readiness evidence](FINAL_READINESS.md) for retained results/qualifications,
[features](FEATURES.md) for implementation mapping and [the player guide](PLAYER_GUIDE.md)
for controls. [Release preparation](RELEASING.md#local-artifacts) explains the local
artifact catalog; [showcase notes](SHOWCASE.md) record video provenance.
