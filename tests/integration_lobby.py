#!/usr/bin/env python3
"""Four actual native lobby processes: UDP browse, UI join/ready and preferences."""
import argparse,json,os,re,socket,subprocess,time
from pathlib import Path
from PIL import Image


def party(runtimes,output,asan,launchers=False):
    output.mkdir(parents=True,exist_ok=True)
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    processes=[]
    try:
        for index,runtime in enumerate(runtimes):
            runtime=runtime.resolve()
            for name in ['arcmp-lan-result.json','arcmp-lan-browser.bmp','arcmp-lan-ready.bmp']:(runtime/name).unlink(missing_ok=True)
            if launchers and (runtime/'arcmp-launch').exists():
                command=[str(runtime/'arcmp-launch'),'--launch','--runtime',str(runtime),'--']
            elif launchers and (runtime/'arcmp-launch.exe').exists():
                native='Z:'+str(runtime).replace('/','\\')
                command=['wine',str(runtime/'arcmp-launch.exe'),'--launch','--runtime',native,'--']
            else:
                root=runtime/'bin' if (runtime/'bin/arcanum-mp').exists() or (runtime/'bin/arcanum-mp.exe').exists() else runtime
                command=[str(root/'arcanum-mp')] if (root/'arcanum-mp').exists() else ['wine',str(root/'arcanum-mp.exe')]
            command+=['-window','--coop-engine-probe=lobby-lan',f'--coop-probe-player={index}',f'--coop-probe-port={port}']
            if not index:command+=['--coop-probe-host']
            env={**os.environ,'WINEDEBUG':'-all','SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy','SDL_RENDER_DRIVER':'software','ARCMP_DISCOVERY_TARGET':'127.0.0.1'}
            if runtime==asan:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            log=(output/f'game-{index}.log').open('wb');processes.append((subprocess.Popen(command,cwd=runtime,env=env,stdout=log,stderr=subprocess.STDOUT),log,runtime))
        until=time.monotonic()+40
        while any(process.poll() is None for process,_,_ in processes) and time.monotonic()<until:time.sleep(.1)
        results=[]
        for index,(process,log,runtime) in enumerate(processes):
            text=(output/f'game-{index}.log').read_text(errors='replace');assert process.poll()==0,text[-5000:]
            assert 'ARCMP engine probe PASS mode=lobby-lan' in text and not re.search('ERROR: AddressSanitizer|LeakSanitizer|runtime error:',text)
            result=json.loads((runtime/'arcmp-lan-result.json').read_text());assert result['verified'] and result['ready_mask']==15 and result['saved_port']==port
            if index:assert result['discovered']
            results.append(result);(output/f'lobby-{index}.json').write_text(json.dumps(result,indent=2)+'\n')
            for scene in ['browser','ready']:
                source=runtime/f'arcmp-lan-{scene}.bmp'
                if source.exists():
                    with Image.open(source) as image:assert image.size==(800,600);image.save(output/f'{scene}-{index}.png')
        (output/'summary.json').write_text(json.dumps({'four_actual_lobbies':True,'native_discovery_join_and_ready':True,'preferences_saved':True,'sanitizer_findings':0,'visual_review':'pending','results':results},indent=2)+'\n')
    finally:
        for process,log,_ in processes:
            if process.poll() is None:process.kill();process.wait(timeout=5)
            log.close()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,action='append',required=True);p.add_argument('--sanitized-runtime',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--launchers',action='store_true');a=p.parse_args();assert len(a.runtime)==4
    linux=[r for r in a.runtime if (r/'arcanum-mp').exists() or (r/'bin/arcanum-mp').exists()];windows=[r for r in a.runtime if (r/'arcanum-mp.exe').exists() or (r/'bin/arcanum-mp.exe').exists()];assert len(linux)==len(windows)==2
    asan=a.sanitized_runtime.resolve() if a.sanitized_runtime else None
    party(linux+windows,a.output/'linux-host',asan,a.launchers);party(windows+linux,a.output/'windows-host',asan,a.launchers);print('PASS: native LAN browse/join/readiness/preferences in both host directions')
