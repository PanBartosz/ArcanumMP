#!/usr/bin/env python3
"""Package the self-contained Polish setup; Python is a maintainer tool only."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
INPUTS = {
    'windows': ('innoextract-1.9-windows.zip', '6989342c9b026a00a72a38f23b62a8e6a22cc5de69805cf47d68ac2fec993065'),
    'linux': ('innoextract-1.9-linux.tar.xz', '008efe5011476ccc4aae17c3e22038b5a1bc5c7aad2b9d4d869537bf3874d21f'),
    'source': ('innoextract-1.9.tar.gz', '6344a69fc1ed847d4ed3e272e0da5998948c6b828cb7af39c6321aba6cf88126'),
}


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def dependency(cache, kind):
    name, sha = INPUTS[kind]
    path = cache / name
    if not path.exists():
        with urllib.request.urlopen('https://constexpr.org/innoextract/files/' + name, timeout=30) as response:
            path.write_bytes(response.read())
    if digest(path) != sha:
        raise ValueError('Extractor archive checksum mismatch: ' + name)
    return path


def archive_files(path):
    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as z:
            return {n: z.read(n) for n in z.namelist() if not n.endswith('/')}
    with tarfile.open(path) as t:
        return {m.name: t.extractfile(m).read() for m in t.getmembers() if m.isfile()}


def package(args):
    source = args.source_tree.resolve()
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=source).strip():
        raise ValueError('Commit the reviewed public source before packaging.')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    stamp = int(subprocess.check_output(['git', 'show', '-s', '--format=%ct', 'HEAD'], cwd=source, text=True))
    build = args.build.resolve()
    version = '1.0.0'
    label = 'Windows' if args.target == 'windows' else 'Linux'
    name = f'ArcanumMP-Polish-Setup-v{version}-{label}-x86_64'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage = output / name
    if stage.exists():
        raise FileExistsError(stage)
    cache = args.dependency_cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    dep = dependency(cache, args.target)
    dep_source = dependency(cache, 'source')
    original = archive_files(dep)
    embedded = next(data for n, data in original.items()
                    if n == 'innoextract.exe' or n.endswith('/bin/amd64/innoextract'))
    binary = build / ('ArcanumMP-Polski.exe' if args.target == 'windows' else 'ArcanumMP-Polski')
    if embedded not in binary.read_bytes():
        raise ValueError('The executable does not embed the verified extractor for this platform.')
    stage.mkdir()
    shutil.copy2(binary, stage / binary.name)
    shutil.copy2(source / 'tools/polish_setup/README.md', stage / 'README.md')
    (stage / 'README-PL.txt').write_text(
        'ArcanumMP — prosty import polskiej lokalizacji\n\n'
        '1. Zamknij grę i rozpakuj paczkę narzędzia w jej folderze.\n'
        '2. Umieść Arcanum_UAP2.0.2PL.1.exe obok ArcanumMP-Polski.exe / ArcanumMP-Polski.\n'
        '3. Uruchom ArcanumMP-Polski, poczekaj na Gotowe i uruchom grę.\n\n'
        'Polski zostanie wybrany automatycznie. Każdy gracz wybiera swój język.\n'
        'Nie potrzeba Pythona ani osobnej wypakowywarki.\n'
        'Poprzednia lokalizacja zostanie zachowana w languages/.pl-backup-…\n\n'
        'Eselter 1.7 — pobierz od autora:\n'
        'https://www.moddb.com/mods/spolszczenie-arcanum/downloads/arcanum-uap20pl\n\n'
        'Więcej informacji: README.md. Zachowaj licencje i archiwa źródłowe.\n', encoding='utf-8')
    shutil.copy2(source / 'LICENSE.md', stage / 'LICENSE.md')
    (stage / 'NOTICE.md').write_text(
        '# Polish setup notices\n\nArcanumMP setup code: Sustainable Use License 1.0 (LICENSE.md).\n'
        'innoextract 1.9 by Daniel Scharrer, zlib license: https://constexpr.org/innoextract/ .\n'
        'The unmodified extractor runs as a separate child executable. Its original\n'
        'package, source and dependency notices accompany this utility.\n'
        'The Linux desktop helper statically links SDL3 under the zlib license.\n'
        'Compiler runtime notices are included under licenses/.\n'
        'Third-party Polish game data is supplied separately and is not included.\n')
    for n, data in original.items():
        parts = Path(n).parts
        if 'license' not in parts:
            continue
        relative = Path(*parts[parts.index('license') + 1:])
        target = stage / 'licenses/innoextract' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    runtime = stage / 'licenses/compiler-runtime'
    runtime.mkdir(parents=True)
    if args.target == 'windows':
        toolchain = args.compiler_root.resolve()
        notices = [toolchain / 'LICENSE.TXT', *sorted((toolchain / 'x86_64-w64-mingw32/share/mingw32').glob('COPYING*'))]
    else:
        notices = [Path('/usr/share/licenses/libstdc++/RUNTIME.LIBRARY.EXCEPTION'), Path('/usr/share/licenses/spdx/GPL-3.0-only.txt')]
    for notice in notices:
        shutil.copy2(notice, runtime / notice.name)
    if args.target == 'linux':
        (stage / 'licenses/SDL3').mkdir()
        shutil.copy2(args.sdl_license, stage / 'licenses/SDL3/LICENSE.txt')
    sources = stage / 'source'
    sources.mkdir()
    shutil.copy2(dep, sources / dep.name)
    shutil.copy2(dep_source, sources / dep_source.name)
    files = subprocess.check_output(['git', 'ls-files', 'tools/polish_setup', 'tools/sha256.hpp', 'LICENSE.md'], cwd=source, text=True).splitlines()
    with tarfile.open(sources / 'arcanum-mp-polish-setup-source.tar.gz', 'w:gz') as t:
        for n in files:
            data = subprocess.check_output(['git', 'show', commit + ':' + n], cwd=source)
            info = tarfile.TarInfo(n)
            info.size = len(data)
            info.mtime = stamp
            info.mode = 0o644
            t.addfile(info, io.BytesIO(data))
    manifest = dict(version=version, target=args.target, source_commit=commit,
                    extractor_version='1.9', extractor_sha256=hashlib.sha256(embedded).hexdigest(),
                    author_package_sha256='294a1c05a88c8f910a7d23327a4efebe0605f3026294c0a77c53185828e850f2',
                    third_party_language_assets_included=False,
                    files={p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob('*')) if p.is_file()})
    (stage / 'package.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if args.target == 'windows':
        archive = output / (name + '.zip')
        with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for p in sorted(stage.rglob('*')):
                if p.is_file():
                    info = zipfile.ZipInfo(p.relative_to(stage).as_posix(), time.gmtime(stamp)[:6])
                    info.external_attr = (p.stat().st_mode & 0xffff) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    z.writestr(info, p.read_bytes())
    else:
        archive = output / (name + '.tar.gz')
        def stable(info):
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = stamp
            return info
        with archive.open('xb') as file, gzip.GzipFile(fileobj=file, mode='wb', mtime=stamp, filename='') as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as t:
                for p in sorted(stage.iterdir()):
                    t.add(p, arcname=p.name, filter=stable)
    archive.with_name(archive.name + '.sha256').write_text(digest(archive) + '  ' + archive.name + '\n')
    print(json.dumps(dict(archive=str(archive), directory=str(stage), bytes=archive.stat().st_size, sha256=digest(archive), source=commit)))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--target', choices=['windows', 'linux'], required=True)
    p.add_argument('--source-tree', type=Path, default=ROOT)
    p.add_argument('--output', type=Path, default=ROOT / 'dist/polish-setup-v1.0.0')
    p.add_argument('--dependency-cache', type=Path, default=ROOT / 'build/polish-setup-deps')
    p.add_argument('--compiler-root', type=Path, default=Path('/path/to/llvm-mingw'))
    p.add_argument('--sdl-license', type=Path, default=ROOT / 'build/linux/_deps/sdl3-src/LICENSE.txt')
    package(p.parse_args())
