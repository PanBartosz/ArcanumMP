# Campaign ownership and persistence

Native animation restoration accepts an invalidated or empty path with a stale
cursor. The engine can clear a path without resetting that cursor; treating it
as an active-path bounds violation rejected a retained original chapter before
play began. Loading normalizes this derived path state while continuing to
reject invalid buffer bounds and inconsistent active paths. Original animation
goals, actors and campaign state remain intact.

The ordinary lobby launches a host-authoritative shared campaign, loads snapshots
and retains the connection for the action services described in `RUNTIME.md`.
Save/load and gameplay reconnect are implemented; their accepted scope and the
remaining entire-campaign work are recorded in `STATUS.md`. The
legacy networking macros stay disabled so the campaign retains single-player
script behavior.

## Stable ownership

Each private runtime has an `arcmp-player.id` containing 16 OS-random bytes.
Creation uses an exclusive file create; invalid existing files are retained and
reported as errors. The identity is independent of the nickname and ephemeral
TCP slot. It is an ownership key on a trusted LAN, not authenticated proof.

The host's campaign registry has four fixed member slots. Each occupied member
stores its owner identity, permanent PC ObjectID, selected prototype and character
name. Process-local object handles are resolved from permanent IDs on access;
they are neither persisted nor sent across the network. A new connection with a
saved owner's identity can therefore map to its original member slot.

`coop_campaign_begin` validates a ready two-to-four-player host roster, distinct
nonzero owners and actual loaded PC prototypes before creation. It replaces the
menu placeholder with the host PC and uses the existing additional-PC primitive
for the remaining members. Each receives separate inventory objects and normal
initial weapon/armor selection. Custom character recipes replay native creation
rules; see `CHARACTERS.md`.

## Engine integration

- Party traversal includes every registered PC. Existing party/ally consumers
  see these PCs as one party.
- Map filtering keeps registered PCs and their followers active.
- There is one active campaign map. Cross-map travel initiated by a registered
  PC serializes every registered PC, their inventory and followers through the
  engine's normal teleport path. Same-map teleports remain individual actions.
  The runtime provides the consent and load/resume barrier in `TRAVEL.md`.
  Broader original story-exit coverage remains required.
- `gamelib_reset` clears the registry. A normal save writes the ownership sidecar
  after module/UI serialization and before archiving `Save/Current`. A load
  restores the sidecar before loading world objects, then validates that the PCs
  and local protagonist exist after all modules/UI load.

## Sidecar format, version 1

`Save/Current/arcmp-party.bin` is included inside the ordinary save archive.
All integers use big-endian byte order. Its exact size is 296 bytes.

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 8 | `ACPARTY` followed by NUL |
| 8 | 4 | Format version, 1 |
| 12 | 4 | Occupied member count, 2–4 |
| 16 | 276 | Four fixed 69-byte records |
| 292 | 4 | CRC-32 of bytes 0–291, zlib convention |

Each record contains occupied (u8), owner identity (16 bytes), permanent PC GUID
(16 bytes), prototype (u32), and NUL-terminated character name (32 bytes).
Absent records are written as zeros. Member zero must exist. Invalid sizes,
versions, checksum, counts, names, identity values, prototypes and duplicate
owners/PC IDs are rejected before replacing the live registry. Missing sidecars
mean an ordinary single-player save and clear the registry. Ordinary single-player
saves remove a stale sidecar before archiving.

The sidecar's staged validation is transactional. The cooperative runtime wraps
native saves and loads in all-owner barriers, atomic checkpoint publication and
interrupted-operation recovery; the ordinary native loader itself can mutate
state before reporting failure. See `COORDINATION.md` and `STATUS.md` for the current
controls and acceptance scope. CRC detects accidental damage and is not authentication.

## Integration harness

For the ordinary retained-party controller, `ARCMP_ORDINARY_MAINLINE=1` continues
the main investigation without making the optional stolen painting a prerequisite.
It supplies no positions, actors, money, supplies, health or story state. Spell
selection checks the native line of sight; recovery rest observes the initiating
member's replicated refusal status instead of repeating a denied request.

The current native animation loader reconstructs default path storage in sparse
unused slots and fixed capacity in loaded paths. The ordinary continuation exposed
that zero-initializing the unused staged slots prevented later movement/attack
goals after Load. The corrected reception/1205/map3 prefix and retained upgraded
party are in `evidence/ordinary-path-restore-v47`; chapter completion remains open.

Use separate disposable prepared runtimes. The explicit game argument
`--coop-engine-probe=roundtrip` performs the following in the actual engine:

1. Establish four real TCP sessions inside one process, select different retail
   presets, ready the roster and create the four registered PCs.
2. Enter the normal crash-site campaign path; validate all four visible PCs,
   party/ally traversal, distinct inventories, item parents and independent stats.
3. Travel to the shopping map and return while retaining the party.
4. Save through `gamelib_save`, alter the live fixture, reset and reload; check
   permanent PC IDs, owners, stats, inventories and a saved global variable.
5. Reject seven damaged/invalid sidecars without rebinding live ownership.
   Semantically invalid cases have recomputed valid CRCs.
6. Reconnect an original owner under a changed nickname/new TCP slot and resolve
   its original campaign member; this checks binding, not gameplay resumption.
7. Save and reload an ordinary single-player fixture without a sidecar, then
   return to the four-PC save.

`--coop-engine-probe=load` loads a transferred fixture. Both modes write a fresh
`arcmp-engine-result.json` containing permanent PC/item IDs, owners, equipment,
stats, map and saved global variable. `tests/integration_campaign.py` transfers
the actual save archives Linux → Windows and Windows → Linux, comparing these
reports exactly. It requires the PASS marker, a successful process exit and a
fresh valid report, preventing stale output or initialization failure from
passing. `--screenshots` captures each owned game window for manual review.

These checks cover a small controlled fixture, not combat, quests, dialogue,
all inventory operations, every map or the complete campaign. Four sessions
inside one engine are not four synchronized gameplay clients. Windows execution uses Wine; testing on real Windows is out of scope.

## Four-process initial synchronization

`tests/integration_bootstrap.py` runs the explicit `bootstrap` probe mode in four
actual game processes. Each has a private runtime/profile. A live four-player
roster creates the host's campaign; the host captures its normal archive pair
and sends a checked, bounded bundle over TCP. Clients validate the bundle/index,
load the reserved `arcmp-bootstrap` save slot and rebind their local PC using
their own profile. All clients acknowledge successful loading before commit.
The driver checks fresh reports, four different local PC IDs, identical member
inventories/equipment/stats and byte-identical archive pairs. It never copies
files between participants. Both Linux-host and Windows-host directions run.

This bootstrap probe holds simulation paused and closes its transport after
commit. It can capture all four real windows for visual review. It does not
accept gameplay inputs or replicate subsequent changes. A real launch flow must
retain the session, gate client simulation/mutations, provide progress/cancel UI,
and handle disconnects and resynchronization during play.

The bundle is limited to 64 MiB. Current fixtures are about 564 KiB; late-campaign
sizes remain to be measured. Index validation precedes reserved-slot writes and
the destructive engine load. This does not make whole-engine save parsing or
the entire load transaction safe against arbitrary hostile/corrupt contents.
See `PROTOCOL.md` for framing, roles, checksums, backpressure and barrier limits.

## Combat ownership lifecycle

Host NPC activity follows the nearest present owned PC. Combat damage and kill
credit reward living party members through native per-character modifiers. A
local controller death no longer resets the shared world, and owned bodies
retain their permanent IDs instead of decaying. The native encounter probe also
restores a full save bundle containing a dead local owner into replica authority.
See `ENCOUNTERS.md` for the exact tests and the remaining resurrection, party-wipe,
corpse-inventory and recovery work.
