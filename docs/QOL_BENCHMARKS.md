# Protocol 59: QoL and performance sweep

2026-10-30. This pass measures and improves networking, rendering, animation
presentation and UI fidelity. No campaign routes or quest progression were
replayed. Existing campaign evidence remains in [FINAL_READINESS.md](FINAL_READINESS.md).
Update all players together to **protocol 59**; existing saves/settings remain readable.

## Resulting changes

- Shared-world transfers use zlib compression with bounded, exact-length
  decompression, canonical CRC validation and the existing all-peer apply/ACK
  barrier. Incompressible batches remain raw. State capture frequency, host
  authority and script/event timing are unchanged.
- Nearby NPC poses send changes plus a periodic refresh. Each client's unsent
  changes survive queue pressure; the scan rotates through crowded scenes.
  Player poses enter the queue before cosmetic NPC packets.
- Repeated samples of the same position no longer restart an unfinished visual
  blend. Unchanged visual pixels and unchanged zoom surfaces avoid redraw work.
  Covered dirty rectangles are composed once. Visibility/status changes still
  redraw stationary actors. Displaced replica sprites restore their actual old
  footprint directly, even when it lies outside their authoritative tile.
- Window copies invalidate the complete destination rectangle. Screen-edge
  blits preserve the source alignment; fractional-scale clipping uses the
  original SDL mapping instead of rounded intermediate rectangles.
- Font measurement now matches actual line baselines and per-line scaling.
  Previously, a measurement-sized text buffer could lose its last line even
  though a larger buffer displayed the same text completely. Hover help sizes
  to its text, stays on screen and avoids covering the pointer.
- Native shortcut captions, custom controls and hover hints follow saved key
  bindings. Input routing and companion shortcuts are unchanged.
- Player drop-ins use `-O2 -g` on Linux and Windows. Debug symbols and assertions
  are retained. Optimized builds also exposed overlapping/bounded text-copy
  warnings in session status/chat; those copies now use explicit bounds and
  overlap-safe movement.

## Network measurements

Four actual Linux engines, with a staged crowd of 64 stationary NPCs. Separate
phases use ordinary owner movement requests and native self-targeted spells.
The existing capture pump runs the normal network/runtime/game/render functions
at 30 Hz. TCP relays count delivered protocol frames, including their headers.
Values below are **total host traffic to all three clients**, excluding startup.
Each steady window is six seconds; this is a controlled load, not ordinary-party
balance or an Internet matchmaking benchmark.

| Phase | Previous drop-in | Protocol 59 | Reduction |
| --- | ---: | ---: | ---: |
| Idle crowd | 336,391 B/s | 26,722 B/s | 92.1% |
| Owner movement | 334,705 B/s | 26,693 B/s | 92.0% |
| Native spell effects | 413,089 B/s | 39,274 B/s | 90.5% |

A separate four-engine run adds **50 ms one-way latency**, deterministic jitter
of ±10 ms and a **64 KiB/s limit per client per direction**. All four engines
remain in play and execute movement/spells. Remote movement is observed after
about 413 ms, including native movement startup and roughly 100 ms controller
sampling. That number is not a pure network RTT measurement. The scenario's
delivered host traffic is 25–34 KB/s total across three clients.

## Rendering measurements

Same machine: AMD Ryzen AI Max+ 395, x86-64 Linux. Native software framebuffer,
SDL dummy/software backend, vsync disabled. Timings use `CLOCK_MONOTONIC` inside
the game process; debugger round trips are outside the measured region.
Each case warms its assets, then measures 240 frames at the crash site or 120
frames per scenery case. Pans move the actual native camera and redraw the view.

These are **render/composition/present service times**, not measured monitor FPS.
The previous delivered build is Debug; the new delivery uses `-O2 -g` without
`-DNDEBUG`. Results therefore include both source fixes and compiler optimization.
The separate matching-Debug comparison isolates the unchanged-view fix: median
150% zoom idle cost drops from 14.36 ms to 0.33 ms before enabling optimization.

| Scene / camera | Previous median | New median | Previous p95 | New p95 |
| --- | ---: | ---: | ---: | ---: |
| Crash site, pan at 100% | 3.59 ms | 1.71 ms | 5.09 ms | 2.22 ms |
| Crash site, pan at 60% | 24.15 ms | 6.00 ms | 25.27 ms | 7.66 ms |
| Crash site, idle at 150% | 14.36 ms | 0.16 ms | 14.90 ms | 0.23 ms |
| Tarant, pan at 60% | 27.81 ms | 7.19 ms | 34.35 ms | 10.61 ms |
| Qintarra, pan at 60% | 32.98 ms | 8.78 ms | 34.46 ms | 9.60 ms |
| Tulla, pan at 60% | 26.08 ms | 5.51 ms | 27.74 ms | 5.81 ms |
| Vendigroth, pan at 60% | 20.77 ms | 3.95 ms | 23.44 ms | 4.23 ms |

Area views are loaded directly for static scenery and camera checks. They do not
establish a new campaign playthrough. Native frames from all locations were
retained for visual review.

## Fidelity, animation and correctness

Exact pixel comparisons cover partial window copies, left/top screen clipping,
scaled clipping at 50%, 75%, 125%, 150% and 200%, overlapping translucent windows
after movement, stationary replica visibility changes, and measurement-sized
versus unconstrained text rendering.
All final comparisons have **zero differing pixels**. The earlier build differs
by 1,223 pixels in the text case and 3,200 pixels in the partial-copy case, with
larger discrepancies in clipped scaling. The alpha-move reference already
passed before and continues to pass.

A native duplicate-sample test sends the same target while a movement blend
is in progress. After 60 ms the old build retains a 27–28 pixel correction;
the corrected build has completed it. This checks the real interpolation state,
not interpolated video footage or a raised native sprite animation rate.

The UI sweep covers 800×600, 1280×720 and 1920×1080, 150% text, all expanded
tool tabs, party help, rebound shortcuts, clipping and layered opacity. A
whole-engine ASan/UBSan run passes the same native checks. Leak detection is
disabled only for the debugger-driven visual process because of ptrace; the
portable sanitizer tests use their normal leak checks.

Validation also includes all **30 portable tests**, four targeted sanitizer
tests, compressed and raw 600 KB world transfers, stalled-peer ACK ordering,
bad CRC/deflate/length rejection, and 24-step four-engine TCP combat with both
Linux and Windows/Wine hosts. The mixed group uses optimized Linux, sanitized
Linux and two Windows executables under Wine. These are scoped compatibility
checks; no real-Windows or campaign acceptance gate was added.

## Reproduce or inspect

The optional tools require GDB, a C compiler, Python, Pillow and NumPy, a prepared
content runtime and the debug symbols retained in the game binary:

```sh
python3 tools/benchmark_qol.py --binary build/game-opt/upstream/arcanum-ce/arcanum-mp \
  --output evidence/my-render
python3 tools/benchmark_qol.py --binary build/game-opt/upstream/arcanum-ce/arcanum-mp \
  --areas --output evidence/my-scenery
python3 tools/benchmark_qol.py --binary build/game-opt/upstream/arcanum-ce/arcanum-mp \
  --visual-only --require-clean --geometry 800x600 --output evidence/my-visuals
python3 tools/benchmark_qol_network.py --binary build/game-opt/upstream/arcanum-ce/arcanum-mp \
  --delay-ms 50 --kib-per-second 64 --output evidence/my-network
```

Every invocation creates new private writable runtimes and refuses to overwrite
them. Retail assets remain linked/read-only; player runtimes and saves are not
used. See [building optimized drop-ins](BUILDING.md#optimized-drop-ins).

Raw reports, native frames, logs, failed diagnostic setups and the frozen
previous executable remain local under `evidence/qol-2026-10-30/` and
`build/qol-baseline/`. The comparison manifest is `acceptance.json` in that
evidence directory. Only the named final runs enter the figures above.
