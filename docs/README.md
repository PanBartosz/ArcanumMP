# Documentation

Start with the player or developer guides below. [STATUS.md](STATUS.md) describes
the current build; [CAMPAIGN_TODO.md](CAMPAIGN_TODO.md) is the only active backlog.

## Players

| Guide | Contents |
| --- | --- |
| [Installation](INSTALL.md) | Drop-in files, updating, content checks and launch |
| [Player guide](PLAYER_GUIDE.md) | Starting a party, controls, saving and recovery |
| [Personal languages](LOCALIZATION.md) | Eselter 1.7, Polish UI, mixed-language parties and presentation-pack import |
| [LAN](LAN.md) and [coordination](COORDINATION.md) | Discovery, chat, votes, pause, rest and ownership |
| [HUD and portraits](PROTOCOL58_FEATURES.md) | Moving/resizing panels, transparency, log toggle and targeting |
| [UI polish](UI_POLISH.md) | Compact cards, revised defaults, responsive action rows and before/after pictures |
| [QoL and performance](QOL_BENCHMARKS.md) | Protocol 59 improvements, before/after measurements and verification |
| [Playability tools](PROTOCOL57_FEATURES.md) | Logs, search, previews, action rows, quests, recipes and pings |
| [Display preferences](PROTOCOL56_FEATURES.md) | Zoom, widescreen, UI size, statuses and interaction speed |
| [Reconnection](RECONNECT.md) | Reserved owners, disconnects and durable recovery |
| [Showcase](SHOWCASE.md) | Trailer files, capture provenance and credits |

## Developers and maintainers

| Guide | Contents |
| --- | --- |
| [Contributing](../CONTRIBUTING.md) | Changes, useful bug reports and scoped validation |
| [Building](BUILDING.md) | Linux, Windows cross-build, asset-free tests and private runtimes |
| [Architecture](ARCHITECTURE.md) and [runtime](RUNTIME.md) | Authority, engine adapters and lifecycle |
| [Wire protocol](PROTOCOL.md) | Messages, bounds, admission and compatibility |
| [Feature inventory](FEATURES.md) | Implementations mapped to retained evidence |
| [Readiness evidence](FINAL_READINESS.md) | Campaign connections, checkpoints and limits |
| [Scope](OBJECTIVE.md) and [workflow](WORKFLOW.md) | Required behavior and development priorities |
| [Release preparation](RELEASING.md) | Drop-in archives, notices, provenance and publication |
| [Maintenance](MAINTENANCE.md) | What belongs in Git and how local work is preserved |
| [Notices](../NOTICE.md) and [license](../LICENSE.md) | Upstream and dependency attribution |

## Technical references

- Characters: [creation](CHARACTERS.md), [drafts](DRAFTS.md),
  [progression](PROGRESSION.md), [training](TRAINING.md).
- Actions: [combat](COMBAT.md), [weapons](WEAPONS.md), [magic](MAGIC.md),
  [skills](SKILLS.md), [item use](USE.md), [items](ITEMS.md), [loot](LOOT.md),
  [commerce](COMMERCE.md).
- Campaign: [dialogue](DIALOGUE.md), [continuity](CONTINUITY.md),
  [scripts/story](STORY.md), [persistence](CAMPAIGN.md), [gameplay](GAMEPLAY.md),
  [documents](DOCUMENTS.md), [time](TIME.md), [encounters](ENCOUNTERS.md).
- World: [replication](WORLD.md), [sectors](SECTORS.md),
  [environment](ENVIRONMENT.md), [travel](TRAVEL.md), [maps](CARTOGRAPHY.md).
- Presentation: [audio](AUDIO.md), [movies/slides](MEDIA.md),
  [HUD art sources](../assets/ui/README.md).

Many technical guides retain their feature's introduction protocol and scoped
verification notes. Those numbers are historical, not the version to install;
current compatibility is recorded in STATUS.md. Coverage suggestions in old
notes are not automatically release requirements.
