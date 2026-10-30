"""Native UI composition review only; sourced by benchmark_qol.py inside GDB."""
def rect(expr):return {k:int(E(expr)[k]) for k in ['x','y','width','height']}
def shot(name):
 G('coop_play_tick(SDL_GetTicks())');G('coop_party_ui_tick(SDL_GetTicks())')
 G('gamelib_redraw()');G('iso_redraw()');G('tig_window_display()')
 assert int(E('tig_video_screenshot_save("%s.bmp")'%name))==0
def inspect():
 return {k:rect(v) for k,v in [('party',"'party_ui.c'::party_rect"),('turns',"'party_ui.c'::turn_rect"),
  ('actions',"'play_ui.c'::bar_rect"),('preview',"'modern_world.c'::preview_rect"),('objectives',"'play_ui.c'::tracker_rect")]}

gdb.execute('set $hero=player_get_local_pc_obj()')
gdb.execute("set $saved_runtime='runtime.c'::runtime")
gdb.execute("set 'runtime.c'::runtime=(CoopRuntime*)calloc(1,sizeof(CoopRuntime))")
gdb.execute("set 'runtime.c'::runtime->phase=COOP_RUNTIME_PLAYING")
G('coop_hud_reset_layout()');G('coop_hud_log_state(0,1)')
G('coop_play_pref_set(PLAY_PREF_BAR,1)');G('coop_play_pref_set(PLAY_PREF_PREVIEW,1)')
G('coop_play_note_set("Meet Virgil and search the wreckage.")')
gdb.execute('set $hero_loc=obj_field_int64_get($hero,OBJ_F_LOCATION)')
for i,name in enumerate(['Silas','Morgan','Alaric','Lucan']):
 G('obj_field_string_set(coop_campaign_object(%d),OBJ_F_PC_PLAYER_NAME,"%s")'%(i,name))
G('stat_base_set($hero,STAT_WILLPOWER,20)');G('spell_add($hero,SPELL_HARM,1)')
G('spell_add($hero,SPELL_MINOR_HEALING,1)')
gdb.execute('set $action=(PlayRow*)calloc(1,sizeof(PlayRow))')
gdb.execute('set $action->kind=HOTKEY_SPELL');gdb.execute('set $action->available=1')
for slot,spell in [(0,'SPELL_HARM'),(1,'SPELL_MINOR_HEALING')]:
 gdb.execute('set $action->id='+spell);G('coop_play_action_assign(%d,$action)'%slot)
G('combat_critter_activate_combat_mode($hero)');assert bool(E('sub_4B6C90(1)'))
G('combat_turn_based_whos_turn_set($hero)')
G('obj_field_int32_set(coop_campaign_object(1),OBJ_F_HP_DAMAGE,8)')
G('obj_field_int32_set(coop_campaign_object(2),OBJ_F_CRITTER_FATIGUE_DAMAGE,critter_fatigue_max(coop_campaign_object(2))-5)')
G('intgame_mode_set(INTGAME_MODE_MAIN)');G('tig_mouse_set_position(640,300)')
G('coop_play_event(PLAY_COMBAT,$hero,coop_campaign_object(1),"Silas hits a wolf for 8 damage.","A native combat log entry staged for UI composition.")')
G('coop_play_event(PLAY_EFFECT,$hero,$hero,"Strength of Earth fades from Silas.","Staged log text.")')
G('coop_play_event(PLAY_COMBAT,coop_campaign_object(1),$hero,"Morgan misses the wolf.","Staged log text.")')
results={}
for scale in [100,150]:
 G('coop_play_pref_set(PLAY_PREF_TEXT,%d)'%scale);G('coop_hud_refresh()')
 G('coop_party_ui_tick(SDL_GetTicks())');G('coop_play_ui_tick(SDL_GetTicks())')
 G('tig_mouse_set_position(400,280)')
 shot('hud-%d'%scale);results[str(scale)]=inspect()
 G('coop_hud_log_state(0,1)');assert bool(E('coop_play_ui_open(PLAY_TAB_LOG)'))
 shot('log-%d'%scale);results[str(scale)]['log']=rect("'play_ui.c'::bounds")
 # Exercise the real compact-row hit box and preserve the selected event.
 if bool(E("'play_ui.c'::compact")):
  gdb.execute('set $msg=(TigMessage*)calloc(1,sizeof(TigMessage))')
  gdb.execute('set $msg->type=TIG_MESSAGE_MOUSE')
  gdb.execute('set $msg->data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN')
  lr=results[str(scale)]['log']
  gdb.execute('set $msg->data.mouse.x=%d'%(lr['x']+30))
  gdb.execute('set $msg->data.mouse.y=%d'%(lr['y']+int(E('coop_play_line_height()'))+34))
  G("'play_ui.c'::input($msg)")
  # Earlier builds select a row; polished builds also open its full details.
  results[str(scale)]['entry_opens_details']=not bool(E("'play_ui.c'::compact"))
  shot('log-details-%d'%scale)
 G('coop_play_ui_close()')
 G('combat_turn_based_whos_turn_set(coop_campaign_object(1))')
 shot('waiting-%d'%scale)
 G('combat_turn_based_whos_turn_set($hero)')
 G('tig_mouse_set_position(400,280)');shot('active-%d'%scale)
 # Hover uses the actual native party card and tooltip creation code.
 pr=rect("'party_ui.c'::party_rect")
 G('tig_mouse_set_position(%d,%d)'%(pr['x']+25,pr['y']+30))
 G('coop_party_ui_tick(SDL_GetTicks())');G('SDL_Delay(700)');G('coop_party_ui_tick(SDL_GetTicks())')
 shot('hover-%d'%scale)
 if int(E("'party_ui.c'::tip"))!=-1:
  gdb.execute('set $data=(TigWindowData*)calloc(1,sizeof(TigWindowData))')
  G("tig_window_data('party_ui.c'::tip,$data)");results[str(scale)]['tooltip']=rect('$data->rect')
 G('tig_mouse_set_position(640,300)');G('coop_party_ui_tick(SDL_GetTicks())')
 for tab in [1,2,3,4,5]:
  assert bool(E('coop_play_ui_open(%d)'%tab));shot('tools-%d-%d'%(scale,tab));G('coop_play_ui_close()')
# Focused layout-control regression: actual title drag, corner resize and opacity.
G('coop_hud_log_state(0,1)');assert bool(E('coop_play_ui_open(PLAY_TAB_LOG)'))
lr=rect("'play_ui.c'::bounds")
def mouse(event,x,y):
 gdb.execute('set $msg->data.mouse.event='+event)
 gdb.execute('set $msg->data.mouse.x=%d'%x);gdb.execute('set $msg->data.mouse.y=%d'%y)
 G("'play_ui.c'::input($msg)")
for event,x,y in [('TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN',lr['x']+50,lr['y']+12),
 ('TIG_MESSAGE_MOUSE_MOVE',lr['x']+20,lr['y']-8),('TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP',lr['x']+20,lr['y']-8)]:mouse(event,x,y)
moved=rect("'play_ui.c'::bounds")
assert moved['x']==lr['x']-30 and moved['y']==lr['y']-20,moved
for event,extra in [('TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN',0),('TIG_MESSAGE_MOUSE_MOVE',20),('TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP',20)]:
 mouse(event,moved['x']+moved['width']-5+extra,moved['y']+moved['height']-5+extra)
resized=rect("'play_ui.c'::bounds")
assert resized['width']==moved['width']+20 and resized['height']==moved['height']+20,resized
assert bool(E('coop_hud_opacity_set(HUD_LOG_COMPACT,75)'))
gdb.execute('set $alpha=(unsigned char*)calloc(1,1)')
assert int(E("tig_window_opacity_get('play_ui.c'::window,$alpha)"))==0 and int(E('*$alpha'))==191
G('coop_play_ui_close()');assert bool(E('coop_play_ui_open(PLAY_TAB_LOG)'))
assert rect("'play_ui.c'::bounds")==resized
shot('custom-log');G('coop_play_ui_close()')
gdb.execute('set $custom=(TigRect*)calloc(1,sizeof(TigRect))')
for k,v in [('x',140),('y',200),('width',480),('height',150)]:gdb.execute('set $custom->%s=%d'%(k,v))
assert bool(E('coop_hud_store(HUD_PREVIEW,$custom)'));G('coop_hud_refresh()')
sw,sh=map(int,os.environ['ARCMP_QOL_GEOMETRY'].split('x'))
G('tig_mouse_set_position(%d,120)'%(sw-90));shot('custom-preview')
assert rect("'modern_world.c'::preview_rect")=={'x':140,'y':200,'width':480,'height':150}
# Paused presentation uses a staged controls view; no network session is claimed.
gdb.execute("set 'runtime.c'::runtime->controls=(CoopControls*)calloc(1,sizeof(CoopControls))")
gdb.execute("set 'runtime.c'::runtime->controls->view.paused=1")
G('coop_play_pause_announce("Party paused")');shot('paused')
gdb.execute("set 'runtime.c'::runtime->controls->view.paused=0")
results['controls']={'title_drag':True,'corner_resize':True,'opacity':75,'reopened_geometry':resized,'custom_preview':True}
G('coop_party_ui_close()')
gdb.execute("set 'runtime.c'::runtime=$saved_runtime")
(out/'ui-review.json').write_text(json.dumps({'geometry':os.environ['ARCMP_QOL_GEOMETRY'],'panels':results},indent=2)+'\n')
print('UI_REVIEW_PASS',flush=True)
