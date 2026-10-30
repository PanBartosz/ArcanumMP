# Cooperative map sectors

Protocol 27 keeps a 3×3 native sector neighborhood around every registered PC
loaded on the host and replicas. Overlapping neighborhoods share pins; four
separated owners can retain 36 sectors. Movement acquires the next complete
union before releasing the previous one. Pin ownership ends with the gameplay
epoch, before shared loading, reconnection or map teardown.

The native cache grows from 16 to 128 entries before pinning. Growth preserves
loaded objects and sector history. The object tracker retires evicted static records without treating eviction as
destruction; reloaded records are published again. Retired GUID deletions also
leave the lookup, keeping tracking proportional to native live objects.
Other regions retain normal native eviction
and private DIF persistence. A non-evicting native save flush writes pinned
regions too. Evicting flushes preserve held regions. The eviction loop now moves
only the remaining indexes, and tile-list reset clears its entire difference
mask. These are engine lifecycle changes, rather than additional game assets.

World batches now include canonical state for loaded sectors: all 4,096 tile art
IDs, the 4,096-bit tile blocking mask, whole-sector blocking, town-map metadata,
aptitude adjustment, lighting scheme, sound-scheme settings, sector script and
tile-script attachments/counters. Unchanged records are omitted; a region
reloaded by the host is published again. Receivers validate the complete sector
section and pin its regions before resolving positional object identities.
Object, campaign and sector state cross the same applied-world ACK boundary.
Scripts and simulation still execute only on the host.

Native difference tracking is updated on installation, so replicated tile,
blocking and script edits survive eviction and normal saves. Removed tile
attachments retain zero-script tombstones for the native difference writer.
The canonical record excludes memory addresses, render caches, local timestamps
and native dirty flags.

Protocol 28 adds structural roof art and complete static-light lists. Roof
fill/fade bits, light animation phase, ambient-derived RGB, handles and palettes
remain local presentation state. Replacements prepare all new lights before
installing the batch, preserve object-attached lights, and remove/replace only
static lights. Each owner rebuilds roof cutaways from their own PC after loading,
sector changes and movement. Rendering resolves that PC's sector lighting scheme
at the shared hour, then restores the authoritative scheme/colors before game
simulation or capture resumes. Script custom colors are preserved in a region
using the authoritative scheme. Protocol 29 adds local sector ambient/music
selection with canonical script/combat priority; see `ENVIRONMENT.md`.

Native roof DIF persistence is a no-op; native light DIFs assume a fixed base
list. Cooperative saves therefore supplement them with per-sector `.acsector`
files under the native save's maps subtree. Each file contains canonical `ASC2`
state and a CRC32. Loading validates length, schema, checksum and sector identity
before installing roof/light and regional sound state; other fields retain
native DIF loading. Native sound DIF read/write routines are empty.
The loader uses the requested sector ID explicitly: the cache assigns
`Sector.id` only after its loading callback returns.
Saving uses a temporary file and rename. The files travel in existing snapshots
and survive eviction and normal save/load. Ordinary single-player saves retain
the original path. Older cooperative saves without these files load their
native sector data. This changes current protocol peers, not retail assets.

## Format

`ACW3` has a 24-byte header: magic, map, object-upsert count, object-deletion
count, story byte count, sector byte count. All counts are u32, big-endian.
Object upserts and deletions precede optional story bytes, then sector bytes.
The complete world batch remains bounded by 16 MiB.

A sector section starts with a u32 record count, 1–128, followed by that many
u32-length-prefixed `ASC2` records. Duplicate coordinates and out-of-map regions
are rejected. A record has these u32 fields after its four-byte magic:

| Offset | Field |
| --- | --- |
| 4, 8 | Sector x, y |
| 12, 16, 20 | Town map, aptitude, lighting scheme |
| 24, 28, 32 | Sound flags, music and ambient schemes |
| 36, 40, 44 | Sector-script flags, counters, number |
| 48, 52 | Tile-script count and whole-sector blocked (0/1) |
| 56 | Static-light count |
| 60 | 4,096 tile art IDs |
| 16,444 | 128 blocking-mask words |
| 16,956 | 256 structural roof art IDs |
| 17,980 | Tile-script records: tile, flags, counters, number |
| After tile scripts | Static lights: tile, signed offset x/y, flags, art, RGB |

Tile-script entries are strictly increasing by tile index, limited to 4,096.
A static light uses six u32 fields (24 bytes), limited to 1,024 per sector.
Flags retain off/dark, indoor/outdoor tint and explicit non-animation behavior.
Offsets are signed 32-bit values bounded to ±1,048,576. Ambient lights use zero
RGB; custom colors use 24-bit RGB. Roof art excludes local bits `0x3000` and rejects piece indices beyond 12.
Record length is exactly 17,980 + 16 × script count + 24 × static-light count.
Unknown schemas, duplicate
or out-of-range tile IDs and trailing bytes are rejected without changing the
decoder output. Positional object IDs retain native map/location/temp-ID rules.

## Coverage and remaining work

The portable sector fixture covers endian layout, maximum attachments,
zero-script entries, malformed sizes/flags/order and 20,000 mutations with
canonical re-encoding. Native object-batch and save fixtures exercise the new
world format with retail data. `STATUS.md` records accepted native integration
runs and their actual scope.

This does not establish all campaign sector lifecycle behavior. Arbitrary
roof/light script combinations and regional audio transitions beyond the
combined fixture,
long exploration sessions, arbitrary scripted object references across eviction
and wider story-region transitions still need acceptance. Parties still share
one active map and use the shared transition barrier when changing maps.
The earlier wilderness black terrain capture remains an open visual finding
until a corresponding native scene has been reviewed with these changes.

Protocol 29 propagates sector save failures through eviction, map saving and
map replacement. A failed sector writer retains that resident sector. Local
regional audio is documented in `ENVIRONMENT.md`.

Cold recovery also accepts the bounded native history of a larger party cache.
A fresh process starts with sixteen slots; loading up to 256 saved history entries
now grows the cache before installing them. Cooperative native DIF loads retain
extinct static records as hidden tombstones, preserving file-index continuity
and permanent identities for future saves and world baselines. Ordinary native
single-player loads retain their original extinction cleanup.

DIF metadata is decoded from its own flags independently of the base sector's
version. Older terrain templates can acquire newer lighting, blocking, aptitude,
town-map and script metadata. Skipping these bytes previously misread a saved
lighting ID as an object extent, shifting identities and losing the final
records. Loaded scalar differences keep their native changed bits for the next
save. The `sector-identities` engine diagnostic inspects a saved sector's native
indices and permanent IDs without changing the retail installation.

## Bounded long-campaign save growth

Protocol 30 writes compact `ACV1` supplements instead of complete ASC2 records:
1,052 fixed bytes plus 24 per static light, followed by the existing CRC. The
header is magic, X/Y, sound flags, music, ambient, light count (u32 each), followed
by 256 structural roof IDs and the light list. Existing ASC2 sidecars remain
readable. Each resident sector retains its pre-supplement canonical visual/audio
baseline; unchanged sectors create no sidecar, and returning a sector to its
baseline removes its obsolete sidecar. Native light-DIF imports preserve a
supplement conservatively because their base has already been altered.

Merely assigning a cooperative static object's reproducible positional identity
no longer marks it changed. Actual object field changes still produce native
DIF records. This prevents untouched exploration from generating object saves
and full-sector supplements solely because a world baseline was published.

The expanded `runtime-sectors` scenario checks 256 untouched loads in both host
directions and requires zero sidecars for those regions, alongside its native
mutation, eviction, save/load and boundary movement stages. The whole-engine
object probe changes roof/light/sound state, reloads compact and legacy files,
then restores the baseline and requires the old supplemental file to disappear.
The compact property record is 1,080 bytes with CRC. These controlled checks do
not establish unlimited exploration or every retail sector mutation.

## Empty script queries and sector lifetime (protocol 50)

Nearby scenery/portal queries acquire sector locks even when their result contains
no objects. Script cleanup previously called `object_list_destroy` only for a
nonempty head, retaining these locks indefinitely. Cleanup now destroys every
collection result. Empty native constructors and absent script anchors also use
the normal list initialization path, balancing list accounting on early returns.

The original archives contain 91 scenery/portal loop instructions, including
Bates guards, Wheel Clan entrance, Nasrudin's tomb and Arronax. Their presence
shows campaign relevance; it does not prove every invocation yields an empty
result. The focused native check runs all 18 collection selectors for each of
four owners, including repeated empty locked and nonempty results, and absent
attachees. Every query must return the global sector-reference count to its
baseline. ASan/UBSan passes; Linux/Windows targets build. No surrounding quests
or already-accepted network matrices are replayed. Exact evidence:
`evidence/script-regions-v50/acceptance.json`.
