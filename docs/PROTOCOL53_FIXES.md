# Protocol53 — player-reported corrections

This batch addresses the six failures reported during actual protocol52 play.
Update every player together. A retained protocol52 story11 save loads and
finishes its arrival stage on all four protocol53 engines.
Linux and Windows drop-ins are linked in [the current bundle index](RELEASING.md#local-artifacts).

| Report | Correction | Verification |
| --- | --- | --- |
| Cannot join another human's real-time fight | Left click recognizes enemies focused on any human or their follower before the local character draws a weapon | All four initially sheathed owners send accepted native mouse attacks against one NPC focused on member0, over real TCP |
| NPCs skip animation frames and leave ghosts | Nearby NPC visual samples up to20Hz; short render interpolation; older bulk snapshots preserve newer samples; client object presentation ticks run; movement invalidates its old bounds | Mixed native/Wine execution, exact-position interpolation check,24 incremental Wine frames of a live walking NPC, visually reviewed |
| Sword cursor remains after victory | Track engagement, sheathe after hostile combat ends, and refresh the owning player's cursor from replicated stance/art | Native check observes the normal cursor after the last hostile dies or is knocked unconscious |
| Linux left click walks while Windows runs | Default multiplayer Always Run on; one-time migration of older defaults; current explicit choices retained | Native default, migration and explicit walking capture/install checks; portable preference codec checks |
| Crash-site elf falls into Virgil's ordinary dialogue | Deferred callbacks retain the script that opened the conversation, preserving replacements already made by the original content | The original1714/1715 exchange completes14 native pages and restores Virgil's1324 attachment |
| Client chat bubbles stay forever | Run the native text-bubble expiry timer with other client presentation work | Bubble expires through replica gamelib_ping while campaign time remains unchanged |

The elf encounter intentionally contains a short Virgil interjection. The fix
keeps its encounter-specific continuation and return to the elf. It does not
remove that original scene.

NPC interpolation changes rendering only. Authoritative coordinates are applied
immediately; replicas do not execute AI, combat, tile scripts or campaign clocks.
The visual cache clears at transitions, and missing newly spawned NPCs wait for
their ordinary world creation record. The stream covers NPCs within64 tiles of
any human; bulk world state continues to supply persistent properties.

Evidence is in `evidence/player-reports-p53/acceptance.json`. The combined check
uses Linux, Linux ASan/UBSan and two Windows executables under Wine. It retains
controlled combat preparation and is not ordinary campaign/balance acceptance.
The first visual capture selected a defeated target; it is excluded as animation
evidence. The final capture uses a live NPC's accepted native walk goal.

All five delivery turns remain used. Full campaign acceptance is incomplete.
Installers, Windows VMs and real-Windows testing remain permanently excluded.
