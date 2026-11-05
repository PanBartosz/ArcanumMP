# Campaign feature inventory — 2026-10-29

The planned families below have source implementations and retained scoped
runtime evidence. This is an implementation inventory, not proof that every
combination is polished. [CAMPAIGN_TODO.md](CAMPAIGN_TODO.md) owns the active
backlog; technical guides retain detailed controls and evidence qualifications.

| Required capability | Implementation / guide | Retained evidence and scope |
| --- | --- | --- |
| Linux/Windows cross-play | Authoritative TCP session and bounded world transfer; [session.c](../src/coop/session.c), [protocol](PROTOCOL.md) | Mixed four-engine sessions pass with both hosts; Windows runs are under Wine |
| Main-menu lobby | Host/join/discovery, creation/import/library/equipment, chat, readiness, content checks, removal; [lobby_ui.c](../src/coop/lobby_ui.c), [LAN](LAN.md), [characters](CHARACTERS.md) | Saved ownership/names and healthy/wounded roster have both-host evidence; disabled-button activation/keyboard traversal are corrected; native Host/Cancel/Host, chat wrapping and anchored scrollback pass; see STATUS for release limits |
| Independent human actions | Stable owner validation, movement/halt and native interaction planning; [gameplay.c](../src/coop/gameplay.c), [gameplay](GAMEPLAY.md) | Four-owner actions pass in native checks and the connected main quest; separated-play scope remains in the readiness record |
| World, sectors and presentation | Shared object/script lifecycle, all-owner sector retention, roofs/light/audio; [world](WORLD.md), [sectors](SECTORS.md), [environment](ENVIRONMENT.md), [audio](AUDIO.md) | Native lifecycle/region cases and connected main-quest transitions pass; protocol55 clears the dense-area reload failure |
| Combat and weapons | RT/TB/fast timing, AP, targeting, called shots, XP/death and cooperative allegiance; [combat](COMBAT.md), [weapons](WEAPONS.md) | Native combat and original encounters pass; weak-party bot deaths do not by themselves show a product defect |
| Spells and technologies | Owned casting/item effects, maintained spells, resurrection, Teleport choice, personal feedback; [magic](MAGIC.md), [use](USE.md) | Both-host native casting/use and saved destination/map transfer pass; retain exact branch limits |
| Skills and devices | Owned skill/theft/assistance, traps/bombs and persistent damage attribution; [skills](SKILLS.md) | Native placement, saved ownership/timers and earlier damage-credit continuations pass; latest tools continuation does not detonate devices |
| Inventory, loot and consumption | Native equip/grid/drop/gift/shared containers, partial quantities and callback-safe lifecycle; [items](ITEMS.md), [loot](LOOT.md) | Both-host native controls/commerce pass; arbitrary callback permutations are additional coverage, not separate features |
| Quickbars and Fate | Personal persistent bindings, authoritative item activation and Fate; [items](ITEMS.md) | Native item drag/recent controls, activation and Save/Load pass; connected continuation retains personal bindings |
| Dialogue and NPC services | Initiator choices/observers, training, barter, identification, repair and crafting; [dialogue](DIALOGUE.md), [training](TRAINING.md), [commerce](COMMERCE.md) | All-four-owner service matrix and all seven restored panel types pass; original main-quest conversations/acquisition now connect |
| Scripted dialogue continuity | Saved page/response, queued callbacks and ordered arrivals; [dialogue](DIALOGUE.md), [campaign issues](CAMPAIGN_TODO.md) | Both-host recovery preserves choices, queues and journals; conversations and hints survive rest/controller handoff; in-flight hint capture retains undelivered messages across reconnect |
| Journals, social state and advancement | Shared rewards/quests/social policy, conversation/document history and owned point spending; [story](STORY.md), [progression](PROGRESSION.md), [documents](DOCUMENTS.md) | Native controls, earned point spending and original main-quest rewards/progression pass through story22 and the ending |
| Followers and party AI | Owned orders/inventory/assistance, cross-human healing/defense, nested strength and idle companion yield; [continuity](CONTINUITY.md) | Native orders/healing and controlled allegiance/AI checks pass; strengthened main-quest run does not establish normal-party balance |
| Maps and travel | Exploration/notes/quest pins, original exits, journey consent and caster-owned Teleport; [cartography](CARTOGRAPHY.md), [travel](TRAVEL.md) | Both-host notes/conflicts/Teleport persistence pass; original acquisition, world voyages and native passages now connect to the Device ending |
| Rest, clocks and options | Native combat timing, shared rest/Stop, native time, host difficulty/timing and per-human preferences; [coordination](COORDINATION.md), [time](TIME.md) | Native preferences/save and rest/Stop recovery pass; rest interrupts for retained story dialogue/travel in both-host native controls flow |
| Campaign saves | Atomic all-owner archives, 63 named slots, F12 load votes, configurable host periodic/pre-travel autosaves and direct latest selection, native cache staging; [campaign](CAMPAIGN.md), [coordination](COORDINATION.md) | Native Save/Load, cache/case controls and sparse path restoration pass; wider interrupted persistence remains a practical risk |
| Party gold | F12 recipient/amount picker, authoritative wallet transfer, proximity/funds/overflow validation | Mixed Linux/Wine party balances replicate and survive named/automatic loads |
| Disconnect and host loss | Reserved owners, frozen authority, retry/resync, acknowledged terminal failure and durable recovery; [reconnect](RECONNECT.md) | Both-host native recovery and host/guest failure propagation with preserved checkpoints and usable exit pass; ten-minute separated play completed seven recovery stages, with one fixture movement-threshold failure retained |
| Shared media and endings | Movies/slides/fades, skip votes, native continuation and menu return; [media](MEDIA.md) | Connected original Device ending completes five synchronized slides and native menu return on all four engines; prepared peaceful/combat/alliance checks are reused |
| Documentation and delivery | Reproducible builds, runtime preparation, controls, architecture and license notices; [installation](INSTALL.md), [architecture](ARCHITECTURE.md) | Matched Linux/Windows drop-ins and required libraries; guides record connected main-quest evidence and limits; installers and real-Windows testing are excluded |
| Modern playability tools | Shared combat log, inventory search/comparisons/junk sales, labels/previews, text settings, automatic native turns/radius, status floaters, personal critical shake, bindings/extra actions, personal quests, crafting help and pings; [controls](PROTOCOL57_FEATURES.md) | Native panel/save/load and commerce/crafting checks; four-engine TCP combat, identical event histories, all-owner world/map pings and host turn-trigger policy; Windows guests use Wine |
| Custom HUD layout and presentation | Saved move/resize/visibility and per-panel opacity; visible log toggle; themed panels, portrait targeting and native bubble colors; [controls](PROTOCOL58_FEATURES.md) | Native mouse events, fresh-process layout restore at800×600, alpha/bubble pixel checks, spell/skill/item routes, off-turn guard and all-owner TCP attack paths; scoped before/after render timings |

The source audit checked runtime APIs, native authority gates, campaign save/load
hooks and snapshot admission for the added sidecars. It did not prove every
engine branch. Original main-quest acquisition, travel and final presentation now
connect using retained checkpoints. Record future reproducible playthrough bugs
in the campaign TODO; scoped coverage limits are not new feature requirements.

Scoped evidence remains reusable after unrelated changes. Current source uses
protocol58, with [HUD layout, targeting and rendering corrections](PROTOCOL58_FEATURES.md),
[combat log and modern playability tools](PROTOCOL57_FEATURES.md),
[zoom, widescreen, UI sizing, status badges and interaction preferences](PROTOCOL56_FEATURES.md),
[dense-area and experience-gauge fixes](PROTOCOL55_FIXES.md),
[party portraits, turn order, immortality and UI fixes](PROTOCOL54_FEATURES.md),
and retains the [six protocol53 player-reported corrections](PROTOCOL53_FIXES.md).
World and local map routes preserve their plotted waypoints.
The latest linked campaign evidence is
`evidence/large-scene-p55/connected-ending.json` (original acquired Device and
connected story22 ending, strengthened combat stats); protocol53 player-report evidence
is `evidence/player-reports-p53/acceptance.json`. Local route control evidence is
`evidence/local-routes-v52/acceptance.json`. Version49 replaces
the unfinished protocol48 attempt with acknowledged terminal failure delivery.
Consult [current evidence](FINAL_READINESS.md)
for accepted prefixes, recovery summaries and exact limits. Broader permutations
listed in older guides are coverage opportunities, not automatically release-blocking
features. A concrete failure in a required capability remains a blocker.
