# Product evidence and limits

Updated 2026-10-30. The development product has protocol59 Linux and
Windows drop-ins and implemented core multiplayer features. Original acquisition
and travel now connect from the opening to the peaceful Device ending using
retained checkpoints. All four engines complete shared presentation and native
menu return. [CAMPAIGN_TODO.md](CAMPAIGN_TODO.md) owns the running campaign task
list; this page records reusable evidence, not a second backlog.

| Evidence | What it establishes |
| --- | --- |
| `evidence/qol-2026-10-30/acceptance.json` | Paired scene/network benchmarks, pixel checks at 800×600/1280×720/1920×1080, whole-engine sanitized UI checks, 30 portable tests, delayed/rate-limited four-engine play and 24-step mixed Linux/Wine combat with both host directions; [methods and results](QOL_BENCHMARKS.md). Existing campaign evidence is reused |
| User manual first-area playtest | Significant first-area play without major issues found; later areas not covered |
| `evidence/ui-art/acceptance.json` | Dedicated21-component art pass (23 embedded images total): native decode/draw of every component at small HUD sizes, live condition badges/cards and native spell/skill/item slot icons; existing layout/opacity/targeting controls checked with the new art. Protocol58 network/campaign/performance evidence is reused |
| `evidence/ui-p58/acceptance.json` | Saved HUD geometry/visibility and per-element opacity, actual drag/resize events, fresh-process800×600 restore, themed panels at100%/150% text, layered-alpha and speech-color pixels, native ability portrait routing/off-turn rejection; four-engine TCP combat with all-owner portrait-path attacks, shared events/pings/policy and no sanitizer failures. Scoped paired render measurements use matching fresh runtimes; unchanged campaign evidence is reused |
| `evidence/modern-p57/acceptance.json` | All ten playability families implemented; native six-tab UI at100%/150% text, native damage/heal, junk sale/crafting, bindings, labels, auto-pause and save/load; four-engine TCP combat and identical shared events/pings/policy on Linux, ASan/UBSan Linux and two Windows/Wine engines. Final shortcut/modal-close changes have separate native UI checks; unaffected campaign evidence is reused |
| `evidence/player-reports-p53/acceptance.json` | Six reported defects corrected; four-owner sheathed attacks, original dialogue, cursor, running, bubbles and NPC visual checks |
| `evidence/party-ui-p54/acceptance.json` | Combined31-stage party UI/debugging check, Linux host, ASan/UBSan guest and two Windows guests under Wine |
| `evidence/display-p56/acceptance.json` | Protocol56 relationship circles, native walk/run interaction goals, settings callbacks/restart, world-only zoom and portrait statuses; combined four-engine flows at800×600 and1280×720, including two Windows guests under Wine |
| `evidence/middle-pan-p54/native.json` | Native camera/map panning, bounds, release, focus/UI exclusion and off-turn use; no new Wine runtime claim |
| `evidence/delivery-4/acceptance.json`, `evidence/delivery-5/acceptance.json` | Connected original early campaign through Bates/BMC pillar, four-owner save/load |
| `evidence/campaign-return-v50/acceptance.json` | Original BMC return, Isle voyage, Thorvald's revelation/spectacles and return |
| `evidence/waypoint-travel-v51/acceptance.json` | Plotted world routes and original Wheel Clan/Loghaire/story8 connection |
| `evidence/stillwater-connection-v52/acceptance.json` | Connected original Stillwater/Qintarra storyline through story10 |
| `evidence/tsenang-connection-v52/acceptance.json` | Connected newspaper/archive research through story11; library attempt failed, not a whole-run pass |
| `evidence/campaign-investigation-p54/acceptance.json` | Connected Gorgoth/Caladon/Roseborough original acquisition/travel and all-owner story14 Save/Load; passing152–165 tail on Linux, ASan/UBSan and two Windows/Wine engines |
| `evidence/campaign-investigation-p54/poison.json` | Hidden/visible native poison checks under ASan/UBSan for the travel crash correction; resumed journey clears the original crash point |
| `evidence/tsenang-return-p54/tsenang-prefix.json` | Connected original Gorgoth/Tsen-Ang approach, guards and platform passages, M’in Gorad/story15 and Qintarra return/story16; four-owner save/load at both milestones |
| `evidence/tsenang-return-p54/panarii-acceptance.json` | Original Alexander/Hadrian prerequisites, grate/sewers/password/tomb route, skull/lid and Hadrian/Gunther revelations through story17; four-owner save/load; strengthens combat stats, supplies no quest items/flags/positions |
| `evidence/thanatos-connection-p54/nasrudin-prefix.json` | Original Pete combat permission, helm voyage to Thanatos, journey to Nasrudin and story18/Device assignment; all four reach the shared Save; return/Wastes suffix remains separate |
| `evidence/tulla-device-connection-p54/ashbury-return.json`, `wastes-gateway.json`, `tulla-acquisition.json` | Original return voyage, Wastes journey, Weldo, Tulla prerequisites, mural/reread, medallion/Verses/runes and Pelojian's revelation; four-owner shared Save |
| `evidence/large-scene-p55/acceptance.json` | Dense native area exceeds the former32,768 instance cap; protocol55 clears the shared reload and continues replication on Linux, ASan/UBSan and two Windows/Wine engines |
| `evidence/large-scene-p55/experience-gauge.json` | Original native Vendigroth transition save has100% rounded experience on all four characters; corrected gauge bounds clear the native-load sanitizer failure and all four continue to the third floor |
| `evidence/large-scene-p55/device-acquisition.json` | Original Vendigroth dungeon, Device item5616/INSERT1094/story19, all original exits and four-owner shared Save on the surface |
| `evidence/large-scene-p55/roseborough-return.json`, `void-handoff.json` | Original paid return voyage, Roseborough/Brodgar approach, Nasrudin banishment and four-owner Save/Load at story20 in the Void |
| `evidence/large-scene-p55/void-castle.json` | Original Arronax revelation/quest1057, Void guard permission, castle entrance/internal passages and four-owner Save/Load before the final chamber; normal owner movement steered controller routing mistakes |
| `evidence/large-scene-p55/connected-ending.json`, `arronax-to-ending/linux-host/result.json` | Passing274–283 continuation from the connected Device state: original Kerghan argument/farewell, five synchronized slides, all four ready/done, terminal0, native menu and cleared party; four identical ending reports and CRC-valid pre-ending saves; Linux, ASan/UBSan and two Windows/Wine engines |
| `evidence/local-routes-v52/acceptance.json` | Native multi-point local movement, owner walk/run and cancellation/validation |
| `evidence/forest-checks`, `dark-checks`, `legacy-checks`, `alternate-checks`, `evil-checks` | Controlled original later chapters and peaceful/combat/alliance endings |
| `evidence/native-core-persistence-v47.json`, `session-lifecycle-v47.json`, `terminal-recovery-v49.json`, `story-continuity-v50.json`, `sparse-party-v50/acceptance.json` | Scoped native save/recovery, ownership, interruption and vacant-slot behavior |

Connected campaign records use strengthened combat stats and retained accepted
prefixes. Later controlled chapter records also include advanced parties,
private inter-region placement or supplied prerequisites; those qualifications
remain; their separate prepared states are not evidence of the connected route.
The connected route is a chain of accepted native checkpoints, with normal owner
inputs steering controller routing mistakes. It is not one uninterrupted process
run. Normal-party balance is not established by strengthened
parties. Keep the user's actual play reports distinct from diagnostic evidence.

The final delivery review matches the requested capabilities to implementation
and retained runtime results:

| Requested behavior | Inspected implementation and evidence |
| --- | --- |
| At least four LAN players and Linux/Windows cross-play | Four-owner session limit and host authority in `session.h`/`gameplay.c`; four actual engines in both-host lobby/creation checks and the connected campaign ending |
| Main-menu lobby, character creation, chat and readiness | `mainmenu_ui.c` opens `lobby_ui.c`; `session-lifecycle-v47.json`, both-host native lobby summaries, preparation UI logs and `characters-launch-final` verify native controls, discovery, creation/equipment and launch |
| Initiator chooses while others observe | Both requester and host check chooser/conversation/revision in `dialogue.c`; native dialogue checks, four-owner service checks and original connected quest conversations include guest choosers |
| Campaign scripts, actions, travel, saves and endings work together | The accepted chapter records above form the original acquisition/travel chain to `connected-ending.json`; shared Save/Load retains identical native archives; prepared alternate endings and recovery cases are reused |
| Playable controls and reported bug corrections | `player-reports-p53/acceptance.json`, `party-ui-p54/acceptance.json`, `middle-pan-p54/native.json` and protocol55 crash/replication records; user reports improvement and significant first-area play without major issues |
| Runnable drop-ins, documentation and visual checks | All three builds pass; package manifests match executables/libraries and clean source; launch evidence includes spaces/Unicode paths; lobby, party layout and ending/menu captures inspected; player/install guides and this evidence record accompany the bundles |

The review reuses unaffected checks rather than replaying their matrices.
Detailed technical guides describe implementation and coverage limits; they do
not add speculative features to the campaign task list. No known required
capability or reproducible campaign blocker remains open in that list.

Matched drop-ins and hashes are linked in [local artifact catalog](RELEASING.md#local-artifacts).
Installers, Windows VMs and testing on real Windows are permanently excluded.
Retail assets are untouched and are not distributed. Prior detailed evidence
and launch notes remain in
the archived readiness record.
