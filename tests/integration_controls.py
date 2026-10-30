#!/usr/bin/env python3
"""Combined four-engine party controls, durable copies, and cross-host recovery.

Use four private prepared runtimes. The optional tail starts at the retained
recovery checkpoint's rest stage and avoids repeating the long-pause prelude.
"""
import argparse
import hashlib
import json
from pathlib import Path

from integration_bootstrap import run_party


def copies(runtimes, output):
    records = {}
    for name in ('arcmp-campaign.acsave', 'arcmp-recovery.acsave'):
        hashes = [hashlib.sha256((runtime / name).read_bytes()).hexdigest()
                  for runtime in runtimes]
        assert len(set(hashes)) == 1, (name, hashes)
        records[name] = hashes[0]
    (output / 'durable-copies.json').write_text(json.dumps(records, indent=2) + '\n')


def recover(runtimes, previous, output, sanitized=None):
    """Change host and connection order while preserving original owner IDs."""
    expected = json.loads((previous / 'game-0.json').read_text())
    copies(runtimes, previous)
    run_party(runtimes, output, False, sanitized, mode='runtime-recover', framebuffer=True)
    for index in range(4):
        actual = json.loads((output / f'game-{index}.json').read_text())
        assert actual['members'] == expected['members']
        assert actual['map'] == expected['map'] and actual['global0'] == expected['global0']
    (output / 'ownership-recovery.json').write_text(json.dumps({
        'verified': True, 'changed_host': True,
        'members': expected['members'], 'previous': str(previous),
    }, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitized-runtime', type=Path)
    parser.add_argument('--tail', action='store_true')
    parser.add_argument('--recover-from', type=Path,
                        help='validate retained completed outputs and run only the new host')
    args = parser.parse_args()
    runtimes = [p.resolve() for p in args.runtime]
    assert len(runtimes) == 4 and len(set(runtimes)) == 4
    sanitized = args.sanitized_runtime.resolve() if args.sanitized_runtime else None
    if args.recover_from:
        recover(runtimes, args.recover_from, args.output, sanitized)
    else:
        run_party(runtimes, args.output / 'controls', False, sanitized,
                  mode='runtime-controls-tail' if args.tail else 'runtime-controls', framebuffer=True)
        copies(runtimes, args.output / 'controls')
        recover([*runtimes[2:], *runtimes[:2]], args.output / 'controls',
                args.output / 'new-host', sanitized)
    print('PASS: combined party controls, identical durable copies and four-owner recovery on a different host')
