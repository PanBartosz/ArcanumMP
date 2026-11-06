# Party timing, rest and checkpoints

The runtime-lifetime party controller coordinates rest, save, load and travel
beside the per-map gameplay controller. Requests and receipts remain ordered.
An operation never reports success merely because its input was queued.

## Controls and consent

- **Pause** changes native turn-based/real-time combat timing. Multiplayer
  timing belongs to the host. Automatic triggers use this same native combat
  state, with a configurable 5–40-tile radius from a player. They never create
  a session pause hold or force the Party window open. Local menus keep the
  campaign running; F12 still opens party chat, voting, saves and gold.
- **S** opens the native rest/wait picker. Clicking a bed approaches it and
  opens that picker. The chosen duration proposes a group operation.
- **F9** creates the shared quick save; **F10** proposes loading that exact save.
- **Save party** and the native Save menu open the party save browser. There
  are 63 named slots, plus quick save, recovery and three rotating autosaves. Select a
  slot, type a name and choose Save here. Overwrite requires a second click.
- **F12 → Load campaign** and the native Load menu show the host's saved campaigns to
  every traveller. The vote identifies the selected slot and saved name. A
  changed file invalidates the selection rather than loading a different save.
- The lobby host's **Resume save** opens the same browser. All saved owners must
  be present and Ready. **Recover party** retains its direct recovery action.
- The browser supports mouse, arrow/Page keys, Home (quick save), End (recovery/autosaves),
  Enter, Tab to edit the save name and Escape. Remove local requires confirmation
  and is available to the host for named slots. It leaves other players' copies.
  Refreshing the same page preserves its selected row and an edited save name;
  an asynchronous catalog reply cannot replace text being typed. Selecting a
  different row/page still selects that campaign's name, and refresh clears an
  earlier overwrite/removal confirmation.
- Rest and load open the Party panel for a unanimous, 30-second vote. The
  proposer agrees automatically. Anyone can decline; anyone can stop resting.
  Stop and vote remain available after action draining reserves the next world
  epoch and while native rest/journey work is active.
  Save requires the connected party's synchronization acknowledgments.

Transport, chat and state replication continue during native turn-based play.
The earlier player-facing custom pause/banner has been removed. The controller
retains internal pause fields for ordered barriers and legacy protocol fixtures. Native confirmation dialogs also poll transport and
simulation; a shared vote or transition dismisses them so it can proceed. Operation barriers can drain work and resynchronize while
preserving each traveller's pause hold for the resulting campaign.

## Native rest

The host validates ownership, consciousness, enemies, party proximity, town
waiting rules and any bed's existence/occupancy before execution. Party members
must be within eight tiles of the initiating PC. A bed must be within three.
Bed permission scripts execute once after consent, before native bed entry.

The eight native choices remain one/two/four/eight hours, one day, morning,
evening and fully healed. Waiting in a town does not apply sleeping healing;
fully healed is unavailable there without a bed. Every human PC and its native
followers participates. Followers are deduplicated and receive the native
leader-sleeping notification once.

Rest advances one native hour per step, processes ordinary scheduled engine
events between steps, and applies native HP/fatigue healing. Beds use the native
double rate. Time events, scripts and interruptions remain active on the host.
Long rest has a 168-hour bound. A shared snapshot replaces every replica after
rest, including interrupted rest; the displayed result includes elapsed hours.

A scripted conversation or party transfer interrupts rest. Conversations that
arrive while the action controller is detached are retained with their original
speaker and owning player, included in the rest checkpoint, and presented to
everyone when controls return. A queued transfer waits for those conversations
and their observer acknowledgments before moving the whole party. Bed and
follower callbacks also stop continuing if the map, campaign epoch or runtime
has changed. The native script is not rerun to recover its requested action.

## Durable files and recovery

Each runtime keeps `arcmp-campaign.acsave` for quick saves and
`arcmp-recovery.acsave` for synchronized recovery boundaries. These contain a
complete native campaign archive and the owner registry. A sibling temporary
file is written and synchronized before atomic replacement. A failed write
before replacement preserves the previous checkpoint. Files do not contain
retail assets.

Named files use `arcmp-save-<party-key>-<slot>.acsave`. The party key is derived
from the content fingerprint and sorted owner identities, so rehosting or changing
connection order retains the same slots. A different party gets a separate set.
Content and complete owner checks still validate every file; the key is only a
namespace, not an authorization mechanism. Slot previews read and check the header
and exact file length. Actual load also validates the full payload checksum and
native archive before replacing the campaign. Every participant stores the same
selected slot and description before acknowledging the shared save.

The host selects periodic autosaves (Off or 2/5/10/15/30 minutes) and saving
before map/world travel in F12 → Autosave or B → Settings. Defaults are five
minutes and travel saving enabled. The authoritative host policy is shown to
every player; client attempts to change it are refused. Preferences are local
to the host and persist across launches.

Three party-specific automatic copies rotate, selecting an empty or the oldest
valid slot. Autosave waits through combat, conversations, arrivals, character
allocation, service/actions, local native panels and other party operations.
Pauses, movies, loading and disconnected time do not count toward the interval.
Before travel, the host captures the drained departure map before native
teleport/route setup. The pending transition is omitted only from this snapshot,
so loading does not repeat it; the running transition is retained. The host
captures the settled native world synchronously and writes the archive
without a shared reload or epoch change. Clients retain their open interfaces.
Automatic copies live on the host and appear in its shared save catalog; loading
uses the ordinary unanimous vote. Manual and quick saves remain separate.

All participants store the received shared checkpoint before acknowledging a
manual save. Recovery snapshots are also stored after campaign bootstrap,
travel, rest and load. The host stores a recovery snapshot before replacing a
running campaign with a manual checkpoint. A failed native load freezes the
session with instructions to recover the previous campaign.

After leaving a failed session, host or join a new lobby with the original
`arcmp-player.id` profiles. Every saved owner must join and mark Ready. The new
host chooses **Resume save** or **Recover party**. Lobby character selections
are replaced by the saved characters, with their original IDs, inventory and
stats. A different player can host using their local copy of the checkpoint.
Content fingerprints and the complete set of saved owners must match.

Recovery restarts at the last stored boundary. Protocol 25 also lets original
owners return to a live host through a fresh snapshot; see `RECONNECT.md`.
Live return interrupts native rest/world journeys, preserving completed work and
reporting elapsed hours. The save-slot browser and live return inside presentations
are now implemented; runtime acceptance of the latest additions remains open.
Arbitrary replacement owners remain outside the current ownership policy.
Native bed/inn script branches, unusual rest event chains,
combat saves and arbitrary campaign saves still need broader acceptance.

The accepted combined fixtures are `evidence/controls-bound/windows-full`
(18 stages) and `evidence/controls-bound/linux-tail` (stages 8–18 resumed from
retained data). Both continue with a different host and verify four unchanged
owner/character/inventory bindings and identical checkpoint copies. The final
party panel is exercised by short `runtime-recover-ui` fixtures on both host
platforms; screenshots show all five toolbar labels, consent state and countdown.
The native bed case uses retail script 3023 with its granted-use flag. Its
controlled follower has automatic healing spells removed so they cannot race
the rest/save assertions. This does not establish every inn or NPC rest branch.

## Wire and disk layout

Types 61/62/63 carry a 244-byte request, 254-byte latest view and 8-byte
epoch/revision acknowledgment. Integer fields use big-endian encoding.
Requests include epoch, per-owner sequence, map, actor GUID, action, value,
proposal number and optional permanent bed ID. Views include phase, pause and
vote masks, proposer, action/value, elapsed hours, per-owner receipts, countdown
and campaign context. Reserved bytes must be zero. Exact request replays cannot
repeat execution; a retired operation's final request can receive its receipt
after the next epoch. Native execution waits for every participant to acknowledge
the current draining view.

Protocol51 appends a waypoint count and 30 packed sector coordinates to both
messages. A direct journey may use count 0; a plotted route uses 1–30 and must
end at `value`. Unused entries are zero, adjacent duplicates are rejected,
and non-travel actions cannot carry routes. Interrupted travel retains the route
in the published view. See PROTOCOL.md for offsets. The host reconstructs all
native segments rather than discarding intermediate waypoints.

Protocol38 appends the selected save header fingerprint (u32), presence flag
(u8) and printable NUL-padded description (64 bytes) to both control messages.
Save/load values select slots0–67; slot64 is recovery and cannot be overwritten.
Slots65–67 are rotating autosaves; only the host may request writing them.
Other actions require these extra fields to be zero. A selected load is revalidated
after consent and read with the expected header fingerprint on the same open file.

Types70/71 query the host's save catalog. An eight-byte request carries serial
and page (u32 each). A 380-byte page contains serial/page, row count (u8), three
zero bytes and four92-byte entries. Each entry contains slot/status/owner-mask
(u8 each), a zero byte, epoch/map/payload-size/header-fingerprint (u32 each),
local modification time (u64), and description64. Unused rows are zero. Four
header previews are served per page, with bounded per-client frequency. No
native paths or player profile secrets are sent. The catalog is read-only;
save and load still use the authoritative party operation and its acknowledgments.

Checkpoint headers are 196 bytes: magic `ACCOOP1` plus NUL, content hash (32),
epoch/map/payload length/payload CRC (four u32), owner mask (u8), seven zero bytes,
four owner identities (64), printable NUL-terminated description (64), then a
header CRC (u32). The bounded payload is the checked `ACSNAP1` native archive.
Load validates the header, content, owners, exact length, both checksums and
archive index before touching the running engine.

## Campaign and personal options (protocol39)

Use the original Options screen for host-controlled difficulty and real-time,
standard turn-based or fast turn-based combat. Clients display the replicated
campaign values. These settings persist with the campaign. Each human separately
controls auto-attack, weapon switching, always-run and follower skill assistance;
initial values come from that owner's preferences, and authenticated changes persist
in the campaign. Client preference files and the host's campaign difficulty remain
separate. New options and destination recovery await native runtime acceptance.


## Focused party controls check in 0.3.0

The `runtime-party-qol` check reuses the existing four-process controller loop.
Two Linux and two Windows/Wine engines, with Polish and English presentation,
complete nine stages: replicated host policy and refused client edits; native
recipient/amount gold UI; an original-T pistol craft on a Windows/Wine client;
host autosave while that client keeps T open; named save; replicated gold/world
mutation; F12 named-load refusal without a quick save; unanimous named load;
and unanimous load of the host-only autosave. Restored balances and the crafted
pistol are checked by every peer. No campaign route is replayed.

A separate native renderer check crafts that pistol in Crash Site Cave at
75/100/150% zoom, applies the world delta and closes T. The map remains visible,
the lens is released and the native render lock is balanced. This does not
reproduce the player's intermittent black map; that exact case remains open
in the active TODO. Native hover text, skill and spell descriptions also survive
12 passive refreshes, release on pointer movement and yield to explicit feedback.


## Playthrough feedback corrections in 0.4.0

The focused controller scenario extends to 15 stages on two Linux and two
Windows/Wine engines. It additionally checks departure-map autosave, a stable
return to that map on load, rejection of a trigger 30 tiles away at radius 5,
native turn-based activation nearby without a pause hold/F12 popup, status
floaters on PCs and an NPC, damaging native critical hits with one peer enabling
shake and the other three disabling it, and direct F12 access to autosaves.
Native visual captures cover the initial menu lobby, compact/expanded log at
150% text, Polish floaters, and world-only shake at 100/150% zoom. The interface
pixels stay fixed and the restored world matches the pre-shake frame exactly.


## Shared party access and playthrough corrections in 0.5.0

Protocol 63 adds the host's **Shared keys and story items** policy: Current map
(default) or 8 tiles. It occupies bit 17 of the existing play-policy word and
is read-only on replicas. Preferences append indices 14 (personal log following)
and 15 (host sharing scope); previous preference indices and save formats do
not change.

Possession checks share native keys/key rings and classified script-visible
story-item names. Chooser inventory wins, then their followers, then other
eligible active humans/companions in stable team order. Script checks,
transfers/destruction and chooser dialogue IN/NI/item actions resolve the same
actual native instance. There is no global replacement of `item_find_by_name`,
no synthetic inventory and no shared purse or ordinary crafting supply pool.
NPC/container checks and equipped/disguise checks retain their native scope.
A particular mod's extra quest-item names may need an explicit classification.

A separate companion relation gives campaign members access to recruited NPCs'
dialogue, normal spell/skill healing and free inventory services. It does not
replace the physical leader getter. Summons, illusions, familiars and controlled
NPCs do not acquire shared rights. Only the physical leader may issue follower
orders other than opening inventory; dialogue command options are filtered and
revalidated as well. Native distance and shared-interaction admission remain.
The original Virgil script now recognizes a non-leader as a companion speaker
without changing that speaker, recruitment ownership or personal quest checks.

Log Auto follows new entries and persists personally. Manual browsing suspends
following visibly while retaining both selected-event and top-visible-event
anchors; Latest resumes. Off-turn inspection handles mouse motion/pings only,
shows the native HP/fatigue card and avoids scripted examine requests. It leaves
action/AP admission in the existing turn gate.

Internal recovery writes and pre-travel automatic saves no longer reset periodic
elapsed time. A successful manual save or periodic save does. Due requests wait
for settled actions and safe native modes; F12 and the settings detail expose
the next/last save or the reason for waiting. Failed writes retain prior files,
report a notice and retry. The three automatic slots and archive formats remain.

Screen-anchored direction/ally hints were copied with the world surface during
scrolling and later erased at stale screen coordinates. Their recorded regions
are now invalidated before the native pre-scroll draw/copy, without forcing a
full-world redraw each frame. Direction labels also fit their bounds and avoid
one another vertically.

### Scoped validation

- A connected 12-stage scenario passes on two Linux and two Windows/Wine engines
  with mixed English/Polish presentation. It covers gold, original client
  crafting, named/automatic loads, policy replication and rejected client policy
  edits. A non-leader opens native companion inventory and receives the real
  item for zero gold; another non-leader's dismissal is rejected. The physical
  leader is preserved. Earlier accepted campaign prefixes were not replayed.
- A separate real two-minute timer runs on the same four-engine party. An actual
  interim travel save at 60 seconds does not postpone the periodic due time.
  An open settings menu defers the due save with a visible reason; closing it
  allows the periodic write at 127 seconds. No timer/elapsed injection is used.
- The retained three-player Steam save loads in an isolated runtime. Original
  Virgil script 1324 offers companion page 385 to a different speaker; follow/
  dismissal command execution is rejected for that speaker. Native healing
  choices route through the normal minor-healing action with no gold charge;
  immediate HP gain was not established by that snapshot. Original native
  key-ring checks, dialogue IN/NI and hand-in, and script item destruction verify
  actual-holder behavior, whole-map/8-tile choice and personal generic supplies.
- Native screenshots show the off-turn HP/fatigue card. Hover selects the target;
  a click does not change the other actor's turn or 12 AP. Log checks retain
  selection/history across new events and resume at Latest.
- Continuous diagonal panning with a character 40 tiles away reproduces green
  label trails in 0.4.1 (2,578–7,383 differing pixels against a clean redraw).
  The corrected sequence has no green residuals in 20 comparisons at
  75/100/150/200% zoom; all-frame differences are 0–10 pixels. The supplied
  player's crop cannot identify each individual label beyond this reproduced
  failure class. Rendering validation here uses the native Linux engine;
  Windows executable runtime evidence is Wine.
- All 34 portable checks pass. Five retained named/automatic saves from 0.3.0
  and 0.4.0 load through normal verified content migration/native restore,
  retaining owner IDs, local-character binding, map, balances and world marker.
  The original eleven Steam save/config/identity files remain byte-identical.

Private evidence: `build/localization/party-access-050/`,
`build/localization/periodic-timer-050/` and `build/localization/party-050/`.
Original game assets, player saves and private diagnostic files are not included
in public source or releases. The separate black map after original crafting
remains unreproduced and open in the campaign TODO.
