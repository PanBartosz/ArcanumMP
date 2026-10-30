# Cooperative positional sound

Protocol 15 adds a presentation service for native world sound calls. The host
still executes combat, spell components, animation goals and sound selection.
Clients receive native sound IDs and map coordinates, resolve their own
content-matched assets, and calculate attenuation, pan and volume for their own
listener. UI sounds, music and voice retain separate local paths. No second
gameplay simulation is started to produce audio.

## Native boundaries and identities

`gsound_play_sfx_at_loc_ex` captures positional starts, including calls from
`gsound_play_sfx_on_obj`. `gsound_move` captures movement. TIG's optional stop
observer distinguishes an explicitly interrupted sound from a naturally finished
one-shot. A naturally completed host one-shot does not truncate a client that
started it later. Native infinite loops publish their stop when the native
handle is released or faded.

The process assigns monotonic 64-bit sound tokens. Native handle indices,
pointers and camera-relative coordinates never appear on the wire. A separate
local TIG allocation generation prevents a delayed stop from destroying an
unrelated local/UI sound that reused the same handle slot. Native sound table
and save layouts are unchanged.

Already-running native positional loops are remembered before a gameplay
service is attached and introduced when that service starts. New starts wait
until every participant's START message has been queued. Finishing/travel seals
production, stops published loops on replicas, drains ordered events and waits
for acknowledgements before the final pose barrier. Disconnect/failure silences
network-owned listener loops; destruction releases only matching local handle
generations.

## Ordered transport and bounds

`audio.c` has no engine dependency. Type 49 carries one immutable event batch;
type 50 acknowledges its revision. The host publishes another batch only after
all connected participants acknowledge the current one. Client socket callbacks
validate/stage bytes; `poll` validates and applies presentation before ACK.
Identical pending/applied replays never invoke presentation twice. Conflicting
replays, wrong epochs/maps/directions, invalid operations or noncanonical fields
are rejected. Engine validation checks every sound ID, coordinate and token in
the complete batch before starting playback.

Batch header, 16 bytes:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Epoch |
| 4 | 4 | Ordered nonzero batch revision |
| 8 | 4 | Current map |
| 12 | 2 | Event count, 1–36 |
| 14 | 2 | Zero |

Each event, 28 bytes:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 1 | START 0, MOVE 1, STOP 2 |
| 1 | 1 | Native positional size: small 0 through extra large 3 |
| 2 | 2 | Native loop count: 0 indefinite, 1 once |
| 4 | 4 | Native sound ID, 0–INT32_MAX |
| 8 | 8 | Stable nonzero instance token |
| 16 | 4 | Map X |
| 20 | 4 | Map Y |
| 24 | 4 | Stop fade in milliseconds, at most 60,000 |

START requires zero fade. MOVE requires zero size/loops/sound/fade. STOP requires
zero size/loops/sound/X/Y. ACK is eight bytes: epoch and applied revision.
Integers are big-endian. The largest batch is exactly the 1,024-byte payload
limit. There are at most 1,024 queued events plus one in-flight batch. Unsent
moves for the same token can be superseded; published bytes are immutable.
Starts/stops are retained. Queue overflow or a 30-second pending-batch timeout
fails the adapter and uses the existing resynchronization-required path.

## Playback and diagnostics

The SDL3 mixer compatibility layer previously ignored the native pan value.
It now applies per-channel stereo gains while preserving centred playback, and
converts the native zero loop count to SDL_mixer's signed infinite-loop value.
Failed decodes/plays release the native cache/handle resources.

The explicit test harness can enable PCM energy counters for isolated mixer
groups containing one track each, after gain and stereo pan. The cooked-track
callback in the bundled SDL_mixer revision runs before stereo pan, so it is not
used as evidence of the final channel balance. The group callback only
accumulates counters under a mutex; it does not
run engine code or mutate playback. Ordinary gameplay leaves these callbacks
disabled. These measurements can prove decoded playback and stereo differences
with an SDL dummy device; they do not establish physical speaker/headset output.

`audio-test` exercises the portable codec, four-session delivery, a delayed
fourth peer, exact and conflicting replays, move coalescing, malformed envelopes,
queue/timeout limits and the sealed barrier. `runtime-magic` retains its native
combat/spell phases and adds five audio stages: a shared loop with opposing
listeners, source movement and pan reversal, deterministic listener handle reuse,
muted-host playback with native source handle reuse, and a loop left active for
the final drain barrier. Local UI audio must survive the retired token's stop.
Final ordered-event counts/digests are compared between all four engines.

The accepted `evidence/audio-final/{linux-host,windows-host}` runs each contain
24 base combat stages, sixteen spell phases and five audio stages in four actual
engines. Each direction delivers 133 ordered sound events with identical final
counts/digests across all four processes and zero reported playback failures.
Measured channels reverse between the first two audio stages. The muted host's
third measurement is zero on both channels while all clients produce nonzero
PCM. The fixture also proves source-slot reuse, listener-slot reuse with a
surviving local UI loop, and zero published loops after the finish barrier.
`review-pcm.png` plots these measurements; it is not a recording from speakers.

`audio-first` is retained as a failed placement diagnostic: twelve opposing
tile offsets produced 960 pixels of separation, outside the native large-sound
range. Six offsets produce an audible hard-pan case. `audio-pan-range` retains
the subsequent failed measurement diagnostic; its cooked-track counters ran
before stereo spatialization. The isolated-group measurements in `audio-final`
are the accepted PCM evidence.

## Remaining acceptance

The entire campaign remains unaccepted. Listener-local ambient schemes and
combat music now have explicit state and local presentation in `ENVIRONMENT.md`.
Host `-nosound`/unavailable-device loop lifetimes, native proximity optimizations
for distant scenery, wider ambient coverage, voice/subtitles and dialogue/service
audio need broader work. Muting a functioning host device is distinct from
disabling its native sound-handle backend. Active-loop disconnect/fault sessions, save/recovery
with active loops and actual speaker/headset output also
remain open.

## Separated-player admission (protocol41)

Native autonomous animation and repeating animation sound admission now follows
the nearest campaign human on the host. A remote-only nearby listener can keep a
native loop active; its sound still uses each recipient's local attenuation and
pan. Remote human pickups now generate their native positional pickup sound.
Current-build native acceptance remains pending.
