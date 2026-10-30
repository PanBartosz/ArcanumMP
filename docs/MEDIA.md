# Shared story presentation (protocol 22)

The implementation batch adds portable Bink video/audio playback, shared
cutscenes and narrated slides, and coordinated campaign endings. Acceptance
results are recorded separately in `STATUS.md`; implementing these paths does
not establish a complete campaign playthrough.

The host executes the retail story script once. A native movie or slide call
publishes a presentation descriptor to every connected traveller. All machines
open their own retail assets before playback starts. Native simulation and
action deadlines stay paused while sockets, acknowledgments, heartbeats and
positional updates continue. Every traveller finishes or agrees to skip the
current presentation before the host script continues. Escape or a click adds
that player's skip vote. Movies marked unskippable retain that behavior. A
bottom caption shows the shared vote and completion counts. Narrated slides
retain their native images and speech.

The service belongs to the campaign runtime, so it remains available while
gameplay is drained for map transfer. Both movies around a native teleport are
shared, and the host applies its game-time cost once. The native ending
selection script chooses the slides; replicas present the selected slide IDs
without executing the selection script. A final shared marker releases every
machine to the main menu. Whole-party death waits five seconds for possible
revival before selecting the native death sequence. A single dead traveller
does not end the campaign.

Missing or undecodable media fails the shared sequence and pauses the session
with an error. It cannot silently advance just the host's story. This is not
live connection recovery; use the most recent cooperative checkpoint.

## Wire format

Messages 64 (host view) and 65 (owner acknowledgment) are independent of the
per-map gameplay epoch. The 288-byte big-endian view contains sequence,
revision, kind, phase, participant/ready/done/skip/failure masks, movie flags,
slide/end ID, audio track, and a zero-padded 260-byte local movie path. Paths
must begin `movies/` or `movies\`, end in `.bik`, and contain no traversal or
drive prefix. Slides and end markers have no path. Immutable descriptors,
monotonic progress, connection-bound acknowledgments and a fully acknowledged
completion prevent a following cutscene from replacing a pending one.

Preparation expires after 30 seconds and playback after 30 minutes. The host
retries the current view and clients retry acknowledgment every 100 ms. A
completed viewer counts toward a collective skip. No machine reads another
player's media file or receives proprietary movie bytes over the network.

## Decoder build and license

`tools/build_movie_decoder.py` builds FFmpeg **9.0.2** from the official
[release archive](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz). The pinned
SHA-256 is `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`.
The archive's detached signature was verified against release key fingerprint
`FCF986EA15E6E293A5644F10B4322F04D67658D8` before pinning it. Only local Bink
demuxing/video/audio, pixel scaling and audio resampling are enabled; network,
GPL, nonfree and version-3 components are disabled. The five FFmpeg libraries
are shared libraries under LGPL-2.1-or-later. The game and its own wrapper
remain under the repository's Sustainable Use License.

Prerequisites are Python 3.12+, make, a C compiler, and llvm-mingw for Windows.
The script verifies downloaded bytes, records configure options, and retains
the unmodified source archive, build manifest and LGPL license under
`build/deps/ffmpeg-{linux,windows}/share/arcmp-ffmpeg/`.

Follow [FFmpeg's license guidance](https://ffmpeg.org/legal.html) when packaging:
ship those notices and exact source archive with the replaceable shared
libraries, preserve dynamic linking, and include source/build instructions.
Linux drop-in bundles include their decoder libraries. Installers and
real-Windows testing are out of scope. Do not replace this dependency with a system FFmpeg
build whose enabled codecs or license configuration differ.

`arcmp-movie-probe` decodes complete local files and reports frame count,
sample count, audio energy and a sampled video digest. `--track=1` selects the
alternate track of `02112.bik`. The opt-in engine harness normally skips native
intro movies after three actual decoded frames, using a collective vote in
co-op; that accelerates unrelated fixtures without changing normal gameplay.
`runtime-story` disables that shortcut after campaign entry and exercises
real-time playback, delayed group skip, EOF, narrated slides and transports.

`runtime-ending` and `runtime-defeat` use the native ending selection script or
actual whole-party death, then verify shared slide completion, the terminal
marker and the native menu reset. Their short movie prelude is a UI fixture,
not an added campaign cutscene. They also capture story captions with the
explicit white native menu font. SDL_mixer release-3.2.2 receives a small,
reproducible CMake source fix freeing converted empty ID3v1 tags; metadata
parsing and narration playback remain enabled.

## Shared native fades

Protocol 32 adds presentation kind 3 for authoritative script, scheduled fade and
teleport effects. It uses the existing 288-byte view: ID is RGB (0–0xffffff),
flags bit 0 selects fade-in and bits 8–23 carry 0–1,024 steps, track is duration in
milliseconds (0–60,000), and path is empty. Zero steps retain the native immediate
reset behavior. Movie, slide and ending formats keep their previous bounds.
Every owner prepares, executes and acknowledges the fade through the existing
campaign-lifetime barrier. Fade input cannot request a skip.

The renderer now honors fade duration while pumping the shared presentation's
network callback. A network failure releases its local fade so the error UI can
be read. Local interface/slide fades retain their local path; authoritative scene
fades use `gfade_run_campaign`. A local-PC teleport no longer runs its outgoing
fade twice. Shared clocks/action deadlines stay frozen throughout the barrier.

## Original campaign finale and visual capture

`runtime-finale` connects the retained campaign through Arronax, the good-route
castle guards and their native death gate, the original chamber ground triggers,
Kerghan's full philosophical Device argument and compiled farewell, native movie,
ending selection, shared terminal marker and menu/party teardown. It can run the
entire legacy chapter batch from 124 or resume a retained later checkpoint.
The harness chooses only offered original options. Persuasion and Device checks
remain in the native interpreter; original quest/story flags are not injected.

Captures now require the selected slide to be actually displayed during the
PLAYING phase. The outer ending UI is already active while a slide is preparing,
so that flag alone can capture a setup or previous frame. `slide_ui_coop_current`
reports the currently drawn native slide and resets when its local window closes.
The original selection script chooses retail slide 46 (`Credits2.bmp`) first on
this route. The Windows ledger confirms IDs 46, 20, 33, 39 and 44; its guarded
capture matches the selected credits asset. The earlier Linux credits frame is
consistent with that content, but predates the stronger displayed-ID guard.
It is not evidence of a rendering defect. Chooser/observer, guarded slide and
native menu frames are reviewed in `evidence/legacy-checks/visual-review.json`.

The native Nasrudin history is 140 seconds. Stage interaction deadlines exclude
measured presentation pump time; unrelated focused continuations collectively
skip already-observed movies after a short real playback. Normal campaign media
and native unskippable behavior are unchanged.

## Alternate combat Device ending

The original combat script 2862 first rejects Device use on the dragon without
consuming the authentic item. Its shell branch plays original movies 3 and 5,
then ends the game with slides. It does not set peaceful dialogue flag 2674.
Native movie presentations carry ID 0 plus their resolved path, so the acceptance
oracle reads entry 3 from the installed module's `movies.mes` and observes that
actual path (`91568.bik` in this installation).

Linux acceptance retains the original fight and shell checkpoint in
`evidence/combat-original-entry-v36/linux-host`, then the focused stage-213
continuation in `evidence/combat-device-tail-v36/linux-host`. Windows-host stages
207–213 pass in `evidence/combat-windows-complete-v36/windows-host`. All four
engines acknowledge the movies, five actual displayed slides, terminal marker
and native menu/party reset. Native pre-ending archives agree within each run.
The original enemies, forms and Device are retained; controlled adjacent chamber
placement and advanced characters remain explicit limits. This is not an organic
castle traversal or a complete campaign acceptance run.
