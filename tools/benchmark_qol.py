#!/usr/bin/env python3
"""Time native redraws and retain visual checks in a new private runtime.

Requires an existing content manifest, Linux debug binary, GDB and C compiler.
No campaign routing, retail-file edits or production performance hooks.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from prepare_runtime import immutable_asset

ROOT = Path(__file__).resolve().parents[1]


def prepare(source, runtime, binary):
    if runtime.exists():
        raise ValueError(f'Refusing to overwrite a benchmark runtime: {runtime}')
    runtime.mkdir(parents=True)
    for entry in json.loads((source / 'arcmp-content.json').read_text()):
        original = source
        for part in Path(entry['path']).parts:
            original = next(p for p in original.iterdir() if p.name.casefold() == part.casefold())
        target = runtime / original.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        if immutable_asset(source,original):
            target.symlink_to(original.resolve())
        else:
            shutil.copy2(original, target)
    for name in ('arcmp-content.json', 'arcmp-content.id', 'arcanum.cfg'):
        if (source / name).exists():
            shutil.copy2(source / name, runtime / name)
    for original in source.rglob('*'):
        if original.is_file() and original.suffix.casefold() in {'.bik', '.mp3', '.wav'}:
            target = runtime / original.relative_to(source)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.symlink_to(original.resolve())
    (runtime / 'arcanum-mp').symlink_to(binary)
    (runtime / 'HighRes').mkdir(exist_ok=True)
    (runtime / 'HighRes/config.ini').write_text('[Main]\nWidth=1280\nHeight=720\nWindowed=1\nLogos=0\nIntro=0\n')


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    runtime = output / 'runtime'
    binary = args.binary.resolve()
    prepare(args.content_runtime.resolve(), runtime, binary)
    helper = output / 'benchmark.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-g', '-O2', str(ROOT / 'tools/qol_benchmark.c'), '-o', str(helper)], check=True)
    script = output / 'render.gdb'
    script.write_text('''set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
break verify_dialogue_semantics
run
python
import gdb,json,os
from pathlib import Path
out=Path(os.environ['ARCMP_QOL_OUT'])
E=gdb.parse_and_eval
def G(s): return gdb.execute('call '+s,to_string=True)
assert bool(E('assemble_party_roster(0,15)'))
G('mainmenu_ui_begin_campaign(coop_campaign_member(0)->prototype)')
G('teleport_ping(0)'); G('coop_authority_set(COOP_AUTHORITY_HOST)')
G('iso_interface_refresh()'); G('intgame_show()'); G('intgame_mode_set(INTGAME_MODE_MAIN)')
G('coop_play_pref_set(PLAY_PREF_AUTOPAUSE,0)'); G('coop_play_pref_set(PLAY_PREF_BAR,1)')
G('coop_play_ui_close()'); G('tig_mouse_hide()'); G('object_hover_obj_set(0)')
G("SDL_SetRenderVSync('video.c'::tig_video_state.renderer,0)")
results={}
for zoom in [100,60,150,200]:
 G('coop_view_zoom_set(%d,640,280)'%zoom)
 for name,mode in [('idle',0),('redraw',1),('pan',2)]:
  key=str(zoom)+'-'+name
  path=out/(key+'.json')
  assert int(E('(int)qol_render((void*)iso_redraw,(void*)tig_window_display,(void*)gamelib_invalidate_rect,(void*)location_origin_scroll,%d,240,%s)'%(mode,json.dumps(str(path)))))==0
  results[key]=json.loads(path.read_text())
 G('gamelib_redraw()'); G('iso_redraw()'); G('tig_window_display()')
 assert int(E('tig_video_screenshot_save("zoom-%d.bmp")'%zoom))==0
(out/'render.json').write_text(json.dumps({'method':'240 warmed native redraw/composition/present calls per scene, CLOCK_MONOTONIC in-process, dummy/software SDL; service time, not display FPS','geometry':os.environ['ARCMP_QOL_GEOMETRY'],'results':results},indent=2)+'\\n')
print('QOL_RENDER_PASS',flush=True)
end
quit
''')
    if args.visual_only:
        setup=script.read_text().split('results={}')[0]
        script.write_text(setup+'exec(open('+repr(str(ROOT/'tools/qol_visual_stage.py'))+').read())\nend\nquit\n')
    elif args.ui_review:
        setup=script.read_text().split('results={}')[0]
        script.write_text(setup+'exec(open('+repr(str(ROOT/'tools/ui_polish_stage.py'))+').read())\nend\nquit\n')
    elif args.areas:
        setup=script.read_text().split('results={}')[0]
        script.write_text(setup+'exec(open('+repr(str(ROOT/'tools/qol_area_stage.py'))+').read())\nend\nquit\n')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software',
               ARCMP_QOL_OUT=str(output), ARCMP_QOL_GEOMETRY=args.geometry)
    preload=str(helper)
    if args.sanitized:
        asan=subprocess.check_output(['cc','-print-file-name=libasan.so'],text=True).strip()
        preload=asan+':'+preload
        env.update(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with (output / 'render.log').open('wb') as log:
        result = subprocess.run(['gdb', '-q', '-batch', '-iex', 'set environment LD_PRELOAD '+preload, '-x', str(script), '--args',
            str(runtime/'arcanum-mp'), '-window', '-geometry', args.geometry,
            '--coop-engine-probe=dialogue-semantics'], cwd=runtime, env=env,
            stdout=log, stderr=subprocess.STDOUT, timeout=180)
    marker='QOL_VISUAL_PASS' if args.visual_only else 'UI_REVIEW_PASS' if args.ui_review else 'QOL_AREAS_PASS' if args.areas else 'QOL_RENDER_PASS'
    if result.returncode or marker not in (output/'render.log').read_text(errors='replace'):
        raise RuntimeError((output/'render.log').read_text(errors='replace')[-4000:])
    from PIL import Image
    for image in runtime.glob('*.bmp'):
        with Image.open(image) as frame:
            frame.save(output / (image.stem+'.png'))
        image.unlink()
    (output/'build.json').write_text(json.dumps({'binary':str(binary),
        'sha256':hashlib.file_digest(binary.open('rb'),'sha256').hexdigest(),
        'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip())},indent=2)+'\n')
    if args.visual_only:
        import numpy as np
        checks=json.loads((output/'visual.json').read_text())
        pixel_cases=['copy','edge','scale-50','scale-75','scale-125','scale-150','scale-200','alpha-move','text-measure','pose-visibility']
        for name in pixel_cases:
            actual=np.asarray(Image.open(output/(name+'-actual.png')).convert('RGB'))
            expected=np.asarray(Image.open(output/(name+'-expected.png')).convert('RGB'))
            checks[name]={'different_pixels':int(np.any(actual!=expected,axis=2).sum())}
        (output/'visual.json').write_text(json.dumps(checks,indent=2)+'\n')
        if args.require_clean:
            assert all(checks[k]['different_pixels']==0 for k in pixel_cases),checks
            assert checks['duplicate_pose_residual_pixels']==0,checks
            assert checks['tooltip_fits'],checks
        print(json.dumps(checks,indent=2))
    elif args.ui_review:
        review=json.loads((output/'ui-review.json').read_text())
        sw,sh=map(int,args.geometry.split('x'))
        checks={}
        def overlaps(a,b):
            return a['x']<b['x']+b['width'] and b['x']<a['x']+a['width'] and a['y']<b['y']+b['height'] and b['y']<a['y']+a['height']
        for scale in ['100','150']:
            panels=review['panels'][scale]
            for a,b in [('party','turns'),('preview','actions'),('preview','log'),('actions','log'),('objectives','log')]:
                checks[scale+'-'+a+'-'+b]=not overlaps(panels[a],panels[b])
            checks[scale+'-native-ap-clear']=all(panels[k]['y']+panels[k]['height']<=sh-194 for k in ['preview','actions','log'])
            checks[scale+'-entry-details']=panels['entry_opens_details']
            checks[scale+'-on-screen']=all(0<=r['x'] and 0<=r['y'] and r['x']+r['width']<=sw and r['y']+r['height']<=sh
                for r in panels.values() if isinstance(r,dict) and 'width' in r)
        review['checks']=checks
        (output/'ui-review.json').write_text(json.dumps(review,indent=2)+'\n')
        if args.require_clean: assert all(checks.values()),checks
        print(json.dumps(review,indent=2))
    else:
        print(json.dumps(json.loads((output/'render.json').read_text())['results'],indent=2))


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--content-runtime',type=Path,default=ROOT/'runtime/isolated-host')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--geometry',default='1280x720')
    parser.add_argument('--visual-only',action='store_true')
    parser.add_argument('--require-clean',action='store_true')
    parser.add_argument('--sanitized',action='store_true')
    parser.add_argument('--areas',action='store_true',help='directly load four scenery areas for camera/render timings; no quest routing')
    parser.add_argument('--ui-review',action='store_true',help='capture staged native HUD layouts, tooltips and tools for visual review')
    run(parser.parse_args())
