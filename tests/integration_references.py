#!/usr/bin/env python3
"""Exercise native owned-item destruction and a deliberate dangling-reference control.

Requires four disposable engine runtimes, two Linux and two Windows/Wine.
The negative control must fail closed in all three replicas; ordinary gameplay
never uses its raw-disposal path.
"""
import argparse
import json
from pathlib import Path
import re
import integration_bootstrap as bootstrap


def run_case(runtimes, output, sanitized, negative, framebuffer):
    mode = 'movement-owned-delete-raw' if negative else 'movement-owned-delete'
    rejected = False
    try:
        bootstrap.run_party(runtimes, output, False, sanitized, mode, framebuffer=framebuffer and not negative)
    except AssertionError:
        if not negative:
            raise
        rejected = True
    assert rejected == negative, 'Deliberate raw disposal must be rejected; native destruction must succeed.'
    logs = [(output / f'game-{i}.log').read_text(errors='replace') for i in range(4)]
    assert all(not re.search(r'ERROR: AddressSanitizer|runtime error:|LeakSanitizer: detected', log) for log in logs)
    assert re.search(r'ARCMP transient deletion item=G_\S+ owner=G_\S+ parent-type=16', logs[0])
    if negative:
        for log in logs[1:]:
            assert re.search(r'ARCMP deletion referenced by object=G_\S+ type=16 field=238 ', log)
            assert 'ARCMP world batch rejected phase=references to deleted objects' in log
            assert f'ARCMP engine probe FAIL mode={mode}' in log
    else:
        reports = [json.loads((output / f'world-{i}.json').read_text()) for i in range(4)]
        assert all(r['owned_transient_delete'] and r['npc_inventory_reference_cleared'] and r['deletions'] >= 1 for r in reports)
    return {'mode': mode, 'expected_rejection': negative, 'verified': True, 'output': str(output)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, action='append', required=True)
    parser.add_argument('--sanitized-runtime', type=Path)
    parser.add_argument('--framebuffer', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert len(args.runtime) == 4 and len({p.resolve() for p in args.runtime}) == 4
    linux = [p for p in args.runtime if (p / 'arcanum-mp').exists()]
    windows = [p for p in args.runtime if (p / 'arcanum-mp.exe').exists()]
    assert len(linux) == len(windows) == 2
    sanitized = args.sanitized_runtime.resolve() if args.sanitized_runtime else None
    results = []
    for name, runtimes in (('linux-host', [linux[0], linux[1], windows[0], windows[1]]),
                           ('windows-host', [windows[0], linux[0], linux[1], windows[1]])):
        for negative in (True, False):
            label = 'raw-negative' if negative else 'native-destroy'
            print(f'RUN {name} {label}', flush=True)
            results.append(run_case(runtimes, args.output / name / label, sanitized, negative, args.framebuffer))
            print(f'PASS {name} {label}', flush=True)
    (args.output / 'summary.json').write_text(json.dumps({'verified': True, 'cases': results}, indent=2) + '\n')
    print('PASS native owned-item cleanup and replica dangling-reference rejection in both host directions', flush=True)
