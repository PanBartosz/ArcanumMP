# Native gameplay film production

These are media-production helpers, not additions to the game or its test suite.
They capture the unmodified native development binary and assemble an editorial
trailer. They require an existing prepared `runtime/isolated-host`, its original
game assets, the debug-symbol native build, GDB, FFmpeg, NumPy, SciPy, Pillow and
the local FreeSerif/Liberation fonts used by the edit.

`capture.py` accepts an explicit `--binary` and fresh `--runtime-prefix` to preserve earlier media runtimes. It copies the known asset manifest into disposable `runtime/showcase-*`
directories, linking immutable archives/media. GDB locates the TIG framebuffer
once; an ancestor recorder reads it through `/proc/PID/mem` at 30 fps and streams
it to FFmpeg. The selected takes have no reported recorder lateness. This
records the game's software framebuffer, not the user's desktop or microphone.

For continuous camera shots, `ARCMP_SHOWCASE_DIRECT_FRAMES=1` records exactly
one completed native draw into a FIFO read by FFmpeg. This avoids asynchronous
sampling and partial draws. `showcase_pan` moves the camera by native pixels
each frame in one continuous call; it does not stop in GDB or step whole tiles.
These shots use frame-index markers rather than wall-clock offsets, since
location setup between shots intentionally produces no recorded frames.

The optional `pump.c` library is loaded only into the disposable filming
processes. It calls native game/event/render functions and, for directed MP,
normal session polling/runtime updates. It is never linked into a drop-in.
GDB scratch variables deliberately avoid hardware register names such as `$pc`
and `$cs`. No game-clock acceleration, fabricated frame interpolation or AI
gameplay frames are used.

The directed multiplayer scripts prepare equipment/stats/positions and native
enemies for filming. Four actual TCP-connected engine processes handle ordinary
owner movement, attacks, spells and pings. The solo scripts prepare inventory,
learned actions/recipes and locations to show the actual UI controls clearly.
These staged conditions are documented in the delivery and end card.

## Capture

From the repository root, create `evidence/showcase/` and compile:

```sh
cc -shared -fPIC -g -O2 tools/showcase/pump.c -o evidence/showcase/pump.so
```

Examples used for the selected takes:

```sh
python3 tools/showcase/capture.py runtime-magic magic-final --seconds 145
env ARCMP_SHOWCASE_LOG=1 ARCMP_PARTY_UI=1 ARCMP_HUD_UI=1 ARCMP_MODERN_UI=1 python3 tools/showcase/capture.py runtime-combat combat-hud-final --seconds 125
env ARCMP_SHOWCASE_DIRECTOR=1 python3 tools/showcase/capture.py runtime-magic showcase-party --seconds 115
env ARCMP_SHOWCASE_DIRECTOR=1 ARCMP_SHOWCASE_CLEAN=1 python3 tools/showcase/capture.py runtime-magic cinematic-combat --seconds 100
python3 tools/showcase/capture.py dialogue-semantics native-features-final --players 1 --stage stage.py --seconds 250
python3 tools/showcase/capture.py dialogue-semantics details-world --players 1 --stage stage_details.py --seconds 150
python3 tools/showcase/capture.py dialogue-semantics display-controls --players 1 --stage stage_options.py --seconds 30
env ARCMP_SHOWCASE_DIRECT_FRAMES=1 python3 tools/showcase/capture.py dialogue-semantics smooth-pan-v2 --players 1 --stage stage_pan.py --seconds 70
```

Run MP takes sequentially: they reuse the same four disposable runtimes. The
solo takes share runtime4 and must also be sequential with one another. The
retained scripts/metadata under `evidence/showcase/takes/` identify each recorded
take. Development scripts can evolve after filming; the edit refers to retained
raw videos, not a claim that a new capture will have identical wall-clock timing.

The soundtrack and selected native SFX were copied from the locally installed
virtual game repository through `tig_file_copy`; the exact extraction script is
retained as `evidence/showcase/extract.gdb`. Their loose source media is ignored
and is not bundled in the public delivery or committed to git. Music excerpts
are credited to Ben Houge, original assets to Troika/Sierra.

## Edit and delivery

```sh
python3 tools/showcase/render.py
python3 tools/showcase/sound.py
python3 tools/showcase/finish.py
```

Set `ARCMP_SHOWCASE_DELIVERY` to an absolute output directory for all three edit
commands to export a new revision separately. Revision 2 uses
`dist/showcase-v2/`, retains the edit/audio lengths, replaces the camera pans
with direct completed-frame takes and switches the uneven Fireflash take to
the smoother matching host footage. Retained native sprite animation is not
interpolated or sped up. `evidence/showcase/cadence-v2.json` records motion
cadence in addition to the ordinary final video/audio stream checks.

`render.py` contains the edit decision list, native source ranges/crops, captions,
four-view timestamp alignment and brass/iron typography. It caches completed
segments and produces both picture edits. `sound.py` adds original-score cues,
native SFX and restrained transition Foley, then normalizes the mix and muxes
stereo AAC. `finish.py` makes the small 720p copy, thumbnail, SRT captions,
credits/posting text, hashes and full-stream media checks. Final outputs are in
`dist/showcase/`; source takes, logs and intermediate art remain ignored.

The new game code's accepted campaign/network evidence is reused. Filming and
export checks do not require installers, Windows VMs or real-Windows testing.

## Public revision 3

The current source uses `ARCMP_SHOWCASE_WORK` and `ARCMP_SHOWCASE_DELIVERY` to
retain a separate edit and export revision. `stage_mp_v3.py` records four actual
TCP owners in Tarant and Tulla, with independent paths and equipped characters.
Use `ARCMP_SHOWCASE_MP_STAGE=stage_mp_v3.py`, `ARCMP_SHOWCASE_DIRECTOR=1`,
`ARCMP_SHOWCASE_DIRECT_FRAMES=1` and an explicit `ARCMP_SHOWCASE_PUMP` library.
The solo UI, details, options and scenery scripts also support completed-frame
recording. The new drag helper holds the native mouse button throughout the
gesture; merely calling mouse message filters was insufficient while pumping.

Revision 3 uses two CC BY 4.0 recordings, "Arcadia" and "Five Armies" by Kevin
MacLeod (incompetech.com), with full credits and source links in the delivery
README and site credits. Keep the recordings in the ignored revision-3 work
directory, named `Arcadia.mp3` and `Five-Armies.mp3`. No loose original-game
audio belongs in Git or the release. The old score extraction notes above
describe earlier local exports, not the public soundtrack.
