# Release preparation

Delivery is matched Linux/Windows drop-in archives with required libraries,
not an installer. Git contains source, tests, documentation and project-owned
UI art. Original game data and local development artifacts stay separate.

## Prepare a build

1. Follow [optimized building](BUILDING.md#optimized-drop-ins). Keep the source revision, protocol and
   dependency configuration consistent for both target builds.
2. Perform validation appropriate to the changed behavior. Reuse unaffected
   [campaign/network evidence](FINAL_READINESS.md) and report its limits.
3. Review and commit the source. The packaging tool rejects a dirty working
   tree unless explicitly allowed for a local smoke package.
4. Build/package from that committed tree. Do not relabel an older executable
   as a newly compiled revision.

```sh
python3 tools/package_release.py --build build/game-opt --target linux --output dist
python3 tools/package_release.py --build build/windows-opt --target windows --output dist
```

Names include the target, protocol and source commit. The packaging tool also
creates extracted staging directories, `package.json` file hashes, a binary
manifest and an archive checksum. It refuses to overwrite an existing staging
directory. Run the target commands sequentially.

Bundles retain executable/decoder libraries, player guides, license notices,
the exact dependency sources and the committed project source archive. Preserve
those parts together. [NOTICE.md](../NOTICE.md) identifies provenance; decoder
details are in [MEDIA.md](MEDIA.md#decoder-build-and-license).

## Local artifacts

`dist/README.md` is the local handoff catalog. It is generated/maintained beside
the archives and is intentionally not part of Git. Current prepared gameplay
builds use protocol 59 and the optimized configuration described in the
[QoL report](QOL_BENCHMARKS.md). The exact source revision is in each archive's
name and `package.json`; documentation-only commits do not create game binaries.

| Local path | Purpose |
| --- | --- |
| `dist/arcanum-mp-*-p59-*` | Latest optimized game drop-ins and staging trees |
| `dist/arcanum-mp-*-p58-0840f41*` | Previous HUD-art drop-ins retained for comparison |
| `dist/archive/` | Older release archives/checksums, kept for rollback |
| `dist/arcanum-mp-linux-x86_64-dev-p57-a9abd61/` | Referenced rendering comparison baseline |
| `dist/showcase-v2/` | Revised showcase/trailer, small copy, cover and credits |
| `dist/showcase/` | Original media exports retained for comparison |

The archive catalog and cleanup audit retain moved paths. An old staging tree
can be restored with `tar -xzf dist/archive/NAME.tar.gz -C dist` or
`unzip dist/archive/NAME.zip -d dist`, from the repository root. Keep any tree
that a runtime or regression comparison still references.

## Public releases

The public project is https://github.com/PanBartosz/ArcanumMP and its player-facing
site is https://panbartosz.github.io/ArcanumMP/ . Version 0.1.0 is the first public
release. Earlier protocol numbers are engineering milestones, not prior public
releases.

For the initial publication, export the reviewed current source into a new local
Git repository and make one initial commit. Keep the original development
repository/history and local evidence intact. Do not push its historical refs.
The root agent-workspace instructions are local and are omitted from the public
snapshot; source, notices, technical documentation and useful source artwork
remain available.

Attach the matching Windows/Linux bundles and checksums to a GitHub Release.
Public names use the release version and platform. Bundles include the exact
public source archive and retain all third-party license/source notices; game
data and local identities are excluded. Verify the packaged executable hashes
against the reviewed native/cross-builds. Record the source correspondence when
publishing a documentation/media-only revision with unchanged game code.

The `site/` directory contains the buildless website source. Publish its completed
files, trailer, captions and continuous layout demo from a separate `gh-pages`
branch with `.nojekyll`. Large video files stay out of the main source branch.
The root README and website should use the public release/page URLs and describe
player-visible features rather than internal wire versions or delivery plans.
