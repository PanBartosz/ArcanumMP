# Shared lighting and campaign audio

Protocol 24 appends an explicit environment view to the authoritative story
state. Replicas receive the current lighting scheme, map default, hour and
resulting indoor/outdoor RGB colors. Native palette and sector invalidation runs
only when colors change. Applying a clock-only update neither schedules a
lighting event nor repeatedly rebuilds the ambient palettes.

The host still executes the hourly lighting callback and script/map actions.
Clients present the results without running their stopped campaign schedulers.
Custom colors are carried separately from the scheme/hour, so a host override
is not silently replaced by a client's local scheme table.

The same view carries the active music/ambient scheme pair, deferred post-combat
and post-song schemes, combat music state and the host's tension-track selection.
Scheme pairs are canonicalized by numeric ID; native slot order has no gameplay
meaning. Content lookups reject unknown scheme IDs before world installation.
Local volume settings and sound-lock counts stay local.

Replica `gsound_ping` now advances background music and ambient presentation.
It does not advance game clocks, AI, scripts or timer queues. Random ambient
clips remain local texture; positional world sounds retain their acknowledged
host event stream. Only the host decides when a transition song restores the
previous scheme. Repeated world updates keep existing music streams running.
Track playback positions are local, rather than sample-synchronized.

Combat music uses native streams with allocation-instance checks before cleanup,
so a finished tension clip cannot cause a reused sound slot to be destroyed.
Completed streamed clips now release their native slots. Scheme music also
checks allocation instances before polling or stopping a stream, and retries a
loop if temporary channel pressure prevented its initial creation.
Nested native sound locks suspend/resume the local music without erasing the
deferred post-combat scheme. Loading a native combat save recreates its looping
combat track; the retail save format never recorded the transient tension choice.

Cooperative saves add optional `Save/Current/arcmp-environment.bin` inside the
existing native archive: 92 canonical `ACE2` environment bytes plus a big-endian CRC32.
Legacy 52-byte `ACE1` sidecars remain accepted at the save boundary.
The archive parser accepts this one optional filename, rejects duplicates and
validates its length, schema and CRC before writing a received archive.
It restores custom colors, scheme metadata and combat presentation after the
native modules finish loading. Older cooperative saves and ordinary native
saves retain the original module fallback. Ordinary single-player saves omit
the sidecar. No `data.sav` module layout or retail asset is changed.

## Independent regional playback

Protocol 29 separates the two canonical campaign scheme slots from two local
presentation slots. Each registered PC hears its own sector's music and ambient
pair while canonical map defaults, shared combat music and pending transition
songs retain priority. An empty sector pair inherits the canonical schemes.
Regional handles and playback phase never enter world state or native saves.
Sector scheme settings persist in the existing supplemental `.acsector` files;
the original sector sound DIF routines are empty.
Nested local locks stop both sets and resume the applicable policy on unlock.

Native scripted sound-scheme actions record each persistent member's current
sector. The script overrides those members until they leave that sector; the
host consumes each member's override bit when they leave. Returning does not
reactivate it. A later script action records fresh origins. Map-default scheme
selection clears the scripted policy. This avoids a local sector immediately
replacing narrative music while allowing separated owners to resume exploration.
The revision, mask and origins cross world/save/rejoin boundaries. Absent members
have zero origins, and a nonscripted state has an empty mask.

## Wire layout

`ACS4` retains the earlier story/combat/magic positions and appends `ACE2` after
the variable-length maintained-spell view. All integer fields are big-endian.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | `ACE2` |
| 4 | 4 | Active lighting scheme ID |
| 8 | 4 | Map default lighting scheme ID |
| 12 | 4 | Lighting hour, 0–23 |
| 16 | 8 | Indoor and outdoor 24-bit RGB values, u32 each |
| 24 | 8 | Active sound scheme pair, u32 each |
| 32 | 8 | Post-combat sound scheme pair |
| 40 | 8 | Post-song sound scheme pair |
| 48 | 1 | Flags: bit 0 combat music, bit 1 pending song restore, bit 2 scripted override |
| 49 | 1 | Combat tension track 0–6; zero means no transient selection |
| 50 | 2 | Reserved zero bytes |
| 52 | 4 | Scripted-audio revision |
| 56 | 1 | Persistent campaign-member override mask, bits 0–3 |
| 57 | 3 | Reserved zero bytes |
| 60 | 32 | Four origin-sector X/Y pairs, u32 each |

Pairs are sorted; inactive restore pairs and inactive combat-track metadata
must be zero. Unknown flags, oversized IDs/colors, bad hours and reserved bytes
are rejected before the decoder changes its output.

## Combined native fixture

`runtime-environment` combines day/night and custom lighting, a file-interpreted
native sound-scheme action, combat music and deferred scheme changes, save/load
under a new epoch, nested sound locks, silence and repair of replica corruption.
Seven music stages measure actual post-mix PCM and check that repeated world
updates retain the same native sound allocation. Three stages capture composed
game frames. A short streamed UI clip checks EOF slot release, followed by a
memory-effect PCM check through the same diagnostic path. The fixture chooses
real installed sound schemes and keeps its
small `.scr` file inside the private runtime only.

`runtime-environment-ui` resumes the retained manual save with its original
owner profiles and checks stages 10–14. It avoids repeating day/night and
save/load when only the local HUD changes. Replicated field installation and
local-PC rebinding now refresh health/fatigue gauges, including after native
load; they previously refreshed money/ammunition while leaving stale gauges.

`STATUS.md` records accepted runs. This fixture does not establish every retail
lighting scheme, sound transition, prolonged audio session or narrative region.
Direct script sounds without positional sources and persistent dialogue/reading
history remain separate work. No complete campaign playthrough is claimed.
