#!/usr/bin/env python3
"""Real campaign-engine probes and bidirectional Linux/Windows save transfers.

Requires separate prepared runtimes. These fixtures are deliberately mutated by
the probes; use disposable directories. Screenshots require Hyprland and grim.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def run_probe(runtime, mode, output, label, screenshots, environment=None):
    runtime = runtime.resolve()
    binary = runtime / 'arcanum-mp'
    command = [str(binary)] if binary.exists() else ['wine', str(runtime / 'arcanum-mp.exe')]
    command += ['-window', f'--coop-engine-probe={mode}']
    if screenshots:
        command += ['--coop-probe-hold=3000']
    result_path = runtime / 'arcmp-engine-result.json'
    result_path.unlink(missing_ok=True)
    (runtime / 'arcmp-engine-story.bin').unlink(missing_ok=True)
    log_path = output / f'{label}.log'
    log = log_path.open('wb')
    process = subprocess.Popen(command, cwd=runtime, stdout=log, stderr=subprocess.STDOUT,
                               env={**os.environ, 'WINEDEBUG': '-all', **(environment or {})})
    captured = False
    try:
        deadline = time.monotonic() + 45
        while process.poll() is None and time.monotonic() < deadline:
            if screenshots and not captured and 'ARCMP engine visual ready' in log_path.read_text(errors='replace'):
                clients = json.loads(subprocess.check_output(['hyprctl', 'clients', '-j'], text=True))
                client = next(c for c in clients if c['pid'] == process.pid)
                address = client['address']
                subprocess.check_call(['hyprctl', 'dispatch', f"hl.dsp.focus({{window='address:{address}'}})"], stdout=subprocess.DEVNULL)
                subprocess.check_call(['hyprctl', 'dispatch', 'hl.dsp.window.bring_to_top()'], stdout=subprocess.DEVNULL)
                time.sleep(.15)
                x, y = client['at']; width, height = client['size']
                subprocess.check_call(['grim', '-g', f'{x},{y} {width}x{height}', str(output / f'{label}.png')])
                captured = True
            time.sleep(.02)
        if process.poll() is None:
            raise AssertionError(f'engine probe timed out: {log_path}')
        text = log_path.read_text(errors='replace')
        assert process.returncode == 0 and f'ARCMP engine probe PASS mode={mode}' in text, text[-3000:]
        if mode == 'objects':
            assert 'ARCMP overlay presentation PASS cases=48 simulation=replica' in text, text[-3000:]
        assert result_path.exists(), f'probe produced no state report: {log_path}'
        if screenshots:
            assert captured, f'no visual capture: {log_path}'
        result = json.loads(result_path.read_text())
        assert len(result['members']) == 4 and result['global0'] == 24601
        assert len({m['id'] for m in result['members']}) == 4
        assert len({m['owner'] for m in result['members']}) == 4
        assert [m['strength'] for m in result['members']] == [11, 12, 13, 14]
        items = [item for m in result['members'] for item in m['inventory']]
        assert items and len(items) == len(set(items))
        (output / f'{label}.json').write_text(json.dumps(result, indent=2))
        return result
    finally:
        if process.poll() is None:
            process.kill(); process.wait(timeout=5)
        log.close()


def transfer(source, destination):
    source = source / 'modules/Arcanum/Save'
    destination = destination / 'modules/Arcanum/Save'
    destination.mkdir(parents=True, exist_ok=True)
    files = list(source.glob('SlotCoop*'))
    assert any(p.suffix == '.tfaf' for p in files)
    assert any(p.suffix == '.tfai' for p in files)
    for p in destination.glob('SlotCoop*'):
        assert p.is_file() and not p.is_symlink()
        p.unlink()
    for p in files:
        shutil.copy2(p, destination / p.name)


def state(result):
    return {key: result[key] for key in ['members', 'map', 'global0', 'story_crc']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linux-runtime', type=Path, required=True)
    parser.add_argument('--windows-runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('evidence/campaign-integration'))
    parser.add_argument('--screenshots', action='store_true')
    parser.add_argument('--object-fields', action='store_true',
                        help='also roundtrip loaded object fields and batches, creation/deletion, and object-pool identity regressions')
    parser.add_argument('--sanitized-runtime', type=Path,
                        help='optional third disposable runtime using an ASan/UBSan game build; runs headless with SDL dummy drivers')
    args = parser.parse_args()
    assert args.linux_runtime.resolve() != args.windows_runtime.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    original_active = None
    if args.screenshots:
        original_active = json.loads(subprocess.check_output(['hyprctl', 'activewindow', '-j'], text=True)).get('address')
    try:
        roundtrip_mode = 'objects' if args.object_fields else 'roundtrip'
        linux = run_probe(args.linux_runtime, roundtrip_mode, args.output, 'linux-roundtrip', args.screenshots)
        transfer(args.linux_runtime, args.windows_runtime)
        windows_loaded = run_probe(args.windows_runtime, 'load', args.output, 'windows-loads-linux', args.screenshots)
        assert state(linux) == state(windows_loaded), 'Windows changed Linux campaign state'
        windows = run_probe(args.windows_runtime, roundtrip_mode, args.output, 'windows-roundtrip', args.screenshots)
        transfer(args.windows_runtime, args.linux_runtime)
        linux_loaded = run_probe(args.linux_runtime, 'load', args.output, 'linux-loads-windows', args.screenshots)
        assert state(windows) == state(linux_loaded), 'Linux changed Windows campaign state'
        if args.sanitized_runtime:
            assert args.sanitized_runtime.resolve() not in {args.linux_runtime.resolve(), args.windows_runtime.resolve()}
            run_probe(args.sanitized_runtime, roundtrip_mode, args.output, 'engine-sanitizers', False,
                      {'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software',
                       'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                       'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
        summary = {'four_pc_creation_from_live_lobby': True, 'map_transition_and_return': True,
                   'separate_inventories_and_stats': True, 'ownership_and_object_ids_persist': True,
                   'malformed_manifests_rejected_without_rebinding': True, 'nickname_and_connection_slot_rebinding': True,
                   'single_player_save_compatibility': True, 'linux_to_windows_save': True, 'windows_to_linux_save': True,
                   'portable_object_field_and_world_batch_roundtrip': args.object_fields,
                   'replica_overlay_layers_frames_and_removal': args.object_fields,
                   'four_networked_gameplay_clients': False,
                   'engine_sanitizers': 'passed' if args.sanitized_runtime else 'not requested',
                   'visual_review': 'pending' if args.screenshots else 'not requested'}
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2))
        print('PASS: campaign creation, ownership, inventory, transitions, saves and both cross-platform save directions')
    finally:
        if original_active:
            clients = json.loads(subprocess.check_output(['hyprctl', 'clients', '-j'], text=True))
            if any(c['address'] == original_active for c in clients):
                subprocess.run(['hyprctl', 'dispatch', f"hl.dsp.focus({{window='address:{original_active}'}})"], stdout=subprocess.DEVNULL)
