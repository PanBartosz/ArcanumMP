"""Check production fingerprints and cache invalidation against Python SHA-256."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--probe', type=Path, required=True)
p.add_argument('--wine', action='store_true')
args = p.parse_args()
probe = str(args.probe.resolve())
with tempfile.TemporaryDirectory(prefix='arcmp-content-') as temporary:
    root = Path(temporary)
    assets = ['arcanum1.dat', 'arcanum2.dat', 'arcanum3.dat', 'arcanum4.dat', 'tig.dat', 'modules/arcanum.dat']
    for index, name in enumerate(assets):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(bytes([index]) * (index + 1))

    def canonical_bytes(path):
        data = bytearray(path.read_bytes())
        if path.suffix == '.pro' and len(data) >= 56 and struct.unpack_from('<Ih', data) == (0x77, -1) and struct.unpack_from('<h', data, 28)[0] == 1:
            for start, end in [(6, 28), (30, 36), (40, 52)]:
                data[start:end] = bytes(end-start)
        return data

    def check(label, compatible=None, accepted=True):
        manifest = [{'path': n, 'sha256': hashlib.sha256(canonical_bytes(root/n)).hexdigest(),
                     'size': (root/n).stat().st_size} for n in sorted(assets)]
        expected = hashlib.sha256(json.dumps(manifest, separators=(',', ':')).encode()).hexdigest()
        command = ['wine', probe, 'Z:' + temporary.replace('/', '\\')] if args.wine else [probe, temporary]
        if compatible:
            command.append(compatible)
        result = subprocess.run(command, env={**os.environ, 'WINEDEBUG': '-all'}, capture_output=True, text=True, timeout=40)
        assert result.returncode == (0 if accepted else 3) and result.stdout.strip() == expected, (label, result.stdout, result.stderr)
        assert json.loads((root/'arcmp-content.json').read_text()) == manifest
        print(label, 'PASS', flush=True)
        return expected

    original = check('initial')
    timestamp = (root/'arcmp-content.json').stat().st_mtime_ns
    assert check('unchanged cache') == original
    assert (root/'arcmp-content.json').stat().st_mtime_ns == timestamp
    changed = root/assets[0]
    old_time = changed.stat().st_mtime_ns
    changed.write_bytes(b'Z')
    os.utime(changed, ns=(old_time + 2_000_000_000, old_time + 2_000_000_000))
    assert check('same-size changed asset') != original
    assets.append('data/rules.mes')
    (root/'data').mkdir()
    (root/assets[-1]).write_bytes(b'original override')
    check('added override')
    (root/assets.pop()).unlink()
    check('removed override')
    (root/'arcmp-content.cache').write_text('incomplete write\n')
    check('torn cache')
    (root/'arcmp-content.id').write_text('0' * 64 + '\n')
    check('mismatched cached identity')
    (root/'arcmp-content.cache').unlink()
    check('legacy fingerprint migration')

    ignored = ['data/preferences.cfg', 'data/desktop.ini', 'data/debug.log',
               'data/rules.mes.bak', 'data/rules.mes.tmp-123', 'data/.DS_Store',
               'data/.cache/index', 'data/art/tile/tilevariant.dat',
               'modules/Arcanum/maps/generated.v1', 'modules/Arcanum/Save/Current/map.sec',
               'modules/Arcanum/players/character.pro', 'data/tigcache/decoded.dat',
               'arcanum.cfg', 'HighRes/config.ini', 'arcmp-display.cfg',
               'languages/pl/mes/menu.mes', 'languages/pl/dlg/dialogue.dlg',
               'modules/Arcanum/screen0000.bmp', 'modules/Arcanum/screen12345.bmp', 'data/screen0001.bmp']
    timestamp = (root/'arcmp-content.json').stat().st_mtime_ns
    for name in ignored:
        path = root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('first run')
    current = check('preferences, localization, caches, saves and backups ignored')
    for name in ignored:
        (root/name).write_text('changed by the next launch')
    assert check('launch-time changes reuse cache') == current
    assert (root/'arcmp-content.json').stat().st_mtime_ns == timestamp

    # Recreate an old release's real manifest including generated/personal files.
    old = json.loads((root/'arcmp-content.json').read_text())
    for name in ignored[:8]:
        old.append(dict(path=name.lower(), sha256=hashlib.sha256((root/name).read_bytes()).hexdigest(), size=(root/name).stat().st_size))
    old.sort(key=lambda row: row['path'])
    old_text = json.dumps(old, sort_keys=True, separators=(',', ':'))
    retired = hashlib.sha256(old_text.encode()).hexdigest()
    (root/'arcmp-content.json').write_text(old_text + '\n')
    (root/'arcmp-content.id').write_text(retired + '\n')
    (root/'arcmp-content.cache').write_text('ACCONTENT1\n')
    assert check('saved identity migrated only after comparing retained file bytes', retired) == current
    check('saved identity retained on restart', retired)
    # Rehash after an identical file is recopied: timestamps are never shared IDs.
    os.utime(root/assets[0], None)
    assert check('identical drop-in/re-copy preserves identity', retired) == current

    for name in ['data/proto/000001.pro', 'data/rules/monster.txt', 'data/scr/0001.scr',
                 'modules/Arcanum/maps/CrashSite/001.sec', 'modules/Arcanum.patch1']:
        path = root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('different gameplay')
        assets.append(name.lower())
        # Native Linux filenames preserve case, but the canonical keys do not.
        if path.as_posix() != (root/assets[-1]).as_posix():
            canonical = root/assets[-1]
            canonical.parent.mkdir(parents=True, exist_ok=True)
            path.rename(canonical)
        assert check('real gameplay change rejected: ' + name, retired, False) != current
        (root/assets.pop()).unlink()
        check('original data restored', retired, False)

    prototype = root/'data/proto/native.pro'
    assets.append('data/proto/native.pro')
    raw = bytearray(range(96))
    struct.pack_into('<Ih', raw, 0, 0x77, -1)
    struct.pack_into('<h', raw, 28, 1)
    struct.pack_into('<i', raw, 36, 6065)
    prototype.write_bytes(raw)
    current = check('native prototype header')
    # Simulate an actual old-release manifest with raw ObjectID padding hashes.
    old = json.loads((root/'arcmp-content.json').read_text())
    for row in old:
        if row['path'] == assets[-1]:
            row['sha256'] = hashlib.sha256(raw).hexdigest()
    old_text = json.dumps(old, separators=(',', ':'))
    retired = hashlib.sha256(old_text.encode()).hexdigest()
    (root/'arcmp-content.json').write_text(old_text+'\n')
    (root/'arcmp-content.id').write_text(retired+'\n')
    (root/'arcmp-content.cache').write_text('ACCONTENT2\n')
    assert check('raw-header saved identity migrated without losing saves', retired) == current
    for start, end in [(6, 28), (30, 36), (40, 52)]:
        for index in range(start, end):
            raw[index] ^= 0x5a
    prototype.write_bytes(raw)
    assert check('regenerated ObjectID padding preserves identity and old saves', retired) == current
    raw[36] ^= 1
    prototype.write_bytes(raw)
    assert check('changed real prototype ID rejected', retired, False) != current
    raw[36] ^= 1
    raw[60] ^= 1
    prototype.write_bytes(raw)
    assert check('changed gameplay field rejected', retired, False) != current
    prototype.unlink()
    assets.pop()
    for name in ['data/art/screen0000.bmp', 'modules/arcanum/maps/cave/screen0000.bmp', 'data/screenshot.bmp']:
        path = root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'actual game art')
        assets.append(name)
        assert check('game artwork still fingerprinted: '+name) != original
