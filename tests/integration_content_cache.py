"""Check production fingerprints and cache invalidation against Python SHA-256."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
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

    def check(label):
        manifest = [{'path': n, 'sha256': hashlib.sha256((root/n).read_bytes()).hexdigest(),
                     'size': (root/n).stat().st_size} for n in sorted(assets)]
        expected = hashlib.sha256(json.dumps(manifest, separators=(',', ':')).encode()).hexdigest()
        command = ['wine', probe, 'Z:' + temporary.replace('/', '\\')] if args.wine else [probe, temporary]
        result = subprocess.run(command, env={**os.environ, 'WINEDEBUG': '-all'}, capture_output=True, text=True, timeout=40)
        assert result.returncode == 0 and result.stdout.strip() == expected, (label, result.stdout, result.stderr)
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
