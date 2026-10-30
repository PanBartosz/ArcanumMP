# Synchronized map travel

The runtime now retains the lobby connection through a native whole-party map
transfer. The host freezes the current map, waits for acknowledgment of its final
world/dialogue/pose state, performs the native transfer, and sends a normal save
of the destination to every client. Character controls resume under a fresh
epoch after every participant has loaded and confirmed its owned PC.

This is a development checkpoint. The tested round trip uses the crash site and
the retail shopping map. Protocol 22 additionally implements collective world
journeys, same-map party transitions and shared transport cutscenes. Broad
story-region, interior and entire-campaign acceptance remain open.

## Plotted journeys — protocol 51

The native world map now submits every plotted waypoint, up to its original
30-point limit. Previously only the final destination reached the host, losing
player-selected detours around terrain. The party vote carries the complete
route; the host validates each destination and reconstructs all native segments
before moving. Rejected submissions leave the plotted map open. The shared
vote shows the waypoint count. Encounters, Stop and disconnect interruption
retain their existing behavior; interrupted-operation state preserves the route.
Checkpoint formats are unchanged, but all connected executables must use 51.

The world pathfinder also counts detour rotations correctly: it no longer
leaves an unwritten step between segments. Read-only installed-terrain inspection
finds a legal Ashbury→60256,45280→Wheel Clan route where a direct request fails.
The connected four-engine continuation uses the owner's earned map discovery
and spectacles; exact acceptance and remaining limits are in STATUS.md.

## Native entry and barrier

`teleport_do` calls `coop_runtime_defer_teleport` before modifying the native
teleport queue. During an active campaign, a host transfer of a registered PC
to another named map is copied into the runtime, together with the travelling
PC's permanent ObjectID. No process handle is sent over the network. The hook
returns accepted while leaving the current map and travel time unchanged.
Native area exits and scripted party transfers carry `TELEPORT_COOP_PARTY` and
use the same barrier even when their destination is on the current base map.
Untagged same-map spell teleports retain their individual engine path.
Protocol 28 normalizes the native `map == -1` sentinel to the current map
before deciding this boundary. A tagged current-map script transfer therefore
uses the whole-party barrier instead of changing only the host's live world.
Door/scenery use now reaches original retail exit scripts through the ordered
host action service. See `history/CAMPAIGN_PROGRESS.md`.

The host enters DRAINING and calls `coop_gameplay_transition(current_epoch + 1)`.
The existing finish barrier settles object/story batches and queued conversation
views, sends a single final pose revision, and waits for all clients to confirm
it. Protocol 15 also seals positional sound production, stops published loops
on replicas, and waits for every queued sound batch to be applied and
acknowledged before that final revision. See `AUDIO.md`. Protocol 16 first lets
already accepted native item use/skill/throw goals finish on the host while
rejecting new intentions. It then freezes simulation, seals audio and settles
the final world capture. Personal reading/schematic events must also be
acknowledged. An active item-goal shutdown is exercised in `USE.md`; travel
during every tool/script/throw branch still needs acceptance. Protocol 8 adds a next
epoch to GAME_FINISH: zero retains the diagnostic finishing behavior; a travel
epoch must be exactly the current epoch plus one, without wrapping.

Protocol 18 also drains admitted native skill/theft goals and their personal
feedback. Skill completion tickets are local and not saved; durable pending
action recovery and every skill branch during travel remain unverified.

A client receiving the travel finish frame acknowledges the final revision,
destroys its old gameplay service and creates a bootstrap receiver for the new
epoch. The host performs the equivalent retirement after receiving every final
acknowledgment. Native dialogue releases its object references before map objects
can be deleted. The old object tracker and conversation service are destroyed;
they cannot interpret handles or revisions from the new map.

## Destination load and resume

The host resolves the traveller by permanent ID and queues its saved native
TeleportData locally. `teleport_flush` returns the native processing result;
ordinary `teleport_ping` also uses it. Existing native serialization carries all
registered PCs, inventory and followers. Fade, sound, movie flags and the travel
time cost are processed once by the host. Protocol 22 shares its movie calls
through the campaign-lifetime presentation service; see `MEDIA.md`.

After loading, the runtime validates all campaign PCs and finds separate,
pathable entrance tiles near member zero. It captures the engine's normal save
pair and begins the existing bounded snapshot transfer under the new epoch.
The archive validator accepts the native root town-map fog (`.tmf`) and note
(`.tmn`) files, as well as the required data/ownership files and maps subtree.
Path-component and length checks continue to apply.

Clients restore that save under REPLICA authority, bind their saved profile to
the corresponding PC, and acknowledge loading. Host simulation remains stopped
through the loaded/commit barrier and the new owned-PC handshake. A fresh world
tracker then publishes the destination baseline. Bootstrap messages must carry
the runtime's expected epoch. An old gameplay command is rejected even if its
actor/map/coordinates would otherwise be valid.

The party panel appears during draining/loading and retains chat. Escape/Back
cannot accidentally leave a loading attempt; Leave party remains available.
The panel closes automatically on resume. Opening party chat during ordinary
play continues to be a local overlay.

## Failure behavior

The existing transfer and final-acknowledgment progress timeouts apply. A client
waiting for the destination offer also has a 30-second deadline. Transport
heartbeats/timeouts remain in effect. Terminal host loss, invalid frames,
failed native loading, missing characters or failed snapshot capture stop the
controller and freeze native simulation. Error text identifies the failed stage.
There is no automatic rollback. Protocol 25 adds live peer rejoin for ordinary
play and a fresh epoch after interrupted snapshot loading. A roster change
retires the transfer and resynchronizes the host's current world. World journeys
stop at the native interruption location, retain elapsed time and restore all
party members before reconnecting. See `RECONNECT.md`. Protocol 21's durable
checkpoints allow recovery in a new lobby with another host; see `COORDINATION.md`.
An interrupted transfer's differing maps/clocks remain frozen until the new
epoch commits; a failed recovery reports failure rather than shared completion.

## World journeys

The native world map remains the route planner. Selecting travel proposes the
destination to every player through the party panel. All must agree before
the old gameplay epoch drains. The host temporarily binds the initiating PC,
including a remote owner, and runs native route planning, world-map scheduler,
game-time increments, area discovery and random encounters. Replicas wait for
the shared destination snapshot. Anyone can press **Stop journey**; the party
arrives at the native interruption location and resumes together.

The initiating traveller must be awake, the group within eight tiles, and the
party able to leave combat. Blocked or out-of-bounds destinations are refused.
The request packs sector coordinates because the retail world is wider than
65,535 tiles. Progress reports elapsed game hours. Journey waiting has its own
deadline policy so a long native trip does not expire the destination offer's
30-second idle timer. The host restores its original owned PC after travel;
character IDs and ownership remain unchanged.

Sector centres can contain blocked terrain. Party arrival precaches the local
sectors, finds a nearby walkable landing, then places separated PCs on pathable
nearby tiles. Terrain's compressed row headers are read with `memcpy` because
their lengths do not guarantee aligned 32-bit addresses. World-map creation
also guards its invalid initial mode before refreshing a previous mode.
Native sector differences are serialized by original file index (`TEMP_ID`),
rather than draw order, so a save/reload cannot apply a tree's changed fields
to another piece of scenery on the same tile. Map-note video buffers are
released on close and replacement.
The difference loader converts sector coordinates to tile origins with the
same factor of 64 as the ordinary sector loader. Without that conversion,
reloading generated wilderness could move unchanged scenery to coordinates
near the map origin while retaining its previous positional identity.

## Evidence and reproduction

Run four prepared private runtimes, with two Linux and two Windows binaries:

```sh
python3 tests/integration_bootstrap.py \
  --runtime runtime/isolated-host \
  --runtime runtime/campaign-probe-asan \
  --runtime runtime/isolated-windows-1 \
  --runtime runtime/isolated-windows-2 \
  --sanitized-runtime runtime/campaign-probe-asan \
  --mode runtime-travel --framebuffer \
  --output evidence/travel-roundtrip-final
```

The driver runs both host OS directions. The opt-in engine fixture launches the
normal runtime controller, closes the opening conversation, creates a carried
marker item and an item left on the original map, then requests native travel
using a remote-owned PC followed by the host PC. Each trip adds 17 seconds.
Assertions verify:

- Every PC keeps its permanent ID and profile binding through epochs 1–3.
- The carried item stays with member two; the ground item reappears exactly once
  on returning to the original map. A script global and full inventories persist.
- Travel time stays unchanged when deferred and advances exactly once per trip.
- The first resumed native tick counts only current frame time, without a jump
  caused by wall time spent waiting at the load barrier.
- One client delays destination loading by 1.2 seconds with its native clock
  frozen; the host cannot resume before that delay completes.
- All four players can send normal UI movement again after returning.
- Final story bytes, owned inventories, save archives and four-player state agree.
- Whole-engine ASan, UBSan and leak checking remain enabled on one participant.

`--framebuffer` records each engine at all three map entries and after final
movement. These are composed TIG game frames using SDL dummy/software drivers;
they do not capture the desktop. Manual vision review covers all 32 frames in
`evidence/travel-roundtrip-final`, including both host directions. Independent
cameras and owned-PC HUD values are expected to differ.

Use `--mode runtime-travel-loss` to disconnect the host after it changes map but
before offering the destination snapshot. All participants must report FAILED
and native game/timer pings must leave their clocks frozen. Their terminal
maps/clocks/archives are intentionally not compared as a committed destination.
Evidence: `evidence/travel-loss-final`.

Use `--mode runtime-travel-stale` to submit an otherwise valid movement command
with epoch 1 after the party has entered epoch 2. The host rejects the malformed
protocol participant and the locked party freezes through the normal failure
boundary. Evidence: `evidence/travel-stale-final`.

The original `runtime`, `movement`, `dialogue` and campaign/save fixtures remain
required regressions. The travel fixture invokes the real native teleport entry
point directly; it does not prove a desktop-driven story exit, shared world-map
selection, every destination's entrance placement or long-session save growth.
Persistent conversation history across a map load is also still pending.

`tools/desktop_smoke.py --launch --controls --loading-check`, with four ordinary
prepared runtimes, temporarily stops one owned native client before campaign
launch. Real Escape/F12 input must leave the host in its loading panel; resuming
the client then completes loading and all dialogue/movement/chat controls.
`evidence/travel-desktop-linux-final` and `travel-desktop-windows-final` exercise
both host directions. Their loading captures show readable status, disabled
Back/character/readiness/start controls, and available Leave party/chat controls.
The eight dialogue frames, eight movement frames and both party-chat frames were
also reviewed. Entry lobby/campaign captures were not separately reviewed.

An earlier Windows desktop trial reached the closed conversation but produced
no host movement requests from four attempted pointer clicks. The subsequent
run, with explicit compositor focus assertions and recorded pointer geometry,
passed all four controls. The cause of the earlier input delivery failure is
unconfirmed; its logs/frame are retained in
`evidence/travel-desktop-windows-pointer-trial`. This remains an input reliability
investigation, separate from the deterministic native UI/TCP action fixtures.
