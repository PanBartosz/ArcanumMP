# Shared exploration and campaign maps

Protocol 33 gives the host a single exploration atlas for the cooperative party.
Native position updates mark town-map tiles for every registered human, while
roof cutaways, wall cutaways and camera control remain local. Replicas draw the
host atlas and cannot create discoveries by opening a map. Original scripted
UNFOG actions update the same atlas, including maps outside the current region.

`ACT1` is a bounded, canonical replacement view. Its header is magic plus u32
record count. Each sorted record contains u32 town-map ID, u32 tile count, then
ceil(tile-count / 8) bytes of native tile bits. Integers are big-endian; native
bit order is retained. Zero maps are omitted and absent maps are unknown. Unused
high bits are zero. Limits are 256 records, 32,768 tiles per map, IDs 1–4,095 and
1,050,632 encoded bytes. The retail assets inspected for this batch contain 114
TMI files; the largest maps have 19,600 tiles. Native installation additionally
matches every ID and tile count against installed content. Decode stages the
entire view before replacing live knowledge.

`ACW4` adds an atlas byte count to the world header. Object records, deletions,
optional story and sector data are followed by the optional atlas. The tracker
sends atlas data only when it changes, independently of clock/story changes.
An initial view is always sent, including an empty atlas. The existing world
CRC, ownership handshake and applied-world acknowledgments cover it.

Native `.tmf` files remain the save format. The host imports legacy exploration
on first capture and keeps the current native tile cache and atlas consistent.
Switching town maps and saving propagate write/close/rename failures, retain a
failed dirty cache, and retry through a temporary file. Replica map switches
read the installed atlas rather than stale local files. A normal checkpoint
therefore carries exploration through travel, load and rejoin without a new
save sidecar or proprietary assets.

Once imported, both host and replicas read exploration from the atlas when
switching maps. Revisiting a known tile does not dirty its file, avoiding repeat
reads/writes when distant owners move within already explored regions.

An open map refreshes after an atlas change. Area discoveries also reconcile
the derived native location notes in an already-open world/continent map;
cleared discoveries remove their markers. Protocol40 also shares user annotations
and original script-created quest pins; see below. Marker reconciliation waits
while the local editor is active. Submitting an annotation keeps its draft until
the host's result and applied world revision arrive. A refused edit leaves the
text available instead of discarding it. While awaiting that result, further
text changes/submissions are held; Escape can cancel the local editor. A late
result cannot close a different draft opened after cancellation. Native note loading bounds record counts,
types/text lengths and rebuilds its local video-buffer cache after loading.

Native party/team enumeration now uses the campaign registry. Party contains
all registered humans; team adds each human's followers. Group/follower scopes
retain their original triggerer-specific meaning. NPC parties outside the
campaign and ordinary single-player iteration keep their native behavior.

The inherited timed long-route path entry was empty and could return an
undefined result. It now runs a bounded heap search over up to 16,384 nodes,
with the requested time/rotation limit and native NPC work budget. It retains
native traversal flags, target approach, door/window rules, trap/fire avoidance,
lighting costs and turn costs. The animation caller continues consuming native
eight-step prefixes and replanning. Exhausted searches return no route.
Player routes now use the 100 ms bounded heap search after a failed straight
route. The old local grid repeatedly scans all 4,096 cells and has no player
time limit; it is retained for NPCs with their existing work accounting.
The caller's length and buffer bounds still apply.
Destination occupancy ignores the moving actor and switched-off, destroyed or
extinct objects, and respects the same ignore-critter flag as the edge traversal
checks. Hidden actors cannot force a landing shift or reject an otherwise clear
movement endpoint.

Outside combat, human interaction approaches search all usable tiles within the
native interaction range together. A candidate must be walkable and pass the
same native interaction obstruction check used upon arrival. One bounded search
chooses the least-cost reachable endpoint; a reachable but unusable nearest tile
does not take precedence. The endpoint stays in the native path, while target
tracking retains the object's actual location. The available native rotation
buffer permits detours around large scenery even when the direct distance is
small. Combat and NPC approaches retain their existing rules. Cooperative loot
requests set the same interaction-path flag as a normal Use click, including
approaches through doors.

The combined `runtime-forest` fixture exercises party/team script loops, remote
exploration while observers view maps, scripted reveals of two maps, a temporary
map-write refusal/retry, divergence and voted restoration. It also follows the
original Stillwater/Qintarra quest route. Accepted evidence and its controlled
positioning/combat limits belong in `STATUS.md` and `history/CAMPAIGN_PROGRESS.md`.

Raven's installed dialogue includes `lf81` queries against a 32-bit local flag
word. Local flag reads/writes explicitly mask the shift count to five bits,
preserving the original x86 behavior on Linux and Windows without an undefined
C shift. The original retail dialogue remains unchanged.

## Shared native annotations (protocol40)

Press N in the world or current town map, then click to add/edit. Use the original
note/skull/chest icons and text editor. Enter submits; submitting empty text removes
a user note. Delete removes the pointed user note; N/right-click finishes editing.
Drafts are local copies and survive incoming changes. Edits/removals bind the exact
note revision, so a stale draft cannot overwrite another player's newer note.
On a conflict the draft stays open. Ctrl+Enter explicitly replaces the current
note with that draft, or adds it as a new note if another player removed it.
Escape discards the local draft. A second concurrent change is checked again.
Original script-created quest markers are shared and cannot be edited or deleted.

The catalog retains1024 notes, at most199 per map. Monotonic IDs and revisions
persist in `Save\Current\arcmp-map-notes.bin`, including a CRC. Missing sidecars
import old native world/town user notes and script markers. Active UI resources
never enter the portable record. Native location pins coexist with shared notes.
New edits validate actor ownership, current campaign map, town context and bounds.
The ordinary request/receipt and world barrier cover execution and exact replay.
The qualified native continuation in
`evidence/evil-checks/core-v44-native-panels-editor-devices.json` covers simultaneous
editor drafts, stale edit/removal conflicts, explicit replacement/recreation under
a new ID and Save/Load in both host directions. Wider native map combinations
remain in acceptance.

## Retail map metadata and retained exploration

The retail `Vendigroth Underground1.tmi` embeds town-map ID58 although the MES
request is54. Native lookups now cache by the requested map ID. This prevents
a subsequent map58 lookup from taking54's120x120 tile dimensions instead of its
own20x20 dimensions. The canonical world still validates the actual content
shape. Old saves can contain an exploration grid written with those incorrect
dimensions: only that incompatible grid starts unknown; the rest of the atlas
is retained. Subsequent native exploration writes the corrected grid atomically.

`evidence/dungeon-correct-charts-v44/linux-host` loads the affected retained save,
discards the incompatible map58 grid, and completes the original2383 transfer
with all four engines at the main fourth-floor landing. Its later blocked-door
timeout is not whole-stage232 acceptance. The original rejected chart payload
and grouped evidence are retained in `evidence/evil-checks`.

## Complete local routes — protocol52

The town map submits all plotted waypoints, up to six, in one owned movement
request. Every coordinate is checked before the actor's current action changes.
The native movement queue visits the points in order; a new click or Halt cancels
the previous route. The owner's Always Run setting and Ctrl/Num modifiers resolve
walking/running before transmission. The host does not reinterpret that choice.

Four mixed engines visit all six points; native checks also cover malformed
requests, invalid later points, wrong ownership and cancellation. Exact scope:
`evidence/local-routes-v52/acceptance.json`. These are local controls, independent
of the party's world-map journey vote.
