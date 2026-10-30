#!/usr/bin/env python3
"""Build pinned, shared LGPL FFmpeg libraries for Arcanum's Bink movies.

Only local file demuxing, Bink video/audio, scaling and resampling are enabled.
The tarball was authenticated with FFmpeg's release signing key, fingerprint
FCF986EA15E6E293A5644F10B4322F04D67658D8, before recording this SHA-256.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = '9.0.2'
SHA256 = '8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e'


def build(target, jobs, mingw):
    root = Path(__file__).resolve().parents[1]
    deps = root / 'build/deps'
    deps.mkdir(parents=True, exist_ok=True)
    archive = deps / f'ffmpeg-{VERSION}.tar.xz'
    url = f'https://ffmpeg.org/releases/{archive.name}'
    if not archive.exists():
        urllib.request.urlretrieve(url, archive)
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == SHA256, 'FFmpeg source checksum differs'
    source = deps / f'ffmpeg-{VERSION}'
    if not source.exists():
        with tarfile.open(archive) as contents:
            contents.extractall(deps, filter='data')
    prefix = deps / f'ffmpeg-{target}'
    stamp = prefix / 'arcmp-build.json'
    if stamp.exists() and json.loads(stamp.read_text())['source_sha256'] == SHA256:
        print(f'Reusing {prefix}'); return
    work = deps / f'ffmpeg-build-{target}'
    work.mkdir(exist_ok=True)
    command = [str(source / 'configure'), f'--prefix={prefix}', '--disable-everything',
               '--disable-autodetect', '--disable-programs', '--disable-doc', '--disable-network',
               '--disable-avdevice', '--disable-avfilter', '--disable-debug', '--disable-x86asm',
               '--disable-gpl', '--disable-nonfree', '--disable-version3', '--enable-shared',
               '--disable-static', '--enable-pic', '--enable-small', '--enable-avcodec',
               '--enable-avformat', '--enable-avutil', '--enable-swscale', '--enable-swresample',
               '--enable-demuxer=bink', '--enable-decoder=bink,binkaudio_dct,binkaudio_rdft',
               '--enable-protocol=file']
    if target == 'windows':
        assert mingw and (mingw / 'bin/x86_64-w64-mingw32-clang').exists(), 'Set ARCMP_MINGW or --mingw'
        command += ['--enable-cross-compile', '--target-os=mingw32', '--arch=x86_64',
                    f'--cross-prefix={mingw}/bin/x86_64-w64-mingw32-',
                    f'--cc={mingw}/bin/x86_64-w64-mingw32-clang',
                    f'--ar={mingw}/bin/llvm-ar', f'--nm={mingw}/bin/llvm-nm',
                    f'--ranlib={mingw}/bin/llvm-ranlib', f'--strip={mingw}/bin/llvm-strip']
    subprocess.run(command, cwd=work, check=True)
    subprocess.run(['make', f'-j{jobs}'], cwd=work, check=True)
    subprocess.run(['make', 'install'], cwd=work, check=True)
    notices = prefix / 'share/arcmp-ffmpeg'
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'COPYING.LGPLv2.1', notices)
    shutil.copy2(archive, notices)
    record = {'version': VERSION, 'source_url': url, 'source_sha256': SHA256,
              'configure': command, 'license': 'LGPL-2.1-or-later', 'linkage': 'shared'}
    stamp.write_text(json.dumps(record, indent=2) + '\n')
    shutil.copy2(stamp, notices)
    print(f'Built {prefix}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['linux', 'windows'], required=True)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--mingw', type=Path, default=Path(os.environ['ARCMP_MINGW']) if 'ARCMP_MINGW' in os.environ else None)
    args = parser.parse_args()
    build(args.target, args.jobs, args.mingw)
