"""Continuous native pixel pans, captured once per completed native draw."""
import os
from pathlib import Path
exec(open(Path(os.environ['ARCMP_SHOWCASE_ROOT'])/'tools/showcase/stage.py').read().split('# A little native world movement')[0])
G('coop_play_pref_set(PLAY_PREF_BAR,0)');G('coop_play_pref_set(PLAY_PREF_PREVIEW,0)')
G('coop_play_pref_set(PLAY_PREF_LABELS,0)');G('tig_mouse_set_position(1240,690)')
G('coop_play_pref_set(PLAY_PREF_SYMBOLS,0)');G('tig_mouse_hide()')
gdb.execute('set $tp=(TeleportData*)calloc(1,sizeof(TeleportData))')
gdb.execute('set $tp->obj=$hero');gdb.execute('set $tp->map=1')
def record_begin():
 s=E("'video.c'::tig_video_state.surface")
 args=[str(out/'peer-0.bgr'),str(out/'peer-0.frames.json')]
 G('showcase_record_open(%s,%s,(void*)%d,%d,%d,%d)'%(string(args[0]),string(args[1]),int(s['pixels']),int(s['w']),int(s['h']),int(s['pitch'])))
if not os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):record_begin()
def mark(name):
 shots.append({'name':name,'monotonic':time.monotonic(),
               'frame':V("'pump.c'::record_frames"),'fps':30})
 (out/'shots.json').write_text(json.dumps(shots,indent=2))
 print('SHOWCASE_SHOT',name,flush=True)
for area_id,label in [(21,'Tarant'),(6,'Qintarra'),(11,'Tulla'),(22,'Vendigroth Ruins')]:
 G('dialog_ui_reset()');gdb.execute('set $tp->loc=area_get_location(%d)'%area_id)
 G('teleport_do($tp)');G('teleport_ping(0)');G('dialog_ui_reset()')
 G('intgame_mode_set(INTGAME_MODE_MAIN)');G('intgame_show()')
 G('spell_ui_cancel()');G('coop_view_zoom_set(125,640,270)')
 G('tig_mouse_set_position(1240,690)');refresh()
 # Pan three screen pixels per frame at 30fps. No location/tile stepping.
 mark('pan-'+label)
 G('showcase_pan((void*)scroll_drag_by,-360,0,4000)')
mark('complete');G('showcase_record_close()')
