# Party pause, rest and checkpoints

Protocol 21 adds a runtime-lifetime party controller beside the per-map gameplay
controller. Pause, rest, save and load share its ordered requests and receipts.
An operation never reports success merely because its input was queued.

## Controls and consent

- **Pause** toggles your own pause hold, including while the native in-game
  menu or Options screen is open. Other travellers retain their holds when you
  resume. The host can select **Resume all** in Party (F12). Opening a local menu
  keeps the campaign clock running; its footer identifies Pause and F12. Party
  chat and F9/F10 shared save/load shortcuts also work from that menu.
- **S** opens the native rest/wait picker. Clicking a bed approaches it and
  opens that picker. The chosen duration proposes a group operation.
- **F9** creates the shared quick save; **F10** proposes loading that exact save.
- **Save party** and the native Save menu open the party save browser. There
  are 63 named slots, plus quick save, recovery and three rotating autosaves. Select a
  slot, type a name and choose Save here. Overwrite requires a second click.
- **Load party** and the native Load menu show the host's saved campaigns to
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

Transport, chat, state replication and acknowledgments continue during pause.
Gameplay action clocks and native simulation stop. Queued native work remains
pending until resume. Native confirmation dialogs also poll transport and
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

The host requests a shared autosave after ten minutes of advancing connected
play. Three party-specific copies rotate, choosing an empty slot or the oldest
valid copy. Autosave waits through combat, conversations, pending arrivals,
character allocation, service/actions and other party operations. Pauses,
movies, loading and time spent disconnected do not count toward its interval.
Automatic copies are read-only in the save browser and load through the same
unanimous vote. Manual and quick saves remain separate.

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
