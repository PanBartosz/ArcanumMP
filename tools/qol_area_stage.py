"""Static scenery sweep, sourced in GDB. No campaign route or simulation ticks."""
gdb.execute('set $trip=(TeleportData*)calloc(1,sizeof(TeleportData))')
gdb.execute('set $trip->obj=player_get_local_pc_obj()')
gdb.execute('set $trip->map=1')
results={}
for area,label in [(21,'Tarant'),(6,'Qintarra'),(11,'Tulla'),(22,'Vendigroth')]:
 G('dialog_ui_reset()')
 gdb.execute('set $trip->loc=area_get_location(%d)'%area)
 assert bool(E('teleport_do($trip)'))
 G('teleport_ping(0)');G('dialog_ui_reset()');G('intgame_mode_set(INTGAME_MODE_MAIN)');G('intgame_show()')
 for zoom in [100,60]:
  G('coop_view_zoom_set(%d,640,280)'%zoom)
  key=label+'-'+str(zoom)+'-pan'
  path=out/(key+'.json')
  assert int(E('(int)qol_render((void*)iso_redraw,(void*)tig_window_display,(void*)gamelib_invalidate_rect,(void*)location_origin_scroll,2,120,%s)'%json.dumps(str(path))))==0
  results[key]=json.loads(path.read_text())
  assert int(E('tig_video_screenshot_save("%s.bmp")'%key))==0
(out/'render.json').write_text(json.dumps({'method':'120 warmed, in-process native camera redraw/composition/present calls per static scene; no campaign route or game-clock ticks; dummy/software SDL with vsync disabled; service time, not display FPS',
 'geometry':os.environ['ARCMP_QOL_GEOMETRY'],'results':results},indent=2)+'\n')
print('QOL_AREAS_PASS',flush=True)
