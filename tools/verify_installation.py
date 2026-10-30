#!/usr/bin/env python3
"""Verify installation bytes against a previously recorded asset manifest."""
import argparse
import hashlib
import json
from pathlib import Path

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.install.resolve()
    entries = json.loads(args.manifest.read_text())
    paths = {p.relative_to(root).as_posix().casefold(): p for p in root.rglob('*') if p.is_file()}
    changed = []
    for entry in entries:
        source = paths.get(entry['path'])
        if source is None:
            changed.append(entry['path'])
            continue
        with source.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if digest != entry['sha256']:
            changed.append(entry['path'])
    result = {'checked_assets': len(entries), 'changed_assets': changed}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2))
    print(json.dumps(result))
    raise SystemExit(1 if changed else 0)
