# Campaign state and shared journals

The host runs native campaign scripts and shares their results with all players.
This guide describes campaign state and journal policy. Current integrated
coverage and remaining whole-campaign work are recorded in [STATUS.md](STATUS.md);
the older scenario descriptions below retain their original evidence scope.

## Replicated state

`story_state.*` is a portable codec independent of the engine. `engine_story.*`
translates its values through explicit native state adapters. It includes:

- All 2,000 global script variables and 100 flag words (3,200 flags).
- The campaign story number and 1,000 global quest states.
- Global rumor quell bits, all 256 monster-generator state bytes, and the entire
  25-entry newspaper queue and its count.
- Discovered-area flags, with the area count checked against loaded content.
- Host game and animation clocks as explicit day/millisecond pairs.
- Shared combat timing, current actor, AP, round and generation; see `COMBAT.md`.
- Maintained-spell tokens, caster GUIDs, spells and optional target identities;
  see `MAGIC.md`. Native effects and causes remain object fields.
- Lighting scheme/hour/colors, sound schemes and combat music presentation;
  see `ENVIRONMENT.md`.

The changed canonical state is appended to an acknowledged `ACW2` world batch.
If it has not changed, the batch omits it. Ongoing host clock changes normally
produce a state update every 200 ms. All story values and object records in a
batch are validated before existing normal object fields or subsystem values
are replaced. Missing object references or invalid schemas therefore cannot
commit story values from an otherwise valid payload. Positional lookup and
new-object allocation still have the engine side effects described in `WORLD.md`;
this is not a transaction over the entire engine.

Native restoration copies values directly. It does not invoke quest setters,
award XP, run scripts, fire time events, rotate newspapers or spawn monsters.
Global terminal quests and monotonic story numbers can be restored to the exact
host value even when a client has a corrupted higher value. The clock UI is
refreshed separately from the timer scheduler. Copies of clocks do not execute
the client's pending event queues or advance its simulation.

The global quest logbook notification is refreshed when global quest state
changes. New area discoveries notify the local world-map UI. The last-viewed
area notification stays local: opening a map and clearing that marker does not
change shared discovery state or cause it to flash on every clock update.

## Shared story policy

For registered cooperative PCs, `quest_state_set` shares the resulting quest
entry and timestamp across the party. Acceptance, achievement, completion,
botching and unbotching are shared. A first successful party completion gives
each PC the normal full quest XP reward once, using the engine's difficulty,
character modifiers and living/dead eligibility. Completion alignment changes
also apply to the party. Subsequent completion calls see the terminal shared
state and give no further reward. This avoids slowing each player's character
progression merely because four people are playing together.

The native quest operation still handles the initiating character's NPC
reaction. Protocol 23 adds explicit sharing for dialogue/script reputation,
alignment and XP operations; direct reputation mutations retain their native
behavior. See `CONTINUITY.md`. Combat damage/kill credit retains its
separate party-sharing policy in `ENCOUNTERS.md`.

Learning a rumor through the public rumor operation shares it with registered
PCs. Discovering an area through any registered PC now updates shared discovery;
the original local-PC-only check discarded discoveries by other party members.
Outside a cooperative campaign, ordinary single-player behavior is retained.
Replica map loading also uses this authority boundary: it may read discovery
flags but cannot reveal an area absent from the host's snapshot. The initial-load
checksum test caught an automatic client-only discovery at the crash site.

Native journal entries now break equal timestamps by quest number, making their
order deterministic across the Linux and Windows C libraries. Story text still
comes from the installed retail content and the local character's normal or
low-intelligence description rules.

## Portable format

Protocol version 5 introduced `ACW2`, retained in version 6. Its 20-byte header contains magic (4), map (u32),
object-upsert count (u32), GUID-deletion count (u32), and story byte count (u32).
Length-prefixed object records and deletions follow as before, then the optional
story payload at the end. Zero story length means no subsystem changes.
The whole batch remains bounded to 16 MiB. The object record cache remains
bounded to 16 MiB, plus at most 64 KiB of cached subsystem data.

Protocol 24 uses `ACS4`; all integers use big-endian encoding and no native
padding is included. It retains the earlier `ACS2` field positions and appends
the maintained-spell view after combat state, followed by the environment view.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | `ACS4` |
| 4 | 4 | Appended maintained-spell view byte length |
| 8 | 4 | Area count, at most 4096 |
| 12 | 4 | Newspaper queue count, 5–25 |
| 16 | 4 | Nonnegative story number, i32 |
| 20 | 8 | Game days and milliseconds, u32 each |
| 28 | 8 | Animation days and milliseconds, u32 each |
| 36 | 8000 | Script variables, 2000 i32 values |
| 8036 | 400 | Script flags, 100 u32 words |
| 8436 | 1000 | Global quest states, one byte each (0–6) |
| 9436 | 251 | Native global rumor bit storage, including its extra byte |
| 9687 | 256 | Generator state: concurrent count bits 0–4 and disabled bit 7 |
| 9943 | 100 | All 25 newspaper entries, i32, each at least -1 |
| 10043 | Area count | Area flags, each zero or one |
| 10043 + area count | 37 | Canonical `ACB1` combat state; see `PROTOCOL.md` |
| 10080 + area count | Variable | `ACM1` maintained-spell view: eight-byte header plus up to 512 entries of 43 bytes; see `PROTOCOL.md` |
| After `ACM1` | 52 | `ACE1` lighting and sound view; see `ENVIRONMENT.md` |

Milliseconds must be below 86,400,000. Dates must remain representable by the
engine's signed 32-bit seconds helpers. The codec rejects trailing/truncated
bytes, unknown reserved bits, invalid enums/counts, and malformed clocks before
changing its output. The retail fixture contains 82 areas, giving 10,222 bytes
including combat state and an empty maintained-spell view. Each maintained entry
adds 43 bytes.

The highest script flag and counter bytes exposed inherited signed-shift UB.
Global/PC/local script flag and counter packing now uses unsigned operations;
the engine probe exercises bit 31 and counter byte 3.

## Verification

`tests/integration_bootstrap.py --mode movement` now performs actual host
mutations during the eight-second shopping-map fixture: script globals/flags,
story number, discovery by another owned PC, shared rumor knowledge, global
rumor/generator/newspaper state, quest acceptance/achievement/completion, and a
second quest's botch/unbotch. It checks every PC receives XP and duplicate
completion requests do not reward again. Clients deliberately corrupt globals,
flags, story and a terminal global quest value; later host state repairs them.
All four processes must produce byte-identical `ACS4` data and identical clocks,
XP, journal order, inventories and final poses in both host OS directions.

One client delays world application/acknowledgment across host finishing while
continuing to receive poses. This exposed a reused final pose revision. The
adapter now starts one distinct final revision after the world queue drains.

`--journal --screenshots` opens each owned PC's actual quest log for desktop
capture. `--journal --framebuffer` uses the actual composed TIG game frame with
SDL dummy video/software rendering and works without accessing the desktop.
The latter can run alongside ASan/UBSan and leak checking. The screenshot driver
rejects a detected locked Hyprland desktop; lock-screen captures are failures,
not visual evidence. Every captured image still requires manual visual review.

The fixture manipulates two real retail quest IDs through engine APIs. Displaying
their text and changing their state does not mean their actual in-world tasks
were completed through NPC dialogue or actions.

`tests/integration_campaign.py --object-fields` includes exact native subsystem
restoration, malformed combined-batch rejection, and campaign save/load transfer
in both OS directions, comparing the restored state checksum. CTest adds golden
bytes, boundary validation and 5,000 mutated subsystem payloads. These supplement
the existing object, transport and whole-engine tests.

Later batches implement host-owned interaction/combat/services, shared
pause/rest/checkpoints, travel barriers and portable shared movies/slides. See
`COORDINATION.md`, `TRAVEL.md`, `MEDIA.md` and current `STATUS.md` for their
controlled acceptance scope. Later dialogue/service, timer recovery and ending
evidence is indexed there; the older coverage suggestions in this guide are not
additional feature requirements. Linked ordinary campaign integration remains
open. Windows execution is through Wine rather than an actual Windows system.
