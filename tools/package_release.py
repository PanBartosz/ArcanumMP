#!/usr/bin/env python3
"""Assemble local development packages without proprietary game assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def package(build, target, output, allow_dirty=False):
    build, output = build.resolve(), output.resolve()
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip())
    if dirty and not allow_dirty:
        raise RuntimeError('Commit the reviewed source before packaging, or explicitly use --allow-dirty for a local smoke package.')
    stamp = int(subprocess.check_output(['git', 'show', '-s', '--format=%ct', 'HEAD'], cwd=ROOT, text=True))
    version = re.search(r'#define COOP_PROTOCOL_VERSION (\d+)', (ROOT / 'src/coop/session.h').read_text())[1]
    name = f'arcanum-mp-{target}-x86_64-dev-p{version}-{commit[:7]}'
    stage = output / name
    if stage.exists():
        raise FileExistsError(stage)
    stage.mkdir(parents=True)
    suffix = '.exe' if target == 'windows' else ''
    binaries = stage / 'bin'
    binaries.mkdir()
    shutil.copy2(build / f'upstream/arcanum-ce/arcanum-mp{suffix}', binaries / f'arcanum-mp{suffix}')
    decoder = ROOT / 'build/deps' / f'ffmpeg-{target}'
    if target == 'windows':
        for path in sorted((decoder / 'bin').glob('*.dll')):
            shutil.copy2(path, binaries / path.name)
    else:
        (binaries / 'lib').mkdir()
        for path in sorted((decoder / 'lib').glob('*.so.*')):
            if re.search(r'\.so\.\d+$', path.name):
                shutil.copy2(path, binaries / 'lib' / path.name)
    manifest = ''.join(f'{digest(path)}  {path.relative_to(binaries).as_posix()}\n'
                       for path in sorted(binaries.rglob('*')) if path.is_file())
    (binaries / '.arcmp-bin.sha256').write_text(manifest)
    shutil.copy2(ROOT / 'LICENSE.md', stage / 'LICENSE.md')
    shutil.copy2(ROOT / 'NOTICE.md', stage / 'NOTICE.md')
    shutil.copy2(ROOT / 'CHANGELOG.md', stage / 'CHANGELOG.md')
    shutil.copy2(ROOT / 'docs/INSTALL.md', stage / 'INSTALL.md')
    # Keep the handoff readable outside the source tree.
    shutil.copy2(ROOT / 'docs/PLAYER_GUIDE.md', stage / 'PLAYER_GUIDE.md')
    shutil.copy2(ROOT / 'docs/FINAL_READINESS.md', stage / 'FINAL_READINESS.md')
    shutil.copy2(ROOT / 'docs/CAMPAIGN_TODO.md', stage / 'CAMPAIGN_TODO.md')
    shutil.copy2(ROOT / 'docs/PROTOCOL53_FIXES.md', stage / 'PROTOCOL53_FIXES.md')
    shutil.copy2(ROOT / 'docs/PROTOCOL54_FEATURES.md', stage / 'PROTOCOL54_FEATURES.md')
    shutil.copy2(ROOT / 'docs/PROTOCOL55_FIXES.md', stage / 'PROTOCOL55_FIXES.md')
    shutil.copy2(ROOT / 'docs/PROTOCOL56_FEATURES.md', stage / 'PROTOCOL56_FEATURES.md')
    shutil.copy2(ROOT / 'docs/PROTOCOL57_FEATURES.md', stage / 'PROTOCOL57_FEATURES.md')
    shutil.copy2(ROOT / 'docs/PROTOCOL58_FEATURES.md', stage / 'PROTOCOL58_FEATURES.md')
    shutil.copy2(ROOT / 'docs/QOL_BENCHMARKS.md', stage / 'QOL_BENCHMARKS.md')
    shutil.copy2(ROOT / 'docs/UI_POLISH.md', stage / 'UI_POLISH.md')
    (stage / 'images').mkdir()
    shutil.copy2(ROOT / 'docs/images/ui-polish-comparison.jpg', stage / 'images/ui-polish-comparison.jpg')
    install = stage / 'INSTALL.md'
    install.write_text(install.read_text().replace('(STATUS.md)', '(FINAL_READINESS.md)')
                       .replace('(../LICENSE.md)', '(LICENSE.md)'))
    legal = stage / 'licenses'
    legal.mkdir()
    dependencies = set((build / '_deps').glob('*-src'))
    cache = (build / 'CMakeCache.txt').read_text()
    for dependency_name in ('FPATTERN', 'SDL3', 'SDL3_MIXER', 'ZLIB'):
        override = re.search(rf'^FETCHCONTENT_SOURCE_DIR_{dependency_name}:[^=]+=(.+)$', cache, re.M)
        if override and override[1].strip():
            dependencies.add(Path(override[1].strip()))
    for dependency in sorted(dependencies):
        notices = [p for p in dependency.iterdir() if p.is_file() and re.match(r'^(copying|license|copyright)(\..*)?$', p.name, re.I)]
        if notices:
            dest = legal / dependency.name
            dest.mkdir()
            for notice in notices:
                shutil.copy2(notice, dest / notice.name)
    dependency_sources = stage / 'dependency-source'
    dependency_sources.mkdir()
    import gzip
    for dependency in sorted(dependencies):
        def source_filter(info):
            if '.git' in Path(info.name).parts:
                return None
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = stamp
            return info
        path = dependency_sources / f'{dependency.name}.tar.gz'
        with path.open('xb') as file, gzip.GzipFile(fileobj=file, mode='wb', mtime=stamp, filename='') as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as bundle:
                bundle.add(dependency, arcname=dependency.name, filter=source_filter)
    if target == 'windows':
        compiler_info = next((build / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
        compiler = Path(re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)', compiler_info.read_text())[1])
        toolchain = compiler.parent.parent
        runtime_notices = [toolchain / 'LICENSE.TXT', *sorted((toolchain / 'x86_64-w64-mingw32/share/mingw32').glob('COPYING*'))]
    else:
        exception = next((p for p in [Path('/usr/share/licenses/libstdc++/RUNTIME.LIBRARY.EXCEPTION'),
                                      Path('/usr/share/doc/gcc-14-base/copyright'), Path('/usr/share/doc/gcc-13-base/copyright')] if p.is_file()), None)
        gpl = next((p for p in [Path('/usr/share/licenses/spdx/GPL-3.0-only.txt'), Path('/usr/share/common-licenses/GPL-3')] if p.is_file()), None)
        if not exception or not gpl:
            raise RuntimeError('Compiler runtime license and exception must be available for redistribution.')
        runtime_notices = [exception, gpl]
    (legal / 'compiler-runtime').mkdir()
    for notice in runtime_notices:
        shutil.copy2(notice, legal / 'compiler-runtime' / notice.name)
    shutil.copytree(decoder / 'share/arcmp-ffmpeg', stage / 'ffmpeg-source')
    source = stage / 'source'
    source.mkdir()
    with (source / 'arcanum-mp-source.tar').open('wb') as stream:
        subprocess.run(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT, stdout=stream, check=True)
    if dirty:
        (source / 'local-source.patch').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD'], cwd=ROOT))
        untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'], cwd=ROOT, text=True).splitlines()
        for relative in untracked:
            p = ROOT / relative
            if p.is_file():
                dest = source / 'untracked' / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, dest)
    metadata = {'source_commit': commit, 'source_dirty': dirty, 'protocol': int(version),
                'target': target, 'stage': 'development',
                'campaign_verification': {
                    'original_main_path_connected': True,
                    'shared_ending_and_menu_verified': True,
                    'retained_checkpoints': True,
                    'strengthened_combat_stats': True,
                    'normal_party_balance_verified': False,
                    'evidence': 'FINAL_READINESS.md',
                },
                'retail_assets_included': False, 'decoder': json.loads((decoder / 'arcmp-build.json').read_text()),
                'files': {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob('*')) if p.is_file()}}
    (stage / 'package.json').write_text(json.dumps(metadata, indent=2) + '\n')
    if target == 'windows':
        archive = output / f'{name}.zip'
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
            for path in sorted(stage.rglob('*')):
                if path.is_file():
                    info = zipfile.ZipInfo(f'{name}/{path.relative_to(stage).as_posix()}', time.gmtime(max(stamp, 315532800))[:6])
                    info.external_attr = (path.stat().st_mode & 0xffff) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    bundle.writestr(info, path.read_bytes())
    else:
        archive = output / f'{name}.tar.gz'
        def stable(info):
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = stamp
            return info
        # Gzip's header timestamp is fixed as well as tar metadata.
        import gzip
        with archive.open('xb') as file, gzip.GzipFile(fileobj=file, mode='wb', mtime=stamp, filename='') as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as bundle:
                bundle.add(stage, arcname=name, filter=stable)
    (output / f'{archive.name}.sha256').write_text(f'{digest(archive)}  {archive.name}\n')
    print(json.dumps({'package': str(archive), 'directory': str(stage), 'sha256': digest(archive), 'protocol': int(version)}))
    return stage


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--target', choices=['linux', 'windows'], required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--allow-dirty', action='store_true')
    args = parser.parse_args()
    package(args.build, args.target, args.output, args.allow_dirty)
