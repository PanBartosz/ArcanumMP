# Build from source

Run commands from the repository root. Git contains the modified engine and HUD
assets; game data is needed to play or run native integration scenarios, not to
compile the source or run the portable session/state tests.

## Prerequisites

- CMake 3.20+, Ninja, C17 and C++17 compilers.
- Python 3.12+ and make for the pinned movie-decoder build and developer tools.
- Git and network access for the pinned CMake dependencies and FFmpeg archive.
- SDL's platform development dependencies for a playable native game build.
- zlib development headers/library for asset-free transport tests (game builds
  use the already pinned zlib source).

On Debian/Ubuntu, a typical Linux development environment is:

```sh
sudo apt install build-essential cmake ninja-build python3 git pkg-config zlib1g-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev \
  libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
```

The Linux game requires a compatible x86-64 glibc environment and system
display/audio support. CMake fetches pinned SDL, SDL_mixer, zlib and fpattern
sources. Their notices remain with the fetched sources and release bundles.
Committed BMP files are sufficient for building the HUD: Python/Pillow and
image generation are not game-build requirements.

## Linux game

```sh
python3 tools/build_movie_decoder.py --target linux
cmake -S . -B build/game -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/game --parallel
```

Output: `build/game/upstream/arcanum-ce/arcanum-mp`, with decoder libraries in
its `lib/` directory. The movie build uses pinned shared FFmpeg libraries;
[media support](MEDIA.md#decoder-build-and-license) records the source checksum,
configuration and notices. Use the project decoder build rather than substituting
an arbitrary system FFmpeg configuration.

## Windows cross-build

Use llvm-mingw on the Linux development host. Set the path to your own toolchain:

```sh
export ARCMP_MINGW=/path/to/llvm-mingw
python3 tools/build_movie_decoder.py --target windows
cmake -S . -B build/windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/windows-llvm.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/windows --parallel
```

Output: `build/windows/upstream/arcanum-ce/arcanum-mp.exe`, with the supplied
decoder DLLs. Existing Windows executable runtime evidence uses Wine.
Upstream mobile/macOS project files are retained as upstream material; this
fork's supported delivery targets are Linux and Windows drop-ins.

## Tests without game assets

```sh
cmake -S . -B build/net -G Ninja -DARCMP_BUILD_GAME=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build/net --parallel
ctest --test-dir build/net --output-on-failure
```

This builds the session/state tests, content probe and `arcmp-net` diagnostic.
`arcmp-net` is a test tool, not a playable game client. Debug builds keep native
assertions enabled. Add `-DARCMP_SANITIZE=ON` to a separate Linux build directory
for ASan/UBSan. The root GitHub workflow runs the asset-free checks on Linux.

## Private development runtimes

Keep the retail installation unchanged and give each game instance its own
writable runtime. This optional helper links immutable data and copies loose
files, configuration and cache locations; it refuses to overwrite a runtime.

```sh
python3 tools/prepare_runtime.py \
  --install /path/to/Arcanum \
  --output runtime/my-game \
  --binary build/game/upstream/arcanum-ce/arcanum-mp
cd runtime/my-game
./arcanum-mp -window
```

Ordinary drop-in players do not need this tool: the game creates its content
fingerprint automatically. See [installation](INSTALL.md).

Native integration tools under `tests/` use private runtimes and original data.
Choose checks relevant to the changed code; consult [workflow](WORKFLOW.md) and
[retained evidence](FINAL_READINESS.md) before repeating a campaign route.
See [release preparation](RELEASING.md) for distributable executables/libraries.

## Optimized drop-ins

Development Debug builds are useful for stepping through code. For player
delivery, use optimized builds with debug symbols and native assertions retained:

```sh
cmake -S . -B build/game-opt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO='-O2 -g' \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O2 -g'
cmake --build build/game-opt --parallel

cmake -S . -B build/windows-opt -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/windows-llvm.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO='-O2 -g' \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O2 -g'
cmake --build build/windows-opt --parallel
```

The Windows command needs `ARCMP_MINGW` and both targets need their decoder
build, as above. These overrides deliberately omit `-DNDEBUG`; optimization
does not disable the checks. [QoL benchmarks](QOL_BENCHMARKS.md) record the
measured difference from the earlier Debug drop-ins.
