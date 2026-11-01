#!/usr/bin/env python3
"""Bootstrap one host campaign into four actual game processes over TCP.

Pass exactly four disposable prepared runtimes (two Linux and two Windows).
Runs both host OS directions; screenshots require Hyprland and grim. This
verifies loaded snapshots/local PC bindings. Movement mode also checks owned
movement and host object/inventory mutations, without a playable campaign flow.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import re
import subprocess
import time
import zlib


def clients():
    return json.loads(subprocess.check_output(['hyprctl', 'clients', '-j'], text=True))


def retain_campaign_checkpoint(games, output, seen, ordinary=False):
    """Retain a fully identical, CRC-valid recovery generation for focused tails.

    The observed phase is a bookmark, not a claim that that stage passed. Its
    all-owner progress acknowledgments still determine accepted prefixes.
    """
    if len(games) != 4:
        return
    try:
        payloads = [(g['runtime'] / 'arcmp-recovery.acsave').read_bytes() for g in games]
        if not payloads or any(p != payloads[0] for p in payloads):
            return
        data = payloads[0]
        digest = hashlib.sha256(data).hexdigest()
        if digest in seen or len(data) < 196 or data[:8] != b'ACCOOP1\0':
            return
        epoch, map_id, size, crc = struct.unpack_from('>4I', data, 40)
        if size != len(data) - 196 or zlib.crc32(data[196:]) != crc or zlib.crc32(data[:192]) != struct.unpack_from('>I', data, 192)[0]:
            return
        phases = []
        for game in games:
            matches = re.findall(r'ARCMP ordinary phase=(\d+)' if ordinary else r'ARCMP progress phase=(\d+)', game['path'].read_text(errors='replace'))
            phases.append(int(matches[-1]) if matches else 0)
        target = output / 'checkpoints' / f'epoch-{epoch}-phase-{min(phases)}-{digest[:12]}'
        source = target / ('recovery' if ordinary else 'source')
        source.mkdir(parents=True, exist_ok=True)
        for game, payload in zip(games, payloads):
            (source / (game['runtime'].name + '.acsave')).write_bytes(payload)
        if ordinary:
            for kind, filename, suffix in [('named', 'arcmp-campaign.acsave', '.acsave'), ('metadata', 'arcmp-ordinary-receipts.bin', '.bin')]:
                for game in games:
                    path = game['runtime'] / filename
                    if path.exists():
                        dest = target / kind / (game['runtime'].name + suffix)
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        dest.write_bytes(path.read_bytes())
        (target / 'checkpoint.json').write_text(json.dumps({'epoch': epoch, 'map': map_id, 'observed_phases': phases, 'sha256': digest, 'bytes': len(data), 'stage_acceptance_requires_logs': True}, indent=2) + '\n')
        seen.add(digest)
    except OSError:
        # Checkpoint replacement can race a read on Windows; retry next tick.
        return


def continuity_script(runtime, environment=False):
    """A private one-action native script, exercising the real file interpreter."""
    header = (Path(__file__).resolve().parents[1] / 'upstream/arcanum-ce/src/game/script.h').read_text()
    body = re.search(r'typedef enum ScriptActionType \{(.*?)\} ScriptActionType;', header, re.S)[1]
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    names = [part.strip() for part in body.split(',') if part.strip()]
    assert all(name.startswith('SAT_') and '=' not in name for name in names)
    action = struct.pack('<i8B8i', names.index('SAT_PLAY_SOUND_SCHEME' if environment else 'SAT_GIVE_QUEST_XP'),
                         3 if environment else 0, 3, 0, 0, 0, 0, 0, 0, 0, 0 if environment else 1, 0, 0, 0, 0, 0, 0)
    condition = bytes(44) + action + bytes(44)
    payload = bytes(8) + b'ARCMP isolated story XP fixture'.ljust(40, b'\0') + struct.pack('<4i', 0, 1, 1, 0) + condition
    assert len(payload) == 196
    path = runtime / ('data/scr/999996arcmp-environment.scr' if environment else 'data/scr/999997arcmp-continuity.scr')
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        assert path.read_bytes() == payload, 'Refusing to overwrite an unrelated script fixture'
    path.write_bytes(payload)
    return path


def reconnect_scripts(runtime):
    """Index the private pending-arrival scripts before native module startup."""
    header = (Path(__file__).resolve().parents[1] / 'upstream/arcanum-ce/src/game/script.h').read_text()
    body = re.search(r'typedef enum ScriptActionType \{(.*?)\} ScriptActionType;', header, re.S)[1]
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    names = [part.strip() for part in body.split(',') if part.strip()]
    paths = []
    for ordinal in (1, 2):
        rows = []
        for name, value in [('SAT_MULTIPLY', 10), ('SAT_ADD', ordinal), ('SAT_REMOVE_THIS_SCRIPT', None), ('SAT_RETURN_AND_RUN_DEFAULT', None)]:
            operands = [1, 1, 3, 0, 0, 0, 0, 0] if value is not None else [0] * 8
            values = [1998, 1998, value, 0, 0, 0, 0, 0] if value is not None else [0] * 8
            action = struct.pack('<i8B8i', names.index(name), *operands, *values)
            rows.append(bytes(44) + action + bytes(44))
        payload = bytes(52) + struct.pack('<3I', 4, 4, 0) + b''.join(rows)
        path = runtime / f'data/scr/{999981 + ordinal}arcmp-reconnect-arrival.scr'
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists():
            assert path.read_bytes() == payload, f'Refusing to replace unrelated script: {path}'
        path.write_bytes(payload)
        paths.append(path)
    return paths


def service_recovery_scripts(runtime):
    """Private native actions; all destructive tests use gameplay destruction."""
    header = (Path(__file__).resolve().parents[1] / 'upstream/arcanum-ce/src/game/script.h').read_text()
    def names(kind):
        body = re.search(r'typedef enum ' + kind + r' \{(.*?)\} ' + kind + ';', header, re.S)[1]
        body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
        return [part.strip() for part in body.split(',') if part.strip()]
    actions, objects = names('ScriptActionType'), names('ScriptFocusObject')
    paths = []
    for number, focus in [(999936, 'SFO_TRIGGERER'), (999937, 'SFO_EXTRA_OBJECT'), (999938, 'SFO_ATTACHEE'), (999939, None), (999940, None)]:
        rows = []
        if focus:
            action = struct.pack('<i8B8i', actions.index('SAT_DESTROY'), objects.index(focus), *([0] * 7), *([0] * 8))
            rows.append(bytes(44) + action + bytes(44))
        action = struct.pack('<i8B8i', actions.index('SAT_RETURN_AND_SKIP_DEFAULT' if number == 999940 else 'SAT_RETURN_AND_RUN_DEFAULT'), *([0] * 16))
        rows.append(bytes(44) + action + bytes(44))
        payload = bytes(8) + b'ARCMP private service recovery'.ljust(40, b'\0') + struct.pack('<4I', 0, len(rows), len(rows), 0) + b''.join(rows)
        path = runtime / f'data/scr/{number}arcmp-service-recovery.scr'
        path.parent.mkdir(parents=True, exist_ok=True)
        assert not path.exists(), f'Refusing to replace existing script: {path}'
        path.write_bytes(payload)
        paths.append(path)
    path = runtime / 'data/dlg/999939arcmp-service-recovery.dlg'
    path.parent.mkdir(parents=True, exist_ok=True)
    assert not path.exists(), f'Refusing to replace existing dialogue: {path}'
    path.write_text('{1}{Choose a native panel.}{}{}{}{}{}\n'
                    '{2}{Show your abilities.}{}{1}{}{10}{ce}\n'
                    '{3}{l:}{}{1}{}{10}{}\n'
                    '{10}{Ready.}{}{}{}{}{}\n'
                    '{11}{Close.}{}{1}{}{0}{}\n')
    paths.append(path)
    return paths


def run_party(runtimes, output, screenshots, sanitized_runtime=None, mode='bootstrap', journal=False, framebuffer=False, campaign_tail=False, campaign_first=10, preparation_resume=False, service_first=1, sustained_first=1, ordinary_first=1, ordinary_last=27):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    games = []
    fixture_paths = []
    retained_checkpoints = set()
    checkpoint_at = 0
    geometry = os.environ.get('ARCMP_PROBE_GEOMETRY', '800x600')
    assert re.fullmatch(r'[0-9]+x[0-9]+', geometry), geometry
    expected_frame = tuple(map(int, geometry.split('x')))
    assert expected_frame[0] >= 800 and expected_frame[1] >= 600
    output.mkdir(parents=True, exist_ok=True)
    try:
        for index, runtime in enumerate(runtimes):
            runtime = runtime.resolve()
            if mode == 'runtime-native-tools':
                (runtime / 'arcmp-controls-result.json').unlink(missing_ok=True)
                for phase in range(301, 312):
                    (runtime / f'arcmp-controls-{phase}.bmp').unlink(missing_ok=True)
            if mode == 'runtime-ordinary':
                (runtime / 'arcmp-ordinary-result.json').unlink(missing_ok=True)
                (runtime / 'arcmp-connected-ending-result.json').unlink(missing_ok=True)
                if ordinary_last == 283: (runtime / 'arcmp-connected-ending-slide.bmp').unlink(missing_ok=True)
                (runtime / 'arcmp-ordinary-input.txt').unlink(missing_ok=True)
                if ordinary_first == 1: (runtime / 'arcmp-ordinary-receipts.bin').unlink(missing_ok=True)
                for phase in range(1, 74): (runtime / f'arcmp-ordinary-{phase}.bmp').unlink(missing_ok=True)
            if mode == 'runtime-sustained':
                (runtime / 'arcmp-sustained-result.json').unlink(missing_ok=True)
                for stage in range(8): (runtime / f'arcmp-sustained-{stage}.bmp').unlink(missing_ok=True)
            if mode in ('runtime-callbacks', 'runtime-service-recovery'):
                fixture_paths.extend(service_recovery_scripts(runtime))
                (runtime / 'arcmp-services-result.json').unlink(missing_ok=True)
                for phase in range(1, 69):
                    (runtime / f'arcmp-service-{phase}.bmp').unlink(missing_ok=True)
            if mode == 'runtime-reconnect':
                fixture_paths.extend(reconnect_scripts(runtime))
            if mode in ('runtime-continuity', 'runtime-companion-ui'):
                fixture_paths.append(continuity_script(runtime))
            if mode in ('runtime-environment', 'runtime-environment-ui'):
                # Native script names are indexed at module startup. Install
                # the private placeholder before launching; the host selects
                # retail scheme operands before its first interpreter call.
                fixture_paths.append(continuity_script(runtime, environment=True))
                (runtime / 'arcmp-environment-result.json').unlink(missing_ok=True)
                for phase in (1, 2, 9, 10, 14):
                    (runtime / f'arcmp-environment-{phase}.bmp').unlink(missing_ok=True)
            if mode in ('runtime-campaign-progress','runtime-regions','runtime-quests','runtime-voyage','runtime-forest','runtime-dark','runtime-legacy','runtime-finale','runtime-traversal','runtime-rescue','runtime-evil','runtime-evil-ending','runtime-combat-finale'):
                (runtime / 'arcmp-campaign-progress-result.json').unlink(missing_ok=True)
                for number in [*range(999990,999995),*range(999970,999976),*range(999960,999967),*range(999950,999958),*range(999940,999945),*range(999900,999910),*range(999920,999931),999932,999933,999934,999935]:
                    path = runtime / f'data/scr/{number}arcmp-progress.scr'
                    payload = bytes(8) + b'ARCMP private campaign integration'.ljust(40, b'\0') + struct.pack('<4i', 0, 1, 1, 0) + bytes(132)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    assert not path.exists(), f'Refusing to replace an existing script: {path}'
                    path.write_bytes(payload)
                    fixture_paths.append(path)
                if mode in ('runtime-legacy','runtime-finale','runtime-traversal','runtime-rescue','runtime-evil','runtime-evil-ending','runtime-combat-finale'):
                    for name in ('arcmp-legacy-result.json','arcmp-legacy-documents.bin','arcmp-finale-result.json','arcmp-finale-slide.bmp','arcmp-traversal-interlude.bmp','arcmp-combat-finale-result.json'):
                        (runtime/name).unlink(missing_ok=True)
                    for phase in range(124,281):
                        (runtime/f'arcmp-progress-campaign-{phase}.bmp').unlink(missing_ok=True)
                if mode == 'runtime-dark':
                    for name in ('arcmp-dark-result.json','arcmp-dark-documents.bin','arcmp-dark-reader-100.bmp','arcmp-dark-reader-123.bmp'):
                        (runtime/name).unlink(missing_ok=True)
                    for phase in range(97,124):(runtime/f'arcmp-progress-campaign-{phase}.bmp').unlink(missing_ok=True)
                if mode == 'runtime-forest':
                    (runtime / 'arcmp-forest-result.json').unlink(missing_ok=True)
                    for phase in range(74,97):
                        (runtime / f'arcmp-progress-campaign-{phase}.bmp').unlink(missing_ok=True)
                if mode == 'runtime-voyage':
                    (runtime / 'arcmp-voyage-result.json').unlink(missing_ok=True)
                    for phase in range(61, 74):
                        (runtime / f'arcmp-progress-campaign-{phase}.bmp').unlink(missing_ok=True)
                    path = runtime / 'data/dlg/999960arcmp-progress.dlg'
                    path.parent.mkdir(parents=True, exist_ok=True)
                    assert not path.exists(), f'Refusing to replace existing private dialogue: {path}'
                    path.write_bytes(b'')
                    fixture_paths.append(path)
                for phase in (4, 7, 10, 12, 13, 19):
                    (runtime / f'arcmp-progress-campaign-{phase}.bmp').unlink(missing_ok=True)
            if mode == 'runtime-sectors':
                (runtime / 'arcmp-sectors-result.json').unlink(missing_ok=True)
                for phase in (1, 7, 9):
                    (runtime / f'arcmp-sectors-{phase}.bmp').unlink(missing_ok=True)
            result = runtime / 'arcmp-engine-result.json'
            result.unlink(missing_ok=True)
            (runtime / 'arcmp-engine-story.bin').unlink(missing_ok=True)
            (runtime / 'arcmp-movement-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-world-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-story-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-story-state.bin').unlink(missing_ok=True)
            (runtime / 'arcmp-probe.bmp').unlink(missing_ok=True)
            (runtime / 'arcmp-dialogue-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-dialogue-transcript.bin').unlink(missing_ok=True)
            for name in ('arcmp-runtime-result.json', 'arcmp-runtime-transcript.bin', 'arcmp-live-state.json', 'arcmp-live-dialogue.bin', 'arcmp-travel-result.json'):
                (runtime / name).unlink(missing_ok=True)
            for epoch in range(1, 4):
                (runtime / f'arcmp-travel-epoch-{epoch}.bmp').unlink(missing_ok=True)
            for name in ('arcmp-items-result.json', 'arcmp-items-final.bmp'):
                (runtime / name).unlink(missing_ok=True)
            for name in ('arcmp-loot-result.json', 'arcmp-loot-open.bmp'):
                (runtime / name).unlink(missing_ok=True)
            for name in ('arcmp-encounter-result.json', 'arcmp-encounter-1.bmp', 'arcmp-encounter-3.bmp', 'arcmp-encounter-5.bmp'):
                (runtime / name).unlink(missing_ok=True)
            for name in ('arcmp-battle-result.json', 'arcmp-battle-turn.bmp', 'arcmp-battle-realtime.bmp'):
                (runtime / name).unlink(missing_ok=True)
            for name in ('arcmp-weapons-result.json', 'arcmp-weapons-once.bmp', 'arcmp-weapons-turn.bmp'):
                (runtime / name).unlink(missing_ok=True)
            for name in ('arcmp-magic-result.json', 'arcmp-magic-fx.bmp', 'arcmp-magic-buffed.bmp', 'arcmp-magic-summoned.bmp', 'arcmp-magic-turn.bmp', 'arcmp-magic-resurrected.bmp'):
                (runtime / name).unlink(missing_ok=True)
            for name in ('arcmp-audio-result.json', 'arcmp-audio-loop.bmp'):
                (runtime / name).unlink(missing_ok=True)
            (runtime / 'arcmp-use-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-use-inventory.bmp').unlink(missing_ok=True)
            (runtime / 'arcmp-use-untargeted.bmp').unlink(missing_ok=True)
            (runtime / 'arcmp-skills-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-skills-spotted.bmp').unlink(missing_ok=True)
            (runtime / 'arcmp-skills-targeting.bmp').unlink(missing_ok=True)
            (runtime / 'arcmp-progress-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-services-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-controls-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-cinema-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-ending-result.json').unlink(missing_ok=True)
            (runtime / 'arcmp-continuity-result.json').unlink(missing_ok=True)
            if mode in ('runtime-reconnect', 'runtime-reconnect-barriers', 'runtime-reconnect-actions', 'runtime-reconnect-journey'):
                (runtime / 'arcmp-reconnect-result.json').unlink(missing_ok=True)
                (runtime / 'arcmp-barrier-result.json').unlink(missing_ok=True)
                for phase in (4, 10, 13, 40, 104, 107, 110, 111):
                    (runtime / f'arcmp-reconnect-{phase}.bmp').unlink(missing_ok=True)
            for phase in (2, 3, 5, 10, 90, 99):
                (runtime / f'arcmp-continuity-{phase}.bmp').unlink(missing_ok=True)
            for scene in range(1, 4):
                (runtime / f'arcmp-cinema-{scene}.bmp').unlink(missing_ok=True)
            for phase in (*range(1, 19), 90, 91, 100, 120):
                (runtime / f'arcmp-controls-{phase}.bmp').unlink(missing_ok=True)
            for phase in range(1, 153):
                (runtime / f'arcmp-service-{phase}.bmp').unlink(missing_ok=True)
            for name in ('arcmp-training-result.json', 'arcmp-training-transcript.bin'):
                (runtime / name).unlink(missing_ok=True)
            for phase in (4, 6, 7, 8, 9, 10, 11, 12):
                (runtime / f'arcmp-training-outcome-{phase}.bmp').unlink(missing_ok=True)
            for phase in (10, 12):
                (runtime / f'arcmp-training-quote-{phase}.bmp').unlink(missing_ok=True)
            for phase in (1, 4, 12, 16, 20, 22, 29, 34, 35, 39, 40):
                (runtime / f'arcmp-progress-{phase}.bmp').unlink(missing_ok=True)
            for phase in (1, 6, 10, 11, 12, 13, 16, 21):
                (runtime / f'arcmp-skills-{phase}.bmp').unlink(missing_ok=True)
            for reader in range(4):
                (runtime / f'arcmp-use-reading-{reader}.bmp').unlink(missing_ok=True)
            binary = runtime / 'arcanum-mp'
            command = [str(binary)] if binary.exists() else ['wine', str(runtime / 'arcanum-mp.exe')]
            command += ['-window', '-geometry', geometry, f'--coop-engine-probe={mode}',
                        f'--coop-probe-port={port}', f'--coop-probe-player={index}']
            if mode == 'runtime-party-qol': command += ['--coop-live-probe']
            if mode == 'runtime-ordinary': command += [f'--coop-ordinary-first={ordinary_first}', f'--coop-ordinary-last={ordinary_last}']
            if mode == 'runtime-sustained': command += [f'--coop-sustained-first={sustained_first}']
            if mode == 'runtime-service-recovery': command += [f'--coop-service-first={service_first}']
            if preparation_resume: command += ['--coop-preparation-resume']
            if campaign_tail:
                command += ['--coop-progress-tail', f'--coop-progress-first={campaign_first}']
            if index == 0:
                command += ['--coop-probe-host']
            if screenshots or framebuffer:
                command += ['--coop-probe-hold=6000' if screenshots else '--coop-probe-hold=1']
                if journal:
                    command += ['--coop-probe-journal']
                if framebuffer:
                    command += ['--coop-probe-framebuffer']
            path = output / f'game-{index}.log'
            log = path.open('wb')
            environment = {**os.environ, 'WINEDEBUG': '-all'}
            if runtime == sanitized_runtime or framebuffer:
                environment.update({'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software',
                                    'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                                    'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
            process = subprocess.Popen(command, cwd=runtime, stdout=log, stderr=subprocess.STDOUT,
                                       env=environment)
            game = {'process': process, 'runtime': runtime, 'log': log, 'path': path, 'captured': False}
            games.append(game)
            if index == 0:
                deadline = time.monotonic() + 20
                while 'ARCMP bootstrap connection host=1' not in path.read_text(errors='replace'):
                    assert process.poll() is None and time.monotonic() < deadline, path.read_text(errors='replace')[-3000:]
                    time.sleep(.02)
        deadline = time.monotonic() + (1860 if mode == 'runtime-ordinary' else 840 if mode == 'runtime-sustained' else 1860 if mode in ('runtime-legacy','runtime-finale','runtime-traversal','runtime-rescue','runtime-evil','runtime-evil-ending','runtime-combat-finale') else 960 if mode == 'runtime-dark' else 480 if mode in ('runtime-regions','runtime-quests','runtime-voyage','runtime-forest') else 360 if mode == 'runtime-campaign-progress' else 180 if mode in ('runtime-sectors', 'runtime-reconnect-journey', 'runtime-reconnect-barriers', 'runtime-reconnect', 'runtime-environment', 'runtime-companion-ui', 'runtime-continuity', 'runtime-ending', 'runtime-defeat', 'runtime-journey', 'runtime-story-tail', 'runtime-story', 'runtime-native-tools', 'runtime-party-qol', 'runtime-controls', 'runtime-controls-ui', 'runtime-controls-tail', 'runtime-recover', 'runtime-recover-ui') else 420 if mode in ('runtime-callbacks', 'runtime-service-recovery', 'runtime-services', 'runtime-commerce', 'runtime-followers') else 240 if mode in ('runtime-magic', 'runtime-use', 'runtime-skills', 'runtime-progression', 'training') else 180 if mode == 'runtime-weapons' else 135 if mode.startswith(('runtime-loot', 'runtime-combat', 'runtime-encounter')) else 60)
        while any(g['process'].poll() is None for g in games) and time.monotonic() < deadline:
            if mode == 'runtime-ordinary' and time.monotonic() >= checkpoint_at:
                retain_campaign_checkpoint(games, output, retained_checkpoints, ordinary=True)
                checkpoint_at = time.monotonic() + 1
            if mode in ('runtime-finale', 'runtime-legacy', 'runtime-traversal', 'runtime-rescue', 'runtime-evil', 'runtime-evil-ending', 'runtime-combat-finale', 'runtime-dark', 'runtime-forest', 'runtime-voyage', 'runtime-quests', 'runtime-campaign-progress', 'runtime-regions') and time.monotonic() >= checkpoint_at:
                retain_campaign_checkpoint(games, output, retained_checkpoints)
                checkpoint_at = time.monotonic() + 1
            for index, game in enumerate(games):
                if game['process'].poll() not in (None, 0):
                    raise AssertionError(game['path'].read_text(errors='replace')[-4000:])
                text = game['path'].read_text(errors='replace')
                if screenshots and not game['captured'] and 'ARCMP engine visual ready' in text:
                    client = next(c for c in clients() if c['pid'] == game['process'].pid)
                    address = client['address']
                    subprocess.check_call(['hyprctl', 'dispatch', f"hl.dsp.focus({{window='address:{address}'}})"], stdout=subprocess.DEVNULL)
                    subprocess.check_call(['hyprctl', 'dispatch', 'hl.dsp.window.bring_to_top()'], stdout=subprocess.DEVNULL)
                    time.sleep(.1)
                    client = next(c for c in clients() if c['pid'] == game['process'].pid)
                    x, y = client['at']; width, height = client['size']
                    subprocess.check_call(['grim', '-g', f'{x},{y} {width}x{height}', str(output / f'game-{index}.png')])
                    game['captured'] = True
            time.sleep(.02)
        if mode == 'runtime-ordinary' and ordinary_last == 283:
            endings = []
            for index, game in enumerate(games):
                log_text = game['path'].read_text(errors='replace')
                assert game['process'].poll() == 0 and 'ARCMP engine probe PASS mode=runtime-ordinary' in log_text, log_text[-5000:]
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:', log_text)
                data = json.loads((game['runtime'] / 'arcmp-connected-ending-result.json').read_text())
                assert data['verified'] and data['first_phase'] == ordinary_first and data['phases'] == 283
                assert data['story'] == 22 and data['original_device'] and data['original_farewell']
                assert data['shared_slides'] > 0 and data['all_ready_done'] and data['terminal_id'] == 0
                assert data['native_menu'] and data['party_reset']
                native = json.loads((game['runtime'] / 'arcmp-engine-result.json').read_text())
                assert native['map'] == 73 and len(native['members']) == 4
                owner = (game['runtime'] / 'arcmp-player.id').read_bytes().hex()
                assert native['members'][native['local_member']]['owner'] == owner
                (output / f'ending-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                (output / f'pre-ending-party-{index}.json').write_text(json.dumps(native, indent=2) + '\n')
                endings.append(data)
                if framebuffer:
                    from PIL import Image
                    for filename,label in [('arcmp-probe.bmp','menu'),('arcmp-connected-ending-slide.bmp','slide')]:
                        with Image.open(game['runtime'] / filename) as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'ending-{label}-{index}.png')
            assert all(data == endings[0] for data in endings)
            archives = [(g['runtime'] / 'arcmp-campaign.acsave').read_bytes() for g in games]
            assert len(set(archives)) == 1
            archive = archives[0]
            assert archive[:8] == b'ACCOOP1\0' and struct.unpack_from('>I',archive,44)[0] == 73
            assert zlib.crc32(archive[196:]) == struct.unpack_from('>I',archive,52)[0]
            (output / 'result.json').write_text(json.dumps({'mode':mode,'four_real_game_processes':True,
                'ending':endings[0],'pre_ending_native_archives_identical':True,
                'pre_ending_native_archive_sha256':hashlib.sha256(archive).hexdigest()},indent=2)+'\n')
            return
        if mode in ('runtime-finale', 'runtime-combat-finale', 'runtime-evil-ending', 'runtime-ending', 'runtime-defeat'):
            ending_results = []
            for index, game in enumerate(games):
                log_text = game['path'].read_text(errors='replace')
                assert game['process'].poll() == 0 and f'ARCMP engine probe PASS mode={mode}' in log_text, log_text[-5000:]
                data = json.loads((game['runtime'] / ('arcmp-finale-result.json' if mode in ('runtime-finale','runtime-combat-finale','runtime-evil-ending') else 'arcmp-ending-result.json')).read_text())
                assert data['verified']
                if mode in ('runtime-finale','runtime-combat-finale','runtime-evil-ending'):
                    assert data['first_phase'] == campaign_first and data['last_phase'] == (280 if mode == 'runtime-evil-ending' else 213 if mode == 'runtime-combat-finale' else 177) and data['story'] == (20 if mode == 'runtime-evil-ending' else 22)
                    assert data['original_device'] == (mode != 'runtime-evil-ending') and data['original_farewell'] == (mode == 'runtime-finale')
                    if mode == 'runtime-evil-ending': assert data['original_alliance']
                    if mode == 'runtime-combat-finale':
                        combat=json.loads((game['runtime']/'arcmp-combat-finale-result.json').read_text())
                        assert combat['verified'] and combat['original_combat_movie'] and combat['original_device_ending']
                        (output/f'combat-finale-{index}.json').write_text(json.dumps(combat,indent=2)+'\n')
                    epochs = [125,127,129,132,135,137,138,141,142,146,147,149,152,157,158,165,168,124,126,128,130,131,133,136,139,140,143,144,145,148,159,160,161,162,163,164,166,167,169,170,171,172,173,174,175,176]
                    if mode == 'runtime-combat-finale': epochs=[207,208,212]
                    if mode == 'runtime-evil-ending': epochs=[244,245,246,247,249,251,252,254,256,258,261,263,266,268,271,273,274,275,276,277,278,279]
                    assert data['epoch'] == 1 + sum(stage >= campaign_first for stage in epochs) + (1 if os.environ.get('ARCMP_PROBE_MEDIA_DROP') else 0)
                    assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:',log_text)
                else:
                    assert data['defeated'] == (mode == 'runtime-defeat')
                assert data['all_ready_done'] and data['native_menu'] and data['party_reset']
                assert data['terminal_id'] == (1 if mode == 'runtime-defeat' else 0)
                assert data['shared_slides'] > 0
                ending_results.append(data)
                (output / f'ending-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for source, label in ([('arcmp-probe.bmp','menu'),('arcmp-finale-slide.bmp','slide')] if mode in ('runtime-finale','runtime-combat-finale','runtime-evil-ending') else [('arcmp-probe.bmp', 'menu'), ('arcmp-ending-slide.bmp', 'slide'), ('arcmp-ending-movie.bmp', 'movie')]):
                        with Image.open(game['runtime'] / source) as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'ending-{label}-{index}.png')
            assert all(data == ending_results[0] for data in ending_results)
            if os.environ.get('ARCMP_PROBE_MEDIA_DROP'):
                assert sum(game['path'].read_text(errors='replace').count('ARCMP native ending movie transport drop=1') for game in games) == 1
            summary = {'mode':mode, 'four_real_game_processes':True, 'ending': ending_results[0]}
            if mode in ('runtime-finale','runtime-combat-finale','runtime-evil-ending'):
                archives = [(game['runtime']/'arcmp-campaign.acsave').read_bytes() for game in games]
                assert len(set(archives)) == 1
                data = archives[0]
                assert data[:8] == b'ACCOOP1\0' and struct.unpack_from('>I',data,44)[0] == 73
                assert zlib.crc32(data[196:]) == struct.unpack_from('>I',data,52)[0]
                summary['pre_ending_native_archive_sha256'] = hashlib.sha256(data).hexdigest()
                summary['pre_ending_native_archives_identical'] = True
            (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
            return
        results = []
        for index, game in enumerate(games):
            process = game['process']
            log_text = game['path'].read_text(errors='replace')
            assert process.poll() == 0 and f'ARCMP engine probe PASS mode={mode}' in log_text, log_text[-4000:]
            if framebuffer:
                from PIL import Image
                with Image.open(game['runtime'] / 'arcmp-probe.bmp') as frame:
                    assert frame.size == expected_frame and frame.format == 'BMP'
                    frame.save(output / f'game-{index}.png')
                game['captured'] = True
            result = json.loads((game['runtime'] / 'arcmp-engine-result.json').read_text())
            owner = (game['runtime'] / 'arcmp-player.id').read_bytes().hex()
            assert len(result['members']) == 4 and (mode.startswith('runtime') or result['global0'] == 24601)
            member = result['members'][result['local_member']]
            assert member['owner'] == owner and member['id'] == result['local_pc']
            if screenshots or framebuffer:
                assert game['captured']
            (output / f'game-{index}.json').write_text(json.dumps(result, indent=2) + '\n')
            (output / f'engine-story-{index}.bin').write_bytes((game['runtime'] / 'arcmp-engine-story.bin').read_bytes())
            results.append(result)
        assert len({r['local_pc'] for r in results}) == 4
        engine_stories = [(game['runtime'] / 'arcmp-engine-story.bin').read_bytes() for game in games]
        if mode not in ('runtime-loss', 'runtime-travel-loss', 'runtime-travel-stale', 'runtime-loot-loss', 'runtime-combat-loss', 'runtime-combat-replay'):
            assert all(raw == engine_stories[0] for raw in engine_stories)
        for result in results[1:]:
            assert result['members'] == results[0]['members']
            if mode != 'runtime-travel-loss':
                assert result['map'] == results[0]['map']
            if mode not in ('runtime-loss', 'runtime-travel-loss', 'runtime-travel-stale', 'runtime-loot-loss', 'runtime-combat-loss', 'runtime-combat-replay'):
                assert result['story_crc'] == results[0]['story_crc']
        movement = []
        if mode in ('runtime-reconnect-barriers', 'runtime-reconnect-actions', 'runtime-reconnect-journey'):
            journey_tail = mode == 'runtime-reconnect-journey'
            tail = mode != 'runtime-reconnect-barriers'
            recovered = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-barrier-result.json').read_text())
                assert data['verified'] and data['phases'] == 14 and data['epoch'] == (5 if journey_tail else 1 if tail else 9) and data['recoveries'] == (2 if journey_tail else 0 if tail else 4)
                if not tail:
                    assert data['combat_points'] > 0 and data['combat_round'] > 0 and data['combat_generation'] > 0
                    assert data['spell_token'] and 2 <= data['rest_hours'] < 24 and data['journey_hours'] >= 1
                (output / f'barrier-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                recovered.append(data)
                if framebuffer:
                    from PIL import Image
                    for phase in (104, 107, 110, 111):
                        source = game['runtime'] / f'arcmp-reconnect-{phase}.bmp'
                        if source.exists():
                            with Image.open(source) as frame:
                                assert frame.size == expected_frame
                                frame.save(output / f'barrier-{phase}-{index}.png')
            assert sum(r['transport_drops'] for r in recovered) == (2 if journey_tail else 0 if tail else 4)
            for name in ('combat_points', 'combat_round', 'combat_generation', 'spell_token', 'rest_hours', 'journey_hours'):
                assert len({r[name] for r in recovered}) == 1, name
        if mode == 'runtime-reconnect':
            recovered = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-reconnect-result.json').read_text())
                assert data['verified'] and data['phases'] == 15 and data['epoch'] == 3 and data['rejoins'] == 2
                assert data['marker'] == 551234 and all(data['dialogue_crc'])
                assert data['restored_crc'] == data['dialogue_crc'][0] and data['restored_pages'] == 3 and data['arrival_order'] == 12 and data['panel_restored']
                before = (game['runtime'] / 'arcmp-reconnect-view-0.bin').read_bytes()
                after = (game['runtime'] / 'arcmp-reconnect-view-1.bin').read_bytes()
                assert before == after, 'Restored reply/options/chooser differ from the original page'
                (output / f'restored-page-{index}.bin').write_bytes(after)
                (output / f'reconnect-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                recovered.append(data)
                if framebuffer:
                    from PIL import Image
                    for phase in (4, 10, 11, 13, 40):
                        source = game['runtime'] / f'arcmp-reconnect-{phase}.bmp'
                        if source.exists():
                            with Image.open(source) as frame:
                                assert frame.size == expected_frame
                                frame.save(output / f'reconnect-{phase}-{index}.png')
            assert sum(r['transport_drops'] for r in recovered) == 3
            assert sum(r['native_retry_click'] for r in recovered) == 1
            assert sum(r['resumed_moves'] for r in recovered) == 1
            assert all(r['dialogue_crc'] == recovered[0]['dialogue_crc'] for r in recovered)
        if mode in ('runtime-legacy','runtime-traversal','runtime-rescue','runtime-evil'):
            first = campaign_first if campaign_tail else (214 if mode == 'runtime-evil' else 193 if mode == 'runtime-rescue' else 178 if mode == 'runtime-traversal' else 124)
            last = 243 if mode == 'runtime-evil' else 206 if mode == 'runtime-rescue' else 192 if mode == 'runtime-traversal' else 169
            saves = [125,127,129,132,135,137,138,141,142,146,147,149,152,157,158,165,168]
            transitions = [124,126,128,130,131,133,136,139,140,143,144,145,148,159,160,161,162,163,164,166,167,169]
            if mode == 'runtime-traversal':
                saves = [187,191]
                transitions = [178,179,180,181,185,186,189,192]
            if mode == 'runtime-rescue':
                saves=[193,197,199,202,205]
                transitions=[193,195,196,200,201,203,203,*([204]*(0 if first == 204 else 8)),206]
            if mode == 'runtime-evil':
                saves=[214,218,221,233,235,238,242]
                transitions=[215,215,215,216,219,220,222,223,224,226,226,227,228,229,230,231,232,232,232,232,234,236,237,239,239,239,239,240,240,240,243]
            documents = []; journals = []
            for index, game in enumerate(games):
                report = json.loads((game['runtime']/'arcmp-legacy-result.json').read_text())
                assert report['verified'] and report['first_phase'] == first and report['last_phase'] == last, report
                detour_prefix = report['dungeon_start'] if mode == 'runtime-evil' and first <= 232 else 0
                assert 0 <= detour_prefix <= 3
                void_prefix = report['void_start'] if mode == 'runtime-evil' and first <= 239 else 0
                assert 0 <= void_prefix <= 4
                assert report['epoch'] == 1 + sum(stage >= first for stage in saves + transitions) - detour_prefix - void_prefix, report
                assert report['story'] == (8 if mode == 'runtime-rescue' else 22 if mode == 'runtime-traversal' else 20) and report['tile_persistence'], report
                assert report['documents'] >= (3 if mode == 'runtime-evil' else 0 if mode == 'runtime-rescue' else 5) and report['journal_pages'] > (100 if mode in ('runtime-rescue','runtime-evil') else 350), report
                for source, prefix, corpus in [('arcmp-legacy-documents.bin','documents',documents),('arcmp-campaign-journal.bin','journal',journals)]:
                    raw = (game['runtime']/source).read_bytes(); corpus.append(raw)
                    (output/f'{prefix}-{index}.bin').write_bytes(raw)
                (output/f'legacy-{index}.json').write_text(json.dumps(report,indent=2)+'\n')
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:',game['path'].read_text(errors='replace'))
            assert len(set(documents)) == 1 and len(set(journals)) == 1
        if mode == 'runtime-dark':
            first = campaign_first if campaign_tail else 97
            epochs = [97,99,101,102,103,104,106,107,108,111,112,113,116,117,118,119,120,121,122,122,123]
            documents=[]; journals=[]
            for index,game in enumerate(games):
                data=json.loads((game['runtime']/'arcmp-dark-result.json').read_text())
                assert data['verified'] and data['first_phase']==first and data['last_phase']==123
                assert data['epoch']==1+sum(stage>=first for stage in epochs)
                assert data['story']==15 and data['documents']>=3 and data['journal_pages']>200
                raw=(game['runtime']/'arcmp-dark-documents.bin').read_bytes();documents.append(raw)
                (output/f'documents-{index}.bin').write_bytes(raw)
                raw=(game['runtime']/'arcmp-campaign-journal.bin').read_bytes();journals.append(raw)
                (output/f'journal-{index}.bin').write_bytes(raw)
                (output/f'dark-{index}.json').write_text(json.dumps(data,indent=2)+'\n')
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:',game['path'].read_text(errors='replace'))
            assert len(set(documents))==1 and len(set(journals))==1, 'Party documents or journals differ'
        if mode == 'runtime-forest':
            first = campaign_first if campaign_tail else 74
            transfers = [75,76,77,80,83,85,87,89,91,96]
            saves = [76,78,79,81,82,84,86,88,90,92,94]
            rows=[]; journals=[]; charts=[]
            for index,game in enumerate(games):
                data=json.loads((game['runtime'] / 'arcmp-forest-result.json').read_text())
                assert data['verified'] and data['first_phase']==first and data['last_phase']==96
                assert data['epoch']==1+sum(stage>=first for stage in transfers+saves)
                assert data['story']==10 and data['chart_hash'] and data['journal_pages']>140
                native_story = (game['runtime'] / 'arcmp-engine-story.bin').read_bytes()
                assert native_story[:4] == b'ACS4' and struct.unpack_from('>i', native_story, 36 + 1930 * 4)[0] == 1, 'Native timed path entry was not exercised'
                if first<=74: assert data['party']==4 and data['team']>=4
                if first<=82: assert data['map_io']==1
                rows.append(data); charts.append(data['chart_hash'])
                (output/f'forest-{index}.json').write_text(json.dumps(data,indent=2)+'\n')
                transcript=(game['runtime']/'arcmp-campaign-journal.bin').read_bytes();journals.append(transcript)
                (output/f'journal-{index}.bin').write_bytes(transcript)
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:',game['path'].read_text(errors='replace'))
            assert len(set(charts))==1 and all(j==journals[0] for j in journals), 'Forest maps or dialogue journals differ'
        if mode == 'runtime-voyage':
            journals = []
            expected = {61:1,62:1,65:2,67:1,68:1,69:1,70:1,71:1,72:1,73:2}
            first = campaign_first if campaign_tail else 61
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-voyage-result.json').read_text())
                assert data['verified'] and data['first_phase'] == first and data['last_phase'] == 73
                assert data['epoch'] == 1 + sum(count for phase,count in expected.items() if phase >= first)
                assert data['story'] >= 8 and data['journal_pages'] > 60
                if first <= 62:
                    assert data['presentations'] >= 4
                    played = re.findall(r'ARCMP presentation played sequence=(\d+) kind=3 success=1', game['path'].read_text(errors='replace'))
                    assert len(played) >= 4, played
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:', game['path'].read_text(errors='replace'))
                (output / f'voyage-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                transcript = (game['runtime'] / 'arcmp-campaign-journal.bin').read_bytes()
                journals.append(transcript)
                (output / f'journal-{index}.bin').write_bytes(transcript)
                if framebuffer:
                    from PIL import Image
                    for phase in (63,64,65,67,69,71,73):
                        if phase < first: continue
                        source = game['runtime'] / f'arcmp-progress-campaign-{phase}.bmp'
                        if source.exists(): Image.open(source).save(output / f'voyage-{phase}-{index}.png')
            assert all(j == journals[0] for j in journals), 'Voyage conversation journals differ'
        if mode == 'runtime-quests':
            journals = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-quests-result.json').read_text())
                first = campaign_first if campaign_tail else 48
                assert data['verified'] and data['first_phase'] == first and data['last_phase'] == 60
                assert data['epoch'] == 1 + sum(stage >= first for stage in (48,49,50,51,53,54,55,57,59))
                assert (first > 50 or data['queued_owners'] == 15) and data['story'] >= 5 and data['journal_pages'] > 20
                assert first > 49 or data['bates_seen']
                assert first > 55 or data['pillar_seen']
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:', game['path'].read_text(errors='replace'))
                (output / f'quests-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                transcript = (game['runtime'] / 'arcmp-campaign-journal.bin').read_bytes()
                journals.append(transcript)
                (output / f'journal-{index}.bin').write_bytes(transcript)
                if framebuffer:
                    from PIL import Image
                    for phase in (49,53,55,56,60):
                        if phase < first: continue
                        Image.open(game['runtime'] / f'arcmp-progress-campaign-{phase}.bmp').save(output / f'quests-{phase}-{index}.png')
            assert all(j == journals[0] for j in journals), 'Saved conversation journals differ'
        if mode == 'runtime-regions':
            for index, game in enumerate(games):
                data=json.loads((game['runtime']/'arcmp-regions-result.json').read_text())
                first=campaign_first if campaign_tail else 23
                assert data['verified'] and data['first_phase']==first and data['last_phase']==47
                assert data['epoch']==1+sum(stage in (29,30) or stage>=34 for stage in range(first,48))
                assert data['pcm_phases']==sum(stage>=first for stage in (24,25,26,28,31,32))
                assert data['retail_exits']==sum(stage>=first for stage in (35,36,38,40,41,43,44,45,46,47))
                assert data['save_faults']==(1 if index==0 and first<=33 else 0)
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:',game['path'].read_text(errors='replace'))
                (output/f'regions-{index}.json').write_text(json.dumps(data,indent=2)+'\n')
                if framebuffer:
                    from PIL import Image
                    for phase in (24,35,40,47):
                        if phase<first: continue
                        Image.open(game['runtime']/f'arcmp-progress-campaign-{phase}.bmp').save(output/f'regions-{phase}-{index}.png')
        if mode == 'runtime-campaign-progress':
            progress = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-campaign-progress-result.json').read_text())
                assert data['verified'] and data['phases'] == 22 and data['epoch'] == ({10:9,13:6,17:3,19:2,20:1}[campaign_first] if campaign_tail else 11)
                assert data['first_phase'] == (campaign_first if campaign_tail else 1)
                assert data['script_cache_entries'] <= 100 and data['script_locks'] == 0
                assert data['ring_owner_known'] and (campaign_first > 13 and campaign_tail or data['ring_reveal_seen']) and data['story'] >= 2 and data['world_use_count'] == 1
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:', game['path'].read_text(errors='replace'))
                (output / f'campaign-progress-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                progress.append(data)
                if framebuffer:
                    from PIL import Image
                    for phase in ((10, 12, 13, 19, 20, 22) if campaign_tail else (4, 7, 10, 12, 13, 19, 20, 22)):
                        if campaign_tail and phase < campaign_first: continue
                        source = game['runtime'] / f'arcmp-progress-campaign-{phase}.bmp'
                        with Image.open(source) as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'campaign-progress-{phase}-{index}.png')
            if not campaign_tail or campaign_first <= 13:
                assert sum(data['retail_choices'] for data in progress) >= 12
        if mode == 'runtime-ordinary':
            ordinary_results = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-ordinary-result.json').read_text())
                assert data['verified'] and data['first_phase'] == ordinary_first and data['phases'] == ordinary_last
                assert data['original_merchants'] == [1040,1140,1140,1140] and data['shrouded_hills']
                assert data['tarant_known'] or data['ristezze_quest'] >= 2
                assert len(set(data['weapons'])) == 4 and all(len(x) == 34 and x != '00'*17 for x in data['weapons'])
                if ordinary_last == 27:
                    assert all(price > 0 and after == before - price for before, price, after in zip(data['gold_before'], data['prices'], data['gold_after']))
                    assert data['gold_after'][0] >= 100
                elif ordinary_last == 48:
                    assert data['gold_after'] == [0,0,107,219]
                    assert data['bridge_key_owned'] and data['in_tarant'] and data['tarant_known'] and data['ristezze_quest'] == 4
                    assert sum(data['gold_before']) - sum(data['gold_after']) - sum(data['prices']) == 1000
                if 73 <= ordinary_last <= 76:
                    assert data['factory_quest'] == 4 and data['saboteur_deaths'] >= 3
                    assert data['bmc_quest'] == 2 and data['bmc_known'] and data['in_bmc'] and data['story'] >= 4
                    assert len(data['xp_before']) == len(data['xp_after']) == 4
                    if ordinary_last == 76: assert data['story'] >= 5
                if ordinary_last >= 82:
                    assert data['bmc_quest'] == 4 and data['story'] >= 5
                assert data['purchases_by_local_owner'] == int(9 + 4*data['member'] >= ordinary_first)
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:', game['path'].read_text(errors='replace'))
                (output / f'ordinary-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                ordinary_results.append(data)
                if framebuffer:
                    from PIL import Image
                    for phase in range(ordinary_first, ordinary_last + 1):
                        source = game['runtime'] / f'arcmp-ordinary-{phase}.bmp'
                        if source.exists():
                            with Image.open(source) as frame: frame.save(output / f'ordinary-{phase}-{index}.png')
            for field in ('gold_before', 'prices', 'gold_after', 'weapons', 'purchased_owned_mask', 'purchased_equipped_mask', 'hp_after', 'ristezze_quest', 'shrouded_hills', 'tarant_known', 'factory_quest', 'saboteur_deaths', 'bmc_quest', 'bmc_known', 'in_bmc', 'story', 'xp_after', 'points_after'):
                assert all(data[field] == ordinary_results[0][field] for data in ordinary_results), field
        if mode == 'runtime-sustained':
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-sustained-result.json').read_text())
                assert data['verified'] and data['ordinary_start'] == (not bool(os.environ.get('ARCMP_PROBE_SUSTAINED_RETAINED'))) and data['first_stage'] == sustained_first
                assert data['duration_ms'] + data['prior_session_ms'] >= 600000
                assert data['maintenance_stages'] == 7 and data['recoveries'] == 8 - sustained_first
                assert data['transport_drops'] == sum(stage >= sustained_first and data['member'] == member for stage, member in [(2, 2), (4, 3), (6, 1)])
                assert data['moves_submitted'] >= 20 and data['position_ticks'] >= 100 and data['max_separation'] >= 20
                assert data['inventory_receipts'] >= 8 and data['options_submitted'] >= 2 and data['native_panels'] >= (3 if sustained_first > 1 else 8) and data['shared_note_present']
                (output / f'sustained-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for stage in range(8):
                        frame = game['runtime'] / f'arcmp-sustained-{stage}.bmp'
                        if frame.exists():
                            with Image.open(frame) as image: image.save(output / f'sustained-{stage}-{index}.png')

        if mode == 'runtime-sectors':
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-sectors-result.json').read_text())
                assert data['verified'] and data['phases'] == 9 and data['epoch'] == 3
                assert data['separated_owners'] == 4 and data['cache_pressure_loads'] > 128 and data['native_boundary_moves'] == 4
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:', game['path'].read_text(errors='replace'))
                (output / f'sectors-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in (1, 7, 9):
                        with Image.open(game['runtime'] / f'arcmp-sectors-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'sectors-{phase}-{index}.png')
        if mode in ('runtime-environment', 'runtime-environment-ui'):
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-environment-result.json').read_text())
                assert data['verified'] and data['phases'] == 14 and data['epoch'] == (1 if mode == 'runtime-environment-ui' else 3)
                assert data['music_checks'] == (2 if mode == 'runtime-environment-ui' else 7) and data['pcm_frames'] > 7000 and data['pcm_energy'] > 0
                if mode == 'runtime-environment-ui':
                    assert data['first_phase'] == 10 and 0 < data['local_hp'] <= data['local_max_hp']
                assert data['stream_eof_released'] and data['stream_pcm_frames'] > 0 and data['effect_pcm_frames'] > 0
                assert data['replica_repair'] == (index != 0)
                (output / f'environment-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in ((10, 14) if mode == 'runtime-environment-ui' else (1, 2, 9)):
                        with Image.open(game['runtime'] / f'arcmp-environment-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'environment-{phase}-{index}.png')
        if mode in ('runtime-continuity', 'runtime-companion-ui'):
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-continuity-result.json').read_text())
                assert data['verified'] and data['phases'] == 17
                assert data['executed'] == (7 if index == 0 else 0) and data['rejected'] == ((2 if mode == 'runtime-continuity' else 1) if index == 0 else 0)
                assert data['replays'] in (0, 1) and data['local_denials'] in (0, 1)
                assert (data['journal_revision'] >= 2 if mode == 'runtime-continuity' else data['journal_revision'] == 0) and data['replica_guard'] == (index != 0)
                (output / f'continuity-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in ((2, 3, 5, 10) if mode == 'runtime-continuity' else (10,)):
                        with Image.open(game['runtime'] / f'arcmp-continuity-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'continuity-{phase}-{index}.png')
                    menu = game['runtime'] / 'arcmp-continuity-90.bmp'
                    if mode == 'runtime-companion-ui' and menu.exists():
                        with Image.open(menu) as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'continuity-90-{index}.png')
            assert sum(json.loads((game['runtime'] / 'arcmp-continuity-result.json').read_text())['replays'] for game in games) == 1
            assert sum(json.loads((game['runtime'] / 'arcmp-continuity-result.json').read_text())['local_denials'] for game in games) == (2 if mode == 'runtime-continuity' else 1)
        if mode in ('runtime-story', 'runtime-story-tail', 'runtime-journey'):
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-cinema-result.json').read_text())
                assert data['verified'] and data['phases'] == 9 and data['epoch'] == (6 if mode == 'runtime-story' else 4)
                assert data['presentations'] == (5 if mode == 'runtime-story' else 0) and data['executed_journeys'] == (2 if index == 0 else 0)
                assert data['travel_hours'] >= 2
                if mode == 'runtime-story':
                    assert data['held_movie_ms'] >= 8800
                if mode == 'runtime-story':
                    assert data['eof_frames'] == 171 and data['eof_samples'] > 500000 and data['clock_and_goal_frozen']
                (output / f'cinema-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for scene in (range(1, 4) if mode == 'runtime-story' else []):
                        with Image.open(game['runtime'] / f'arcmp-cinema-{scene}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'cinema-{scene}-{index}.png')
        if mode == 'runtime-native-tools':
            for index, game in enumerate(games):
                log = game['path'].read_text(errors='replace')
                network_slot = int(re.search(r'ARCMP campaign connected host=[01] slot=(\d+)', log).group(1))
                data = json.loads((game['runtime'] / 'arcmp-controls-result.json').read_text())
                assert data['verified'] and data['phases'] == data['phases_completed'] == 10
                assert data['epoch'] == 4 and data['executed'] == (2 if index == 0 else 0)
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:|Unhandled page fault', log)
                (output / f'native-tools-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in range(301, 312):
                        if phase == 311 and network_slot != 2: continue
                        with Image.open(game['runtime'] / f'arcmp-controls-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'native-tools-{phase}-{index}.png')
        if mode == 'runtime-party-qol':
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-controls-result.json').read_text())
                assert data['verified'] and data['phases'] == data['phases_completed'] == 9
                assert data['epoch'] == 4 and data['executed'] == (3 if index == 0 else 0)
                assert not data['recovery'] and data['first_phase'] == 1 and not data['long_pause_ms']
                (output / f'party-qol-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for frame in game['runtime'].glob('arcmp-controls-*.bmp'):
                        with Image.open(frame) as image:
                            assert image.size == expected_frame
                            image.save(output / f'party-qol-{index}-{frame.stem}.png')
        if mode == 'runtime-preferences':
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-controls-result.json').read_text())
                assert data['verified'] and data['phases'] == data['phases_completed'] == 4
                assert data['epoch'] == 3 and data['executed'] == (2 if index == 0 else 0)
                assert not data['recovery'] and data['first_phase'] == 1
                assert not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:', game['path'].read_text(errors='replace'))
                (output / f'preferences-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in range(201, 205):
                        with Image.open(game['runtime'] / f'arcmp-controls-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'preferences-{phase}-{index}.png')
        if mode in ('runtime-controls', 'runtime-controls-ui', 'runtime-controls-tail', 'runtime-recover', 'runtime-recover-ui'):
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-controls-result.json').read_text())
                recovering = mode in ('runtime-recover', 'runtime-recover-ui')
                rest_story = bool(os.environ.get('ARCMP_PROBE_REST_STORY')) and not recovering
                assert data['verified'] and data['phases'] == (1 if recovering else 18)
                assert data['epoch'] == (1 if recovering else 6 + rest_story)
                assert data['executed'] == (5 if index == 0 and not recovering else 0)
                assert data['recovery'] == recovering
                first = 8 if mode == 'runtime-controls-tail' else 1
                assert data['first_phase'] == first and data['phases_completed'] == (1 if recovering else 19 - first)
                if not recovering:
                    assert data['long_pause_ms'] == (0 if mode == 'runtime-controls-tail' else 2000 if mode == 'runtime-controls-ui' else 35000) and data['native_guards']
                    assert 2 <= data['cancelled_hours'] < 24
                if rest_story:
                    assert data['rest_story'] and data['story_observers'] == 15
                    if index == 0:
                        log = game['path'].read_text(errors='replace')
                        assert log.count('ARCMP retained transition dialogue count=1 script=1324') == 1
                        assert log.count('ARCMP dispatch queued dialogue remaining=0 script=1324') == 1
                (output / f'controls-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in (*range(1, 19), 90, 91, 100, 120, *([130] if rest_story else [])):
                        path = game['runtime'] / f'arcmp-controls-{phase}.bmp'
                        if path.exists():
                            with Image.open(path) as frame:
                                assert frame.size == expected_frame
                                frame.save(output / f'controls-{phase}-{index}.png')
        if mode in ('runtime-callbacks', 'runtime-service-recovery', 'runtime-services', 'runtime-commerce', 'runtime-followers'):
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-services-result.json').read_text())
                phases, replays, denials, executed, transfers = {
                    'runtime-callbacks': (0, 0, 0, 0, 0),
                    'runtime-service-recovery': (68, 9 if service_first == 1 else 0, 8 if service_first == 1 else 0, 0, 0),
                    'runtime-services': (152, 9, 8, 36, 8),
                    'runtime-commerce': (26, 7, 4, 7, 2),
                    'runtime-followers': (9, 3, 0, 3, 0),
                }[mode]
                acting = data['member'] == 2 if mode == 'runtime-service-recovery' else mode == 'runtime-services' or data['member'] == 0
                assert data['verified'] and data['phases'] == phases
                assert data['replays'] == (replays if acting else 0) and data['denials'] == (denials if acting else 0)
                assert data['replica_guard'] == (index != 0 and mode != 'runtime-callbacks')
                if mode == 'runtime-callbacks':
                    assert data['callback_guards'] == (34 if index == 0 else 0)
                    assert "Couldn't match object in parent!" not in game['path'].read_text(errors='replace')
                elif mode == 'runtime-service-recovery':
                    recoveries = 7 if service_first == 1 else 1 if service_first == 40 else 0
                    assert data['first_phase'] == service_first and data['recoveries'] == recoveries and data['drops'] == (recoveries if acting else 0)
                    assert data['callback_guards'] == (7 if index == 0 and service_first == 1 else 0)
                    assert data['conflicts'] == (2 if data['member'] == 3 and service_first <= 43 else 0)
                    assert data['device_credit'] == 3 and all(damage > 0 for damage in data['device_damage'])
                else:
                    assert data['executed'] == (executed if index == 0 else 0) and data['transfers'] == (transfers if index == 0 else 0)
                (output / f'services-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in range(1, 153):
                        if not (game['runtime'] / f'arcmp-service-{phase}.bmp').exists():
                            continue
                        with Image.open(game['runtime'] / f'arcmp-service-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'services-{phase}-{index}.png')
        if mode == 'training':
            outcomes, transcripts = [], []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-training-result.json').read_text())
                assert data['verified'] and data['cases'] == 24 and data['pages'] == 112
                assert data['inputs'] == 22 and data['observer_denials'] == 66 and data['stale_denials'] == 88
                assert data['replica_native_guard'] == (index != 0)
                assert data['accepted'] == (88 if index == 0 else 0)
                assert all(pc['heal_training'] == pc['firearms_training'] == 1
                           and pc['heal_points'] == pc['firearms_points'] == 2 for pc in data['players'])
                (output / f'training-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                raw = (game['runtime'] / 'arcmp-training-transcript.bin').read_bytes()
                (output / f'training-transcript-{index}.bin').write_bytes(raw)
                transcripts.append(raw); outcomes.append(data)
                if framebuffer:
                    from PIL import Image
                    for kind, phases in (('outcome', (4, 6, 7, 8, 9, 10, 11, 12)), ('quote', (10, 12))):
                        for phase in phases:
                            with Image.open(game['runtime'] / f'arcmp-training-{kind}-{phase}.bmp') as frame:
                                assert frame.size == expected_frame
                                frame.save(output / f'training-{kind}-{phase}-{index}.png')
            assert all(raw == transcripts[0] for raw in transcripts)
            assert all(row['players'] == outcomes[0]['players'] for row in outcomes)
        if mode == 'runtime-encounter':
            outcomes = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-encounter-result.json').read_text())
                assert data['verified'] and data['phases'] == 5 and data['shared_world_preserved']
                assert data['native_prototype'] == 28447 and data['legal_starting_fighters']
                if index == 0:
                    assert data['existing_attack_confirmations'] > 0
                if index == 0:
                    assert all(data[k] for k in ('off_host_native_ai_focus', 'off_host_native_ai_damage', 'native_enemy_turn', 'native_turn_damage', 'host_character_died', 'dead_controller_denied'))
                if results[index]['local_member'] == data['remote_member']:
                    assert data['real_time_attacks'] >= 2
                if results[index]['local_member'] == (2 if data['remote_member'] == 3 else 3):
                    assert data['survivor_control_after_host_death']
                (output / f'encounter-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in (1, 3, 5):
                        with Image.open(game['runtime'] / f'arcmp-encounter-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'encounter-{phase}-{index}.png')
                outcomes.append(data)
            assert sum(data['turn_based_attacks'] for data in outcomes) >= 1
        if mode.startswith('runtime-combat') or mode in ('runtime-weapons', 'runtime-magic', 'runtime-use', 'runtime-skills', 'runtime-progression'):
            outcomes = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-battle-result.json').read_text())
                assert data['verified'] and data['replica_mutation_guard_checked'] == (index != 0)
                assert data['steps'] == (2 if mode == 'runtime-combat-loss' else 19 if mode == 'runtime-combat-replay' else 31 if os.environ.get('ARCMP_PARTY_UI') else 24)
                if mode in ('runtime-combat', 'runtime-weapons', 'runtime-magic', 'runtime-use', 'runtime-skills', 'runtime-progression'):
                    assert data['stale_noncombat_rejections'] == (0 if index == 0 else 3)
                    assert data['fresh_item_after_turn']
                assert data['loss_during_combat'] == (mode == 'runtime-combat-loss')
                assert data['replay_conflict'] == (mode == 'runtime-combat-replay')
                assert data['disconnect_freeze'] == (mode not in ('runtime-combat', 'runtime-weapons', 'runtime-magic', 'runtime-use', 'runtime-skills', 'runtime-progression'))
                if mode != 'runtime-combat-loss':
                    assert data['turn_based_attacks'] == 1 and data['real_time_attacks'] == 1 and data['target_damage'] > 0
                (output / f'battle-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    stages = ('turn',) if mode == 'runtime-combat-loss' else ('turn', 'realtime')
                    for stage in stages:
                        with Image.open(game['runtime'] / f'arcmp-battle-{stage}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'battle-{stage}-{index}.png')
                outcomes.append(data)
            if mode == 'runtime-combat': assert len({o['target_damage'] for o in outcomes}) == 1
            assert outcomes[0]['executed'] > 0 and all(o['executed'] == 0 for o in outcomes[1:])
        if mode == 'runtime-weapons':
            weapon_reports = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-weapons-result.json').read_text())
                assert data['verified'] and data['phases'] == 10 and data['friendly_called_shot_denied'] == 1
                if data['local_member'] == 1:
                    assert data['stand_attempts'] == 1 and data['empty_attempts'] == 1
                if index == 0:
                    assert data['repeat_attacks'] >= 2
                    for member in data['native']:
                        assert member['once_resolved'] == 1 and member['turn_resolved'] == 2
                        if member['member'] in (1, 2): assert member['once_ammo'] == 29
                        if member['member']: assert member['ranged'] >= 2
                    assert data['native'][0]['aimed_mask'] & 2
                    assert data['native'][1]['aimed_mask'] & 8
                    assert data['native'][2]['aimed_mask'] & 4
                (output / f'weapons-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for stage in ('once', 'turn'):
                        with Image.open(game['runtime'] / f'arcmp-weapons-{stage}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'weapons-{stage}-{index}.png')
                weapon_reports.append(data)
            assert all(data['members'] == weapon_reports[0]['members'] for data in weapon_reports)
        if mode == 'runtime-progression':
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-progress-result.json').read_text())
                assert data['verified'] and data['phases'] == 41 and data['replays'] == 2 and data['denials'] == 10
                assert data['replica_guard'] == (index != 0) and data['replica_level_notices'] == (0 if index == 0 else 1)
                if index == 0:
                    assert all(value > 0 for value in data['earned_points']) and data['executed'] == 136 and data['rejected'] == 40
                else:
                    assert data['executed'] == data['rejected'] == 0
                (output / f'progression-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for phase in (1, 4, 12, 16, 20, 22, 29, 34, 35, 39, 40):
                        with Image.open(game['runtime'] / f'arcmp-progress-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'progression-{phase}-{index}.png')
        if mode == 'runtime-skills':
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-skills-result.json').read_text())
                assert data['verified'] and data['phases'] == 21 and data['native_counts_verified']
                assert data['native_invocations'] == (32 if index == 0 else 0)
                assert data['finish_native'] == (1 if index == 0 else 0)
                assert (4 <= data['spot_invocations'] <= 80) if index == 0 else data['spot_invocations'] == 0
                assert data['feedback_replays'] == (3 if index == 0 else 0)
                assert data['reentrant_callbacks'] == (4 if index == 0 else 0) and data['retired_callbacks'] == (4 if index == 0 else 0)
                assert 0 <= data['healed_damage'] < 10
                assert data['drain_clock_advanced'] == (index == 0)
                assert data['floater_expiry_checks'] == 1
                assert data['personal_feedback'] == (10 if data['member'] == 0 else 9)
                assert data['exact_replays'] == 2 and data['denials'] == 5
                assert data['skills_accepted'] == (20 if index == 0 else 0) and data['skills_rejected'] == (8 if index == 0 else 0)
                (output / f'skills-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for stage in ('spotted', 'targeting'):
                        with Image.open(game['runtime'] / f'arcmp-skills-{stage}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'skills-{stage}-{index}.png')
                    for phase in (1, 6, 10, 11, 12, 13, 16, 21):
                        with Image.open(game['runtime'] / f'arcmp-skills-{phase}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'skills-{phase}-{index}.png')
        if mode == 'runtime-use':
            reports = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-use-result.json').read_text())
                assert data['verified'] and data['phases'] == 21
                assert data['rejected_requests'] == 6 and data['exact_replays'] == 1 and data['target_cancellations'] == 1
                assert data['personal_reading_views'] == 2
                assert data['personal_views'] == 4 and data['reading_clock_checks'] == 2
                assert data['native_use_drained']
                assert data['reading_replays_sent'] == (3 if index == 0 else 0)
                assert data['executed'] == (48 if index == 0 else 0) and data['rejected'] == (24 if index == 0 else 0)
                assert data['script_dispatches'] == (57 if index == 0 else 0) and data['script_dispatch_counts_verified']
                assert data['null_target_dispatches'] == (4 if index == 0 else 0)
                assert data['script_handled_invalid_default'] == (4 if index == 0 else 0)
                assert data['interrupted_approaches'] == 1 and data['stale_completion_callbacks'] == (4 if index == 0 else 0)
                assert data['reentrant_completion_callbacks'] == (4 if index == 0 else 0)
                assert data['pool_pressure_checks'] == (4 if index == 0 else 0)
                assert data['turn_policy_changes'] == (1 if index == 0 else 0)
                assert (data['pool_reserved_slots'] > 0) if index == 0 else data['pool_reserved_slots'] == 0
                (output / f'use-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    with Image.open(game['runtime'] / 'arcmp-use-inventory.bmp') as frame:
                        assert frame.size == expected_frame
                        frame.save(output / f'use-inventory-{index}.png')
                    with Image.open(game['runtime'] / 'arcmp-use-untargeted.bmp') as frame:
                        assert frame.size == expected_frame
                        frame.save(output / f'use-untargeted-{index}.png')
                    for reader in range(4):
                        with Image.open(game['runtime'] / f'arcmp-use-reading-{reader}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'use-reading-{reader}-{index}.png')
                reports.append(data)
        if mode == 'runtime-magic':
            reports = []
            audio_reports = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-magic-result.json').read_text())
                assert data['verified'] and data['phases'] == 16 and data['rejected_requests'] == 5 and data['exact_replays'] == 1
                assert data['turn_ap_spent'] >= 4
                assert data['overlay_visibility_verified'] and data['overlay_frames'] > 0 and data['overlay_clear_transitions'] > 0
                assert data['executed'] == (41 if index == 0 else 0) and data['rejected'] == (24 if index == 0 else 0)
                if index == 0: assert data['reused_native_slots'] > 0
                assert all(member['alive'] and member['target_damage'] > 0 for member in data['members'])
                (output / f'magic-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    for stage in ('fx', 'buffed', 'summoned', 'turn', 'resurrected'):
                        with Image.open(game['runtime'] / f'arcmp-magic-{stage}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'magic-{stage}-{index}.png')
                reports.append(data)
                audio = json.loads((game['runtime'] / 'arcmp-audio-result.json').read_text())
                assert audio['verified'] and audio['phases'] == 5 and audio['active_loops'] == 0
                assert audio['wire_events'] > 0 and audio['starts'] > 0 and audio['moves'] > 0 and audio['stops'] > 0
                assert audio['local_ui_survives_old_stop']
                assert audio['native_handle_reuse'] if index == 0 else audio['listener_handle_reuse']
                assert (audio['pcm'][0]['pan'] < 64) != (audio['pcm'][1]['pan'] < 64)
                for sample in audio['pcm'][:2]:
                    assert sample['frames'] >= 5000 and sample['left'] + sample['right'] > 0 and sample['gain'] > 0
                    assert sample['left'] > 4 * sample['right'] if sample['pan'] < 64 else sample['right'] > 4 * sample['left']
                if index == 0:
                    assert audio['host_muted'] and audio['pcm'][2]['gain'] == 0 and audio['pcm'][2]['left'] == audio['pcm'][2]['right'] == 0
                else:
                    assert not audio['host_muted'] and audio['pcm'][2]['left'] + audio['pcm'][2]['right'] > 0
                (output / f'audio-{index}.json').write_text(json.dumps(audio, indent=2) + '\n')
                audio_reports.append(audio)
                if framebuffer:
                    with Image.open(game['runtime'] / 'arcmp-audio-loop.bmp') as frame:
                        assert frame.size == expected_frame
                        frame.save(output / f'audio-loop-{index}.png')
            assert all(data['members'] == reports[0]['members'] for data in reports)
            assert all((a['wire_events'], a['wire_digest']) == (audio_reports[0]['wire_events'], audio_reports[0]['wire_digest']) for a in audio_reports)
        if mode.startswith('runtime-loot'):
            outcomes = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-loot-result.json').read_text())
                assert data['verified'] and data['decay_balanced']
                if mode == 'runtime-loot':
                    assert data['steps'] == 19 and data['stale_denials'] == 3 and data['moved_and_revoked']
                    assert data['one_viewer_close_keeps_shared_container_open'] == (index == 0)
                else:
                    assert data['steps'] == 1 and data['loss_with_open_loot'] and data['disconnect_freeze']
                outcomes.append(data)
                (output / f'loot-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    with Image.open(game['runtime'] / 'arcmp-loot-open.bmp') as frame:
                        frame.save(output / f'loot-open-{index}.png')
            assert sum(outcome['race_wins'] for outcome in outcomes) == (2 if mode == 'runtime-loot' else 0)
        if mode == 'runtime-items':
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-items-result.json').read_text())
                assert data['verified'] and data['stages'] == 10 and data['staged_without_mutation']
                assert data['executed'] == (28 if index == 0 else 0) and data['rejected'] == (12 if index == 0 else 0)
                assert data['replayed'] == (index == 1)
                (output / f'items-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer:
                    from PIL import Image
                    with Image.open(game['runtime'] / 'arcmp-items-final.bmp') as frame:
                        frame.save(output / f'inventory-{index}.png')
        if mode.startswith('runtime-travel'):
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-travel-result.json').read_text())
                assert data['verified']
                if mode == 'runtime-travel':
                    assert data['epochs'] == 3 and data['final_epoch'] == 3
                    assert data['moved_after_return'] and data['accepted'] == (4 if index == 0 else 0) and not data['rejected']
                    assert data['delayed_loading'] == (index == 3)
                    assert data['travel_time_costs_checked'] == (2 if index == 0 else 0)
                    assert data['resume_clock_checks'] == (2 if index == 0 else 0)
                else:
                    assert data['disconnect_freeze'] and data['final_epoch'] == 2
                    assert data['epochs'] == (1 if mode == 'runtime-travel-loss' else 2)
                    assert data['loss_during_loading'] == (mode == 'runtime-travel-loss')
                    assert data['stale_epoch_fault'] == (mode == 'runtime-travel-stale')
                    assert data['travel_time_costs_checked'] == (1 if index == 0 else 0)
                    assert data['resume_clock_checks'] == (1 if index == 0 and mode == 'runtime-travel-stale' else 0)
                (output / f'travel-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                if framebuffer and mode == 'runtime-travel':
                    from PIL import Image
                    for epoch in range(1, 4):
                        with Image.open(game['runtime'] / f'arcmp-travel-epoch-{epoch}.bmp') as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'epoch-{epoch}-game-{index}.png')
        if mode in ('runtime-characters','runtime-preparation'):
            for index, game in enumerate(games):
                created = (game['runtime'] / ('arcmp-prepared-character.bin' if mode=='runtime-preparation' else 'arcmp-edited-character.bin')).read_bytes()
                applied = (game['runtime'] / 'arcmp-character-applied.bin').read_bytes()
                assert created == applied and len(created) == 176
                (output / f'character-{index}.bin').write_bytes(applied)
                if framebuffer:
                    from PIL import Image
                    for stage in (('preparation-library','preparation-equipment') if mode=='runtime-preparation' else ('identity','allocation','edit','identity-reset')):
                        with Image.open(game['runtime'] / (f'arcmp-{stage}.bmp' if mode=='runtime-preparation' else f'arcmp-character-{stage}.bmp')) as frame:
                            assert frame.size == expected_frame
                            frame.save(output / f'character-{stage}-{index}.png')
        if mode in ('runtime', 'runtime-loss', 'runtime-characters','runtime-preparation'):
            transcripts = []
            live_states = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-runtime-result.json').read_text())
                assert data['verified'] and data['revision'] == 6 and data['moved'] and data['halted']
                if mode in ('runtime-characters','runtime-preparation'): assert data['invalid_draft_launch_refused_and_repaired'] == (index == 0)
                assert data['disconnect_freeze']
                assert data['loss_during_play'] == (mode == 'runtime-loss')
                if mode == 'runtime-loss' and os.environ.get('ARCMP_PROBE_NATIVE_FAILURE') in ('host', 'client'):
                    assert data['native_failure'] and data['native_failure_propagated'] and data['recovery_checkpoint_preserved'] and data['failed_session_exit']
                    assert data['native_fault_injected'] == (index == (0 if os.environ['ARCMP_PROBE_NATIVE_FAILURE'] == 'host' else 3))
                    if framebuffer:
                        from PIL import Image
                        with Image.open(game['runtime'] / 'arcmp-native-failure.bmp') as frame:
                            frame.save(output / f'native-failure-{index}.png')
                assert data['observer_input'] and data['choices'] == data['cancellations']
                assert data['choices'] == (1 if data['local_member'] < 2 else 0)
                assert data['local_member'] != 1 or data['talked']
                assert data['accepted'] == (9 if index == 0 else 0) and data['rejected'] == (2 if index == 0 else 0)
                (output / f'runtime-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                raw = (game['runtime'] / 'arcmp-runtime-transcript.bin').read_bytes()
                (output / f'runtime-transcript-{index}.bin').write_bytes(raw)
                transcripts.append(raw)
                state = json.loads((game['runtime'] / 'arcmp-live-state.json').read_text())
                (output / f'live-state-{index}.json').write_text(json.dumps(state, indent=2) + '\n')
                live_states.append(state)
            assert all(raw == transcripts[0] for raw in transcripts)
            assert all(state['members'] == live_states[0]['members'] for state in live_states)
        if mode == 'dialogue':
            transcripts = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-dialogue-result.json').read_text())
                assert data['verified'] and data['revision'] == 5 and data['conversation'] == 2
                assert data['submissions'] == (1 if results[index]['local_member'] in (0, 1) else 0)
                assert data['submissions'] + data['observer_denials'] == 2
                assert data['accepted'] == (2 if index == 0 else 0) and data['rejected'] == 0
                (output / f'dialogue-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                raw = (game['runtime'] / 'arcmp-dialogue-transcript.bin').read_bytes()
                (output / f'dialogue-{index}.bin').write_bytes(raw)
                transcripts.append(raw)
            assert all(raw == transcripts[0] for raw in transcripts)
        if mode in ('movement', 'movement-owned-delete'):
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-movement-result.json').read_text())
                assert data['finished'] and data['pose_changes'] >= 2
                assert data['campaign_time_advanced'] and data['local_simulation_advanced'] == (index == 0)
                assert index == 0 or data['client_local_pose_repair_exercised']
                (output / f'movement-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                movement.append(data)
            assert movement[0]['accepted'] == 4 and movement[0]['rejected'] == 8
            assert sum(data['delayed_world_ack'] for data in movement) == 1
            assert all(data['poses'] == movement[0]['poses'] for data in movement)
            worlds = []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-world-result.json').read_text())
                assert data['verified'] and data['parent_member'] == 1 and data['quest_state'] == 2
                assert data['script_counter'] == 0x10203040 and data['deletions'] >= 1 and data['revision'] >= 4
                assert data['owned_transient_delete'] == (mode == 'movement-owned-delete')
                assert data['npc_inventory_reference_cleared']
                (output / f'world-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                worlds.append(data)
            assert all(data == worlds[0] for data in worlds)
            stories, story_bytes = [], []
            for index, game in enumerate(games):
                data = json.loads((game['runtime'] / 'arcmp-story-result.json').read_text())
                raw = (game['runtime'] / 'arcmp-story-state.bin').read_bytes()
                assert data['verified'] and data['story'] == 4 and data['last_variable'] == -654321
                assert raw[:4] == b'ACS4' and len(raw) == data['bytes']
                (output / f'story-{index}.json').write_text(json.dumps(data, indent=2) + '\n')
                (output / f'story-{index}.bin').write_bytes(raw)
                stories.append(data); story_bytes.append(raw)
            assert all(data == stories[0] for data in stories)
            assert all(raw == story_bytes[0] for raw in story_bytes)
        # Transport transferred the engine archive pair; no filesystem copying
        # occurs between the participants in this driver.
        manifests = []
        for game in games:
            save = game['runtime'] / 'modules/Arcanum/Save'
            manifests.append({suffix: hashlib.sha256((save / f'arcmp-bootstrap.{suffix}').read_bytes()).hexdigest()
                              for suffix in ['tfai', 'tfaf']})
        if mode != 'runtime-travel-loss':
            assert all(m == manifests[0] for m in manifests)
        summary = {'four_real_game_processes': True, 'shared_campaign_snapshot': mode != 'runtime-travel-loss',
                   'archive_pair_byte_identical': mode != 'runtime-travel-loss', 'distinct_owned_local_pcs': True,
                   'matching_inventory_equipment_stats': True, 'loaded_barrier_committed': True,
                   'native_quest_chapters_and_persistent_conversations': mode == 'runtime-quests',
                   'sea_voyage_companion_and_wheel_clan_route': mode == 'runtime-voyage',
                   'native_party_shared_exploration_and_forest_story': mode == 'runtime-forest',
                   'native_dark_elf_investigation_and_shared_documents': mode == 'runtime-dark',
                   'native_panarii_tulla_vendigroth_and_void_arrival': mode == 'runtime-legacy',
                   'native_traversal_companion_and_optical_entrance': mode == 'runtime-traversal',
                   'native_ship_return_emergency_helm_and_paid_passage':mode=='runtime-rescue',
                   'host_authoritative_movement': mode in ('movement', 'movement-owned-delete'),
                   'object_inventory_script_and_pc_quest_mutations': mode in ('movement', 'movement-owned-delete'),
                   'campaign_subsystems_shared_journal_and_quest_rewards': mode in ('movement', 'movement-owned-delete'),
                   'live_native_journal_and_shared_dialogue_script_rewards': mode == 'runtime-continuity',
                   'typed_companion_orders_with_applied_world_receipts': mode in ('runtime-continuity', 'runtime-companion-ui'),
                   'native_companion_context_menu_and_wait_resume': mode == 'runtime-companion-ui',
                   'shared_lighting_sound_schemes_combat_music_and_save_restore': mode == 'runtime-environment',
                   'checkpoint_recovery_health_gauges_and_environment_tail': mode == 'runtime-environment-ui',
                   'rejoin_during_combat_rest_world_journey_and_snapshot_transfer': mode == 'runtime-reconnect-barriers',
                   'live_rejoin_reserved_owners_frozen_pause_and_resumed_actions': mode == 'runtime-reconnect',
                   'shared_retail_dialogue_chooser_and_observers': mode == 'dialogue',
                   'retail_dialogue_training_payment_refusal_and_observers': mode == 'training',
                   'campaign_runtime_ui_movement_halt_npc_and_cancellation': mode in ('runtime', 'runtime-loss', 'runtime-characters'),
                   'native_character_creation_and_authoritative_launch': mode == 'runtime-characters',
                   'retail_enemy_ai_shared_combat_xp_and_host_character_death': mode == 'runtime-encounter',
                   'native_combat_requests_turns_ap_and_attacks': mode == 'runtime-combat',
                   'equipped_weapons_ammo_called_shots_and_attack_modes': mode == 'runtime-weapons',
                   'native_spells_maintained_controls_summons_and_resurrection': mode == 'runtime-magic',
                   'disconnect_during_combat': mode == 'runtime-combat-loss',
                   'altered_combat_replay_refused': mode == 'runtime-combat-replay',
                   'owned_item_actions_with_ordered_receipts': mode == 'runtime-items',
                   'shared_loot_access_and_competing_transfers': mode == 'runtime-loot',
                   'disconnect_with_open_loot_access': mode == 'runtime-loot-loss',
                   'synchronized_roundtrip_travel_with_loading_delay_and_inventory': mode == 'runtime-travel',
                   'disconnect_during_destination_loading': mode == 'runtime-travel-loss',
                   'retired_epoch_command_rejected': mode == 'runtime-travel-stale',
                   'runtime_disconnect_during_play': mode == 'runtime-loss',
                   'complete_gameplay_synchronization': False,
                   'sanitized_runtime': str(sanitized_runtime) if sanitized_runtime else None,
                   'visual_review': 'pending' if screenshots or framebuffer else 'not requested',
                   'capture_backend': 'composed TIG framebuffer with SDL dummy/software drivers' if framebuffer else 'Hyprland/grim' if screenshots else None,
                   'journal_screenshots': journal,
                   'commands': [str(g['runtime']) for g in games], 'archive_hashes': manifests[0]}
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    finally:
        if mode == 'runtime-ordinary':
            retain_campaign_checkpoint(games, output, retained_checkpoints, ordinary=True)
            if framebuffer:
                from PIL import Image
                for index, game in enumerate(games):
                    for source in game['runtime'].glob('arcmp-ordinary-*.bmp'):
                        with Image.open(source) as frame: frame.save(output / (source.stem.replace('arcmp-', '') + f'-{index}.png'))
        if mode in ('runtime-finale', 'runtime-legacy', 'runtime-traversal', 'runtime-rescue', 'runtime-evil', 'runtime-evil-ending', 'runtime-combat-finale', 'runtime-dark', 'runtime-forest', 'runtime-voyage', 'runtime-quests', 'runtime-campaign-progress', 'runtime-regions'):
            retain_campaign_checkpoint(games, output, retained_checkpoints)
        for game in games:
            if game['process'].poll() is None:
                game['process'].kill()
                game['process'].wait(timeout=5)
            game['log'].close()
        if mode in ('runtime-legacy','runtime-finale','runtime-traversal','runtime-rescue','runtime-evil','runtime-evil-ending','runtime-combat-finale') and framebuffer:
            from PIL import Image
            for index,game in enumerate(games):
                for phase in range(124,281):
                    frame = game['runtime']/f'arcmp-progress-campaign-{phase}.bmp'
                    if frame.exists(): Image.open(frame).save(output/f'legacy-{phase}-{index}.png')
                interlude=game['runtime']/'arcmp-traversal-interlude.bmp'
                if interlude.exists(): Image.open(interlude).save(output/f'interlude-{index}.png')
        if mode == 'runtime-dark' and framebuffer:
            from PIL import Image
            for index,game in enumerate(games):
                for phase in range(97,124):
                    frame=game['runtime']/f'arcmp-progress-campaign-{phase}.bmp'
                    if frame.exists():Image.open(frame).save(output/f'dark-{phase}-{index}.png')
                for phase in (100,123):
                    frame=game['runtime']/f'arcmp-dark-reader-{phase}.bmp'
                    if frame.exists():Image.open(frame).save(output/f'dark-reader-{phase}-{index}.png')
        if mode == 'runtime-forest' and framebuffer:
            from PIL import Image
            for index,game in enumerate(games):
                for phase in range(74,97):
                    frame=game['runtime']/f'arcmp-progress-campaign-{phase}.bmp'
                    if frame.exists():Image.open(frame).save(output/f'forest-{phase}-{index}.png')
        for path in fixture_paths:
            path.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, default=Path('evidence/bootstrap'))
    parser.add_argument('--screenshots', action='store_true')
    parser.add_argument('--framebuffer', action='store_true', help='capture composed game frames with headless SDL drivers; requires Pillow')
    parser.add_argument('--campaign-progress-tail', action='store_true', help='resume retained campaign recovery; --campaign-progress-first selects the continuation stage')
    parser.add_argument('--preparation-resume', action='store_true',help='Reuse verified native UI recipes for the network/campaign continuation')
    parser.add_argument('--journal', action='store_true', help='show each owned PC\'s quest log during screenshot capture')
    parser.add_argument('--sanitized-runtime', type=Path, help='one of the supplied Linux runtimes, using an ASan/UBSan build')
    parser.add_argument('--mode', choices=['runtime-ordinary', 'runtime-sustained', 'runtime-preparation', 'runtime-quests', 'runtime-voyage', 'runtime-forest', 'runtime-dark', 'runtime-legacy', 'runtime-finale', 'runtime-traversal', 'runtime-rescue', 'runtime-evil', 'runtime-evil-ending', 'runtime-combat-finale', 'runtime-regions', 'runtime-campaign-progress', 'runtime-sectors', 'bootstrap', 'movement', 'movement-owned-delete', 'movement-owned-delete-raw', 'dialogue', 'training', 'runtime', 'runtime-loss', 'runtime-travel', 'runtime-travel-loss', 'runtime-travel-stale', 'runtime-items', 'runtime-loot', 'runtime-loot-loss', 'runtime-characters', 'runtime-combat', 'runtime-combat-loss', 'runtime-combat-replay', 'runtime-encounter', 'runtime-weapons', 'runtime-magic', 'runtime-use', 'runtime-skills', 'runtime-progression', 'runtime-callbacks', 'runtime-service-recovery', 'runtime-services', 'runtime-commerce', 'runtime-followers', 'runtime-native-tools', 'runtime-party-qol', 'runtime-preferences', 'runtime-controls', 'runtime-controls-ui', 'runtime-controls-tail', 'runtime-recover', 'runtime-recover-ui', 'runtime-story', 'runtime-story-tail', 'runtime-journey', 'runtime-ending', 'runtime-defeat', 'runtime-continuity', 'runtime-companion-ui', 'runtime-environment', 'runtime-environment-ui', 'runtime-reconnect', 'runtime-reconnect-barriers', 'runtime-reconnect-actions', 'runtime-reconnect-journey'], default='bootstrap')
    parser.add_argument('--ordinary-first-phase', type=int, choices=range(1, 284), default=1)
    parser.add_argument('--ordinary-source', type=Path)
    parser.add_argument('--ordinary-last-phase', type=int, choices=[27,48,73,76,82,88,94,102,111,119,132,142,165,175,184,201,217,243,262,272,281,283], default=27)
    parser.add_argument('--sustained-first-stage', type=int, choices=range(1, 8), default=1)
    parser.add_argument('--sustained-source', type=Path)
    parser.add_argument('--service-recovery-first', type=int, choices=[1,40,43,58], default=1)
    parser.add_argument('--host-direction', choices=['both','linux','windows'], default='both')
    parser.add_argument('--campaign-progress-first', type=int, choices=[10,13,17,19,20,*range(23,281)], default=10)
    parser.add_argument('--campaign-progress-source', type=Path, help='Retained recovery archives for a focused campaign continuation')
    parser.add_argument('--campaign-progress-load-source', type=Path, help='Separate retained named-save branch restored by a focused continuation')
    args = parser.parse_args()
    assert len(args.runtime) == 4 and len({r.resolve() for r in args.runtime}) == 4
    assert not (args.framebuffer and args.screenshots)
    assert not args.journal or ((args.screenshots or args.framebuffer) and args.mode in ('movement', 'movement-owned-delete'))
    if args.screenshots:
        assert subprocess.run(['pgrep', '-x', 'hyprlock'], stdout=subprocess.DEVNULL).returncode != 0, 'Desktop is locked; use --framebuffer for isolated visual QA.'
    linux = [r for r in args.runtime if (r / 'arcanum-mp').exists()]
    windows = [r for r in args.runtime if (r / 'arcanum-mp.exe').exists()]
    assert len(linux) == 2 and len(windows) == 2
    sanitized = args.sanitized_runtime.resolve() if args.sanitized_runtime else None
    if sanitized:
        assert sanitized in {r.resolve() for r in linux}
        assert not args.screenshots, 'The sanitizer runtime runs headless; use a separate ordinary run for screenshots.'
    if args.mode == 'runtime-sustained' and (args.sustained_first_stage > 1 or os.environ.get('ARCMP_PROBE_SUSTAINED_RETAINED')):
        assert args.sustained_source, 'A retained source is required for a sustained continuation'
        for runtime in args.runtime:
            for kind, target in [('recovery', 'arcmp-recovery.acsave'), ('named', 'arcmp-campaign.acsave')]:
                payload = (args.sustained_source / kind / (runtime.name + '.acsave')).read_bytes()
                assert payload[:8] == b'ACCOOP1\0' and len(payload) >= 196
                size, crc = struct.unpack_from('>II', payload, 48)
                assert len(payload) == 196 + size and zlib.crc32(payload[196:]) == crc
                (runtime / target).write_bytes(payload)
    campaign_sources = {}
    if args.mode == 'runtime-ordinary':
        assert args.ordinary_source, 'Original-party continuation requires an explicit retained source'
        for runtime in args.runtime:
            for kind, filename in [('recovery', 'arcmp-recovery.acsave'), ('named', 'arcmp-campaign.acsave')]:
                payload = (args.ordinary_source / kind / (runtime.name + '.acsave')).read_bytes()
                assert len(payload) >= 196 and payload[:8] == b'ACCOOP1\0'
                size, crc = struct.unpack_from('>II', payload, 48)
                assert len(payload) == 196 + size and zlib.crc32(payload[196:]) == crc
                campaign_sources[runtime / filename] = payload
            if args.ordinary_first_phase > 1:
                campaign_sources[runtime / 'arcmp-ordinary-receipts.bin'] = (args.ordinary_source / 'metadata' / (runtime.name + '.bin')).read_bytes()
        for path, data in campaign_sources.items(): path.write_bytes(data)
    if args.campaign_progress_tail or args.mode in ('runtime-regions','runtime-quests','runtime-voyage','runtime-forest','runtime-dark','runtime-legacy','runtime-finale','runtime-traversal','runtime-rescue','runtime-evil','runtime-evil-ending','runtime-combat-finale'):
        source_dir = args.campaign_progress_source or args.output / 'source'
        source_dir.mkdir(parents=True, exist_ok=True)
        for runtime in args.runtime:
            source = source_dir / (runtime.name + '.acsave')
            if not source.exists():
                assert not args.campaign_progress_source, 'Requested pre-quest recovery source is missing'
                source.write_bytes((runtime / 'arcmp-recovery.acsave').read_bytes())
            campaign_sources[runtime / 'arcmp-recovery.acsave'] = source.read_bytes()
            if (args.mode == 'runtime-dark' and args.campaign_progress_first == 123) or (args.mode == 'runtime-legacy' and args.campaign_progress_first == 169) or (args.mode == 'runtime-finale' and args.campaign_progress_first >= 176) or (args.mode == 'runtime-traversal' and args.campaign_progress_first in (189,192)) or (args.mode=='runtime-rescue' and args.campaign_progress_first in (196,206)) or (args.mode=='runtime-evil' and args.campaign_progress_first==243) or (args.mode=='runtime-combat-finale' and args.campaign_progress_first==213) or (args.mode=='runtime-evil-ending' and args.campaign_progress_first>=279):
                # Seed the named archive when a continuation loads or ends
                # before creating one; it must never use a prior run's save.
                campaign_sources[runtime / 'arcmp-campaign.acsave'] = source.read_bytes()
            if args.campaign_progress_load_source:
                named = args.campaign_progress_load_source / (runtime.name + '.acsave')
                campaign_sources[runtime / 'arcmp-campaign.acsave'] = named.read_bytes()
        for path, data in campaign_sources.items(): path.write_bytes(data)
    previous = json.loads(subprocess.check_output(['hyprctl', 'activewindow', '-j'], text=True)).get('address') if args.screenshots else None
    try:
        if args.host_direction in ('both','linux'):
            run_party([linux[0], linux[1], windows[0], windows[1]], args.output / 'linux-host', args.screenshots, sanitized, args.mode, args.journal, args.framebuffer, args.campaign_progress_tail, args.campaign_progress_first, args.preparation_resume, args.service_recovery_first, args.sustained_first_stage, args.ordinary_first_phase, args.ordinary_last_phase)
        for path, data in campaign_sources.items(): path.write_bytes(data)
        if args.host_direction in ('both','windows'):
            run_party([windows[0], windows[1], linux[0], linux[1]], args.output / 'windows-host', args.screenshots, sanitized, args.mode, args.journal, args.framebuffer, args.campaign_progress_tail, args.campaign_progress_first, args.preparation_resume, args.service_recovery_first, args.sustained_first_stage, args.ordinary_first_phase, args.ordinary_last_phase)
        print(f'PASS: four actual engines {args.mode} over TCP with distinct owned PCs; host direction={args.host_direction}')
    finally:
        if previous and any(c['address'] == previous for c in clients()):
            subprocess.run(['hyprctl', 'dispatch', f"hl.dsp.focus({{window='address:{previous}'}})"], stdout=subprocess.DEVNULL)
