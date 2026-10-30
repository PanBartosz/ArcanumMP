#!/usr/bin/env python3
"""Record the shipped engine's composed framebuffer without changing gameplay.

GDB locates the software framebuffer once, then leaves the engine running.
Its ancestor recorder samples /proc/PID/mem at 30 fps. No game hooks, fake
network peers, desktop recording, or changes to the delivered binary are used.
Run only against isolated media runtimes. Raw takes remain outside git.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'evidence/showcase'


def prepare(index, binary=None, prefix='showcase'):
    source = ROOT / 'runtime/isolated-host'
    runtime = ROOT / f'runtime/{prefix}-{index}'
    if (runtime / 'arcanum-mp').exists():
        media(source, runtime)
        return runtime
    runtime.mkdir(exist_ok=True)
    manifest = json.loads((source / 'arcmp-content.json').read_text())
    for entry in manifest:
        # Preserve original spelling, including Arcanum4.dat.
        relative = Path(entry['path'])
        original = source / relative
        if not original.exists():
            original = source
            for part in relative.parts:
                original = next(p for p in original.iterdir() if p.name.casefold() == part.casefold())
            relative = original.relative_to(source)
        dest = runtime / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            continue
        if original.is_symlink():
            dest.symlink_to(original.resolve())
        else:
            shutil.copy2(original, dest)
    for relative in ('arcmp-content.id', 'arcmp-content.json'):
        shutil.copy2(source / relative, runtime / relative)
    (runtime / 'arcanum-mp').symlink_to(binary or ROOT / 'build/game/upstream/arcanum-ce/arcanum-mp')
    (runtime / 'HighRes').mkdir(exist_ok=True)
    (runtime / 'HighRes/config.ini').write_text('[Main]\nWidth=1280\nHeight=720\nWindowed=1\nLogos=0\nIntro=0\n')
    (runtime / 'arcanum.cfg').write_text('difficulty=1\nshadows=1\nobject lighting=1\ntext duration=6\ntext floaters=1\nfloat speed=2\nfollower skills=1\neffects volume=8\nvoice volume=6\nmusic volume=0\nturn-based=0\nfast turn-based=0\nauto attack=1\ncombat taunts=1\nviolence filter=0\nalways run=1\nbrightness=0\nshow version=0\nselected_char_id=NULL\n')
    media(source, runtime)
    return runtime


def media(source, runtime):
    for original in source.rglob('*'):
        if original.is_file() and original.suffix.casefold() in ('.bik', '.mp3', '.wav'):
            dest = runtime / original.relative_to(source)
            if not dest.exists():
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.symlink_to(original.resolve())


def script(info):
    result = '''set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set python print-stack full
break tig_video_flip
run
python
import gdb,json
from pathlib import Path
s=gdb.parse_and_eval("'video.c'::tig_video_state.surface")
data={k:int(s[k]) for k in ('w','h','pitch','format')}
data['pixels']=int(s['pixels'])
data['pid']=gdb.selected_inferior().pid
Path(INFO).write_text(json.dumps(data))
end
disable 1
continue
'''.replace('INFO', repr(str(info)))
    if os.environ.get('ARCMP_SHOWCASE_DIRECTOR'):
        result=result.rsplit('continue',1)[0]+'''break gamelib_ping if 'runtime.c'::runtime && 'runtime.c'::runtime->phase == COOP_RUNTIME_PLAYING
continue
disable 2
python
exec(open(STAGE).read())
end
quit
'''.replace('STAGE',repr(str(ROOT/'tools/showcase'/os.environ.get('ARCMP_SHOWCASE_MP_STAGE','stage_mp.py'))))
    elif os.environ.get('ARCMP_SHOWCASE_LOG'):
        result=result.rsplit('continue',1)[0]+'''break battle_probe.c:setup
continue
call coop_play_pref_set(PLAY_PREF_TEXT,100)
call coop_play_pref_set(PLAY_PREF_BAR,1)
set $tr=(TigRect*)calloc(1,sizeof(TigRect))
set $tr->x=846
set $tr->y=315
set $tr->width=420
set $tr->height=175
call coop_hud_store(HUD_LOG_COMPACT,$tr)
call coop_hud_opacity_set(HUD_LOG_COMPACT,75)
call coop_hud_log_state(1,1)
call coop_play_ui_open(0)
disable 2
continue
'''
    return result


def recorder(info_path, video, process, done, stats):
    deadline = time.monotonic() + 40
    while not info_path.exists() and process.poll() is None and time.monotonic() < deadline:
        time.sleep(.02)
    if not info_path.exists():
        stats['error'] = 'No initialized framebuffer'
        return
    info = json.loads(info_path.read_text())
    stats.update(info)
    try:
        if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):
            fifo=info_path.with_suffix('.bgr')
            ff=subprocess.Popen(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-y',
                '-f','rawvideo','-pixel_format','bgr0','-video_size',f"{info['w']}x{info['h']}",
                '-framerate','30','-i',str(fifo),'-an','-c:v','libx264','-preset','veryfast',
                '-crf','16','-threads','2','-pix_fmt','yuv420p',str(video)])
            while ff.poll() is None and process.poll() is None and not done.is_set():time.sleep(.05)
            if ff.poll() is None and process.poll() is not None:ff.wait(timeout=15)
            if ff.poll() is None:
                ff.terminate()
                try:ff.wait(timeout=3)
                except subprocess.TimeoutExpired:ff.kill();ff.wait()
            report=info_path.with_suffix('.frames.json')
            if report.exists():stats.update(json.loads(report.read_text()))
            stats.update(encoded=ff.returncode==0,seconds=stats.get('frames',0)/30)
            return
        memory = os.open(f"/proc/{info['pid']}/mem", os.O_RDONLY)
        assert info['pitch'] == info['w'] * 4
        ff = subprocess.Popen(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
            '-f', 'rawvideo', '-pixel_format', 'bgr0', '-video_size', f"{info['w']}x{info['h']}",
            '-framerate', '30', '-i', '-', '-an', '-c:v', 'libx264', '-preset', 'veryfast',
            '-crf', '16', '-threads', '2', '-pix_fmt', 'yuv420p', str(video)], stdin=subprocess.PIPE)
        start = time.monotonic()
        stats['started_monotonic'] = start
        frames = late = 0
        previous = None
        while not done.is_set() and process.poll() is None:
            now = time.monotonic()
            frame_number = int((now-start)*30)
            if frames > frame_number:
                time.sleep(min(.005, (frames/30)-(now-start)))
                continue
            try:
                frame = os.pread(memory, info['pitch']*info['h'], info['pixels'])
            except OSError:
                break
            if len(frame) != info['pitch']*info['h']:
                break
            if frames + 2 < frame_number:
                late += frame_number - frames
            # Preserve wall-clock cadence if the encoder briefly falls behind.
            while frames <= frame_number:
                ff.stdin.write(previous if previous is not None and frames < frame_number else frame)
                frames += 1
            previous = frame
        ff.stdin.close()
        ff.wait(timeout=40)
        os.close(memory)
        stats.update(frames=frames, seconds=frames/30, late_frames=late, encoded=ff.returncode == 0)
    except Exception as exc:
        stats['error'] = str(exc)


def run(mode, label, seconds, extra=(), players=4, stage=None, binary=None, prefix='showcase'):
    out = OUT / 'takes' / label
    out.mkdir(parents=True, exist_ok=True)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    games = []
    done = threading.Event()
    start = time.monotonic()
    try:
        for index in range(players):
            runtime = prepare(index if players > 1 else 4, binary, prefix)
            info = out / f'peer-{index}.json'
            info.unlink(missing_ok=True)
            if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):
                fifo=info.with_suffix('.bgr')
                fifo.unlink(missing_ok=True);os.mkfifo(fifo)
            gdb_file = out / f'peer-{index}.gdb'
            if stage:
                gdb_file.write_text('set pagination off\nset confirm off\nset debuginfod enabled off\nset print thread-events off\nbreak verify_dialogue_semantics\nrun\npython\nexec(open('+repr(str(ROOT/'tools/showcase'/stage))+').read())\nend\nquit\n')
            else:
                gdb_file.write_text(script(info))
            log = (out / f'peer-{index}.log').open('wb')
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
            env.update(ARCMP_SHOWCASE_OUT=str(out), ARCMP_SHOWCASE_INFO=str(info))
            env['ARCMP_SHOWCASE_ROOT']=str(ROOT)
            env['ARCMP_SHOWCASE_PLAYER']=str(index)
            if os.environ.get('ARCMP_SHOWCASE_DIRECTOR'):
                env['LD_PRELOAD']=os.environ.get('ARCMP_SHOWCASE_PUMP',str(OUT/'pump.so'))
            args = ['gdb', '-q', '-batch', '-x', str(gdb_file), '--args', str(runtime/'arcanum-mp'),
                '-window', '-geometry', '1280x720', f'--coop-engine-probe={mode}', f'--coop-probe-port={port}',
                f'--coop-probe-player={index}', '--coop-probe-hold=3000', *extra]
            if index == 0:
                args.append('--coop-probe-host')
            p = subprocess.Popen(args, cwd=runtime, env=env, stdout=log, stderr=subprocess.STDOUT)
            stats = {'player':index, 'runtime':str(runtime), 'video':str(out/f'peer-{index}.mp4')}
            thread = threading.Thread(target=recorder, args=(info, out/f'peer-{index}.mp4', p, done, stats))
            thread.start()
            games.append((p, thread, stats, log))
            if index == 0 and players > 1:
                deadline = time.monotonic()+30
                while 'ARCMP bootstrap connection host=1' not in (out/f'peer-{index}.log').read_text(errors='replace'):
                    if p.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Host did not reach bootstrap')
                    time.sleep(.05)
        while time.monotonic()-start < seconds and games[0][0].poll() is None and any(p.poll() is None for p,_,_,_ in games):
            time.sleep(.2)
    finally:
        done.set()
        for p,thread,stats,log in games:
            thread.join(timeout=50)
            if p.poll() is None:
                # Interrupt the debugger and its inferior only; no user processes.
                import signal
                if 'pid' in stats:
                    try: os.kill(stats['pid'], signal.SIGTERM)
                    except ProcessLookupError: pass
                try: p.wait(timeout=4)
                except subprocess.TimeoutExpired: p.terminate()
            log.close()
        report = {'mode':mode, 'label':label, 'captured_at':time.strftime('%Y-%m-%dT%H:%M:%S%z'),
            'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'backend':'completed native frames at 30fps' if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES') else '30fps real-time sampling of shipped native TIG framebuffer',
            'network':'four independent native engine processes over TCP loopback' if players==4 else 'solo',
            'peers':[g[2] for g in games]}
        (out/'capture.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2),flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode')
    parser.add_argument('label')
    parser.add_argument('--seconds',type=float,default=100)
    parser.add_argument('--extra',action='append',default=[])
    parser.add_argument('--players',type=int,default=4)
    parser.add_argument('--stage')
    parser.add_argument('--binary',type=Path,help='explicit engine binary for a new media runtime')
    parser.add_argument('--runtime-prefix',default='showcase',help='use a fresh prefix to preserve previous takes/runtimes')
    args=parser.parse_args()
    run(args.mode,args.label,args.seconds,args.extra,args.players,args.stage,args.binary.resolve() if args.binary else None,args.runtime_prefix)
