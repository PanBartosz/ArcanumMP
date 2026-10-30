#!/usr/bin/env python3
"""Four native engines through measured, optionally impaired TCP relays.

Stages an isolated crowd/movement/effect scene, never a campaign route. Uses
the existing native capture pump; reports actual wire bytes by message type.
"""
import argparse
import asyncio
from collections import Counter
import json
import os
from pathlib import Path
import random
import socket
import struct
import subprocess
import time

from benchmark_qol import ROOT, prepare


class Relay:
    def __init__(self, port, delay, rate):
        self.port, self.delay, self.rate = port, delay/1000, rate*1024
        self.events = []
        self.connections = 0
        self.failures = []

    async def connection(self, reader, writer):
        self.connections += 1
        peer = self.connections
        try:
            upstream, upstream_writer = await asyncio.open_connection('127.0.0.1', self.port)
            async def pump(source, target, direction):
                queue = asyncio.Queue(maxsize=256)
                async def receive():
                    available = 0
                    rng = random.Random(peer + (100 if direction=='down' else 0))
                    while data := await source.read(4096):
                        now = time.monotonic()
                        due = max(available, now + max(0, self.delay + rng.uniform(-self.delay/5, self.delay/5)))
                        if self.rate:
                            due += len(data)/self.rate
                        available = due
                        await queue.put((due,data))
                    await queue.put(None)
                async def transmit():
                    pending = bytearray()
                    while item := await queue.get():
                        due,data = item
                        await asyncio.sleep(max(0, due-time.monotonic()))
                        target.write(data)
                        await target.drain()
                        now = time.monotonic()
                        pending.extend(data)
                        while len(pending) >= 16:
                            magic, version, kind, size, sequence = struct.unpack('!4sHHII',pending[:16])
                            assert magic == b'ACMP' and size <= 1024
                            if len(pending) < 16+size:
                                break
                            self.events.append((now, peer, direction, kind, 16+size))
                            del pending[:16+size]
                await asyncio.gather(receive(),transmit())
            await asyncio.gather(pump(reader, upstream_writer, 'up'), pump(upstream, writer, 'down'))
        except (ConnectionError, asyncio.CancelledError):
            pass
        except Exception as exc:
            self.failures.append(str(exc))
        finally:
            writer.close()
            if 'upstream_writer' in locals():
                upstream_writer.close()


async def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    helper = output/'pump.so'
    subprocess.run(['cc','-shared','-fPIC','-g','-O2',str(ROOT/'tools/showcase/pump.c'),'-o',str(helper)],check=True)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0))
        port=sock.getsockname()[1]
    relay=Relay(port,args.delay_ms,args.kib_per_second)
    server=await asyncio.start_server(relay.connection,'127.0.0.1',0)
    relay_port=server.sockets[0].getsockname()[1]
    games=[]
    try:
        for index in range(4):
            runtime=output/f'runtime-{index}'
            prepare(args.content_runtime.resolve(),runtime,args.binary.resolve())
            script=output/f'peer-{index}.gdb'
            script.write_text('''set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
break gamelib_ping if 'runtime.c'::runtime && 'runtime.c'::runtime->phase == COOP_RUNTIME_PLAYING
run
disable 1
python
exec(open(STAGE).read())
end
quit
'''.replace('STAGE',repr(str(ROOT/'tools/qol_network_stage.py'))))
            log=(output/f'peer-{index}.log').open('wb')
            env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
                LD_PRELOAD=str(helper),ARCMP_QOL_OUT=str(output),ARCMP_QOL_PLAYER=str(index))
            command=['gdb','-q','-batch','-x',str(script),'--args',str(runtime/'arcanum-mp'),'-window','-geometry','1280x720',
                '--coop-engine-probe=runtime-magic',f'--coop-probe-port={port if index==0 else relay_port}',
                f'--coop-probe-player={index}','--coop-probe-hold=3000']
            if index==0:command.append('--coop-probe-host')
            process=await asyncio.create_subprocess_exec(*command,cwd=runtime,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            games.append((process,log))
            if index==0:
                deadline=time.monotonic()+30
                while 'ARCMP bootstrap connection host=1' not in (output/'peer-0.log').read_text(errors='replace'):
                    if process.returncode is not None or time.monotonic()>deadline:raise RuntimeError('Host bootstrap failed')
                    await asyncio.sleep(.05)
        await asyncio.wait_for(asyncio.gather(*(p.wait() for p,_ in games)),timeout=100)
        reports=[json.loads((output/f'peer-{i}.json').read_text()) for i in range(4)]
        assert all(p.returncode==0 for p,_ in games) and not relay.failures
        assert all(p['move_observed_ms'] is not None for p in reports),reports
        phases={}
        start=reports[0]['started']
        for name,a,b in [('idle',3,9),('movement',10,16),('effects',18,24)]:
            events=[e for e in relay.events if start+a<=e[0]<start+b]
            directions={}
            for direction in ['down','up']:
                selected=[e for e in events if e[2]==direction]
                directions[direction]={'bytes':sum(e[4] for e in selected),'bytes_per_second':sum(e[4] for e in selected)/(b-a),
                    'frames':dict(Counter(str(e[3]) for e in selected))}
            phases[name]=directions
        report={'method':'four real Linux engines; staged 64 stationary NPCs, four owner moves and native spells; TCP relay counts include headers; down totals all three clients',
            'delay_ms_one_way':args.delay_ms,'rate_kib_per_second_per_direction_per_client':args.kib_per_second,
            'phases':phases,'peers':reports,'relay_errors':relay.failures}
        (output/'network.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(phases,indent=2))
    finally:
        for process,log in games:
            if process.returncode is None:
                import signal
                os.killpg(process.pid,signal.SIGTERM)
                await process.wait()
            log.close()
        server.close()
        await server.wait_closed()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--content-runtime',type=Path,default=ROOT/'runtime/isolated-host')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--delay-ms',type=float,default=0)
    parser.add_argument('--kib-per-second',type=float,default=0)
    asyncio.run(run(parser.parse_args()))
