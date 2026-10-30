#!/usr/bin/env python3
"""Create an isolated, writable runtime; leave the retail installation intact.

Only immutable archives and media are linked. Loose files are private copies.
Each runtime owns config, caches and saves.
The content manifest hashes actual asset bytes in canonical path order.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def generated_cache(relative: Path) -> bool:
    parts = tuple(p.casefold() for p in relative.parts)
    # map_preprocess_mobile writes an extensionless, obfuscated mobile-object
    # cache directly inside modules/<module>/maps. Never import or fingerprint
    # those derived files; regenerate them inside the private runtime.
    return len(parts) == 4 and parts[0] == 'modules' and parts[2] == 'maps' and not relative.suffix


def immutable_asset(install: Path, source: Path) -> bool:
    archive = source.parent in {install, install / 'modules'} and (
        source.suffix.casefold() == '.dat' or source.suffix.casefold().startswith('.patch'))
    return archive or source.suffix.casefold() in {'.bik', '.mp3', '.wav'}


def prepare(install: Path, output: Path, binary: Path) -> None:
    install, output, binary = install.resolve(), output.resolve(), binary.resolve()
    if output == install or install in output.parents or output in install.parents:
        raise ValueError("runtime must be separate from the installation")
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"refusing to overwrite existing runtime: {output}")
    required = ["arcanum1.dat", "arcanum2.dat", "arcanum3.dat", "Arcanum4.dat", "tig.dat", "modules/Arcanum.dat"]
    for name in required:
        if not (install / name).is_file():
            raise FileNotFoundError(install / name)
    if not binary.is_file():
        raise FileNotFoundError(binary)
    output.mkdir(parents=True, exist_ok=True)
    assets = [p for p in install.iterdir() if p.is_file() and p.suffix.lower() == ".dat"]
    for root in [install / "modules", install / "data"]:
        for source in root.rglob("*"):
            if not source.is_file():
                continue
            relative = source.relative_to(root)
            if any(part.casefold() in {"tigcache", "players", "save", "saves"} for part in relative.parts):
                continue
            if generated_cache(source.relative_to(install)):
                continue
            assets.append(source)
    manifest = []
    keys = set()
    for source in sorted(assets, key=lambda p: p.relative_to(install).as_posix().casefold()):
        relative = source.relative_to(install)
        key = relative.as_posix().casefold()
        if key in keys:
            raise ValueError(f"case-colliding game asset: {relative}")
        keys.add(key)
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if immutable_asset(install, source):
            target.symlink_to(source)
        else:
            shutil.copy2(source, target)
        # Movies/music do not affect simulation, but are still linked for playback.
        if source.suffix.casefold() in {".bik", ".mp3", ".wav"}:
            continue
        with source.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        manifest.append({"path": key, "size": source.stat().st_size, "sha256": digest})
    canonical = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    content_id = hashlib.sha256(canonical).hexdigest()
    (output / "arcmp-content.id").write_text(content_id + "\n")
    (output / "arcmp-content.json").write_bytes(canonical + b"\n")
    (output / "HighRes").mkdir(exist_ok=True)
    (output / "HighRes/config.ini").write_text("[Main]\nWidth=800\nHeight=600\nWindowed=1\nLogos=0\nIntro=0\n")
    if (install / "arcanum.cfg").is_file():
        shutil.copy2(install / "arcanum.cfg", output / "arcanum.cfg")
    target_binary = output / ("arcanum-mp.exe" if binary.suffix.lower() == ".exe" else "arcanum-mp")
    target_binary.symlink_to(binary)
    if binary.suffix.lower() == '.exe':
        for pattern in ('avcodec-*.dll', 'avformat-*.dll', 'avutil-*.dll', 'swresample-*.dll', 'swscale-*.dll'):
            for decoder in binary.parent.glob(pattern):
                shutil.copy2(decoder, output / decoder.name)
    print(json.dumps({"runtime": str(output), "assets": len(assets), "content_id": content_id, "binary": str(target_binary)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.install, args.output, args.binary)
