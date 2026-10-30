"""Public showcase: four real owners, separate paths, equipped party and city combat."""
import gdb,json,os,re,time
from pathlib import Path
ROOT=Path(os.environ['ARCMP_SHOWCASE_ROOT']);out=Path(os.environ['ARCMP_SHOWCASE_OUT'])
index=int(os.environ['ARCMP_SHOWCASE_PLAYER'])
def expression(s):return re.sub(r'location_make\((-?\d+),\s*(-?\d+)\)',lambda m:str((int(m[1])&0xffffffff)|((int(m[2])&0xffffffff)<<32)),s)
E=lambda s:gdb.parse_and_eval(expression(s))
def G(s):
 return gdb.execute('call '+expression(s),to_string=True)
V=lambda s:int(E(s))
def P(name):return re.search(r'^#define '+name+r'\s+(\d+)',(ROOT/'upstream/arcanum-ce/src/game/descriptions.h').read_text(),re.M)[1]
def pump(t):G('showcase_pump(%d)'%round(t*1000))
G('showcase_init((void*)tig_ping,(void*)gamelib_ping,(void*)iso_redraw,(void*)tig_window_display,(void*)tig_message_dequeue,(void*)intgame_process_event,(void*)tig_mouse_set_position)')
G('showcase_network(coop_runtime_session(),(void*)coop_session_poll,(void*)coop_runtime_tick,(void*)SDL_GetTicks)')
gdb.execute('set $hero=player_get_local_pc_obj()');gdb.execute('set $ptr=(int64_t*)calloc(1,8)')
gdb.execute('set $trip=(TeleportData*)calloc(1,sizeof(TeleportData))')
gdb.execute('set $td=(TargetDescriptor*)calloc(1,sizeof(TargetDescriptor))')
gdb.execute('set $portrait=(int*)calloc(1,4)')
gdb.execute('set $cues=(ShowcaseCue*)calloc(12,sizeof(ShowcaseCue))')
G('coop_play_pref_set(PLAY_PREF_AUTOPAUSE,0)');G('coop_play_pref_set(PLAY_PREF_TEXT,100)')
G('coop_play_pref_set(PLAY_PREF_BAR,1)');G('coop_play_pref_set(PLAY_PREF_SYMBOLS,0)')
G('coop_play_pref_set(PLAY_PREF_PREVIEW,1)');G('coop_hud_reset_layout()');G('coop_hud_log_state(0,1)')
G('tig_mouse_set_position(1180,650)');G('tig_mouse_hide()')
events=[]
def mark(name):
 e={'name':name,'monotonic':time.monotonic(),'frame':V("'pump.c'::record_frames"),'fps':30}
 events.append(e);(out/('actions-%d.json'%index)).write_text(json.dumps(events,indent=2))
 if index==0:(out/'shots.json').write_text(json.dumps(events,indent=2))
 print('DIRECTOR',index,name,flush=True)
def wait_file(name):
 deadline=time.monotonic()+45
 while not (out/name).exists():
  assert time.monotonic()<deadline,name
  pump(.15)
 return json.loads((out/name).read_text())
def travel(area,name):
 if index==0:
  gdb.execute('set $trip->obj=player_get_local_pc_obj()');gdb.execute('set $trip->map=1')
  gdb.execute('set $trip->flags=512');gdb.execute('set $trip->loc=area_get_location(%d)'%area)
  assert bool(E('teleport_do($trip)'))
  deadline=time.monotonic()+45
  while V('coop_runtime_phase()')!=V('COOP_RUNTIME_PLAYING') or V('map_current_map()')!=1 or bool(E('coop_runtime_entry_pending()')):
   assert time.monotonic()<deadline,gdb.execute('p coop_runtime_error()',to_string=True)
   pump(.15)
  loc=V('obj_field_int64_get(player_get_local_pc_obj(),OBJ_F_LOCATION)')
  origin={'x':loc&0xffffffff,'y':loc>>32,'start':time.monotonic()+4}
  (out/name).write_text(json.dumps(origin))
 else:origin=wait_file(name)
 while V('coop_runtime_phase()')!=V('COOP_RUNTIME_PLAYING') or V('map_current_map()')!=1:pump(.15)
 gdb.execute('set $hero=player_get_local_pc_obj()')
 G('location_origin_set(location_make(%d,%d))'%(origin['x']+2,origin['y']+2))
 G('iso_interface_refresh()');G('intgame_show()');G('coop_view_zoom_set(115,640,300)')
 G('dialog_ui_reset()');G('intgame_mode_set(INTGAME_MODE_MAIN)')
 return origin
if index==0:
 G('showcase_hide_npcs((void*)obj_inst_first,(void*)obj_inst_next,(void*)obj_field_int32_get,(void*)object_flags_set,OBJ_F_TYPE,OBJ_TYPE_NPC)')
 for i,name in enumerate(['Silas','Morgan','Alaric','Lucan']):
  hero=V('coop_campaign_object(%d)'%i)
  G('obj_field_string_set(%d,OBJ_F_PC_PLAYER_NAME,%s)'%(hero,json.dumps(name)))
  G('obj_field_int32_set(%d,OBJ_F_HP_PTS,180)'%hero);G('obj_field_int32_set(%d,OBJ_F_HP_DAMAGE,0)'%hero)
  G('critter_fatigue_pts_set(%d,300)'%hero)
  for stat,value in [('STAT_STRENGTH',14),('STAT_DEXTERITY',16),('STAT_WILLPOWER',20),('STAT_PERCEPTION',16)]:G('stat_base_set(%d,%s,%d)'%(hero,stat,value))
  for skill in ['BASIC_SKILL_MELEE','BASIC_SKILL_BOW','BASIC_SKILL_DODGE']:G('basic_skill_points_set(%d,%s,16)'%(hero,skill))
  G('tech_skill_points_set(%d,TECH_SKILL_FIREARMS,16)'%hero)
  for spell in ['SPELL_HARM','SPELL_MINOR_HEALING','SPELL_FIREFLASH','SPELL_STRENGTH_OF_EARTH']:G('spell_add(%d,%s,1)'%(hero,spell))
  for proto,slot in [(['BP_CHAINMAIL','BP_LEATHER_ARMOUR','BP_NICE_SUIT','BP_PRIEST_ROBES'][i],1006),('BP_BOOTS',1008),(['BP_SWORD','BP_BOW','BP_REVOLVER','BP_SWORD'][i],1004)]:
   assert bool(E('object_create(sub_4685A0(%s),obj_field_int64_get(%d,OBJ_F_LOCATION),$ptr)'%(P(proto),hero)))
   assert bool(E('item_transfer(*$ptr,%d)'%hero))
   assert bool(E('item_wield_set(*$ptr,%d)'%slot))
   if slot==1004:
    ammo=V('item_weapon_ammo_type(*$ptr)')
    if ammo!=10000:G('item_transfer(item_ammo_create(250,%d,obj_field_int64_get(%d,OBJ_F_LOCATION)),%d)'%(ammo,hero,hero))
  pc_flags=V('obj_field_int32_get(%d,OBJ_F_PC_FLAGS)'%hero)
  G('obj_field_int32_set(%d,OBJ_F_PC_FLAGS,%d)'%(hero,pc_flags&~4))
  assert bool(E('portrait_find_first(%d,$portrait)'%hero))
  G('obj_field_int32_set(%d,OBJ_F_CRITTER_PORTRAIT,*$portrait)'%hero)
  for n in range(i*3):
   G('portrait_find_next(%d,$portrait)'%hero)
   G('obj_field_int32_set(%d,OBJ_F_CRITTER_PORTRAIT,*$portrait)'%hero)
  G('obj_field_int32_set(%d,OBJ_F_CRITTER_PORTRAIT,*$portrait)'%hero)
  G('obj_field_int32_set(%d,OBJ_F_PC_FLAGS,%d)'%(hero,pc_flags))
origin=travel(21,'tarant-ready.json');ox,oy=origin['x'],origin['y']
s=E("'video.c'::tig_video_state.surface")
G('showcase_record_open(%s,%s,(void*)%d,%d,%d,%d)'%(json.dumps(str(out/('peer-%d.bgr'%index))),json.dumps(str(out/('peer-%d.frames.json'%index))),int(s['pixels']),int(s['w']),int(s['h']),int(s['pitch'])))
while time.monotonic()<origin['start']:pump(.1)
def sequence(cues,seconds):
 for n,(frame,kind,target,spell,descriptor) in enumerate(cues):
  for field,value in [('frame',frame),('kind',kind),('target',target),('spell',spell)]:gdb.execute('set $cues[%d].%s=%d'%(n,field,value))
  gdb.execute('set $cues[%d].descriptor=%s'%(n,descriptor))
 G('showcase_sequence((void*)coop_runtime_move,(void*)coop_party_ui_target_actor,(void*)coop_runtime_cast,$hero,$cues,%d,%d,(void*)obj_field_int64_get,OBJ_F_LOCATION,(void*)target_descriptor_set_loc)'%(len(cues),seconds*1000))
def loc(x,y):return (x&0xffffffff)|((y&0xffffffff)<<32)
paths=[[(7,0),(4,5),(-2,4)],[(-4,5),(-7,0),(1,-4)],[(3,-5),(8,-3),(6,4)],[(-6,-3),(-2,-7),(1,-1)]]
mark('independent-paths')
sequence([(30+n*120,0,loc(ox+x,oy+y),0,'0') for n,(x,y) in enumerate(paths[index])],14)
mark('tarant-complete')
origin=travel(11,'tulla-ready.json');ox,oy=origin['x'],origin['y']
G('coop_view_zoom_set(140,640,300)')
if index==0:
 G('showcase_hide_npcs((void*)obj_inst_first,(void*)obj_inst_next,(void*)obj_field_int32_get,(void*)object_flags_set,OBJ_F_TYPE,OBJ_TYPE_NPC)')
 for i in range(4):
  G('sub_43E770(coop_campaign_object(%d),location_make(%d,%d),0,0)'%(i,ox-2+(i%2)*3,oy-3+(i//2)*3))
 for j,proto in enumerate(['BP_ORC_BERSERKER','BP_ORC_GRUNT','BP_ORC_SHAMAN','BP_ORE_GOLEM']):
  assert bool(E('object_create(sub_4685A0(%s),location_make(%d,%d),$ptr)'%(P(proto),ox+6+(j%2)*3,oy-3+(j//2)*4)))
  G('obj_field_int32_set(*$ptr,OBJ_F_NPC_PAD_I_1,%d)'%(799100+j))
  G('obj_field_int32_set(*$ptr,OBJ_F_HP_PTS,140)');G('obj_field_int32_set(*$ptr,OBJ_F_HP_DAMAGE,0)')
 (out/'battle-ready.json').write_text(json.dumps({'start':time.monotonic()+5}))
ready=wait_file('battle-ready.json')
while time.monotonic()<ready['start']:pump(.1)
# All targets are real replicated native critters; no dummy props.
deadline=time.monotonic()+12
while not V("'magic_probe.c'::target(%d)"%(index+100)):
 assert time.monotonic()<deadline
 pump(.15)
enemy=V("'magic_probe.c'::target(%d)"%(index+100))
G('combat_auto_attack_set(1)')
if index==0:
 for j in range(4):G('ai_attack(\'magic_probe.c\'::target(%d),coop_campaign_object(%d),0,0)'%(j+100,j))
G('target_descriptor_set_loc($td,location_make(%d,%d))'%(ox+7,oy-1))
mark('tulla-battle')
cues=[(25,1,enemy,0,'0')]
if index==3:cues=[(60,2,enemy,V('SPELL_FIREFLASH'),'$td'),(210,1,enemy,0,'0'),(330,2,enemy,V('SPELL_FIREFLASH'),'$td')]
sequence(cues,17)
mark('tulla-battle-end')
if index==0:
 G('coop_runtime_combat(COOP_COMBAT_POLICY,1,0)');G('coop_runtime_combat(COOP_COMBAT_STANCE,1,0)')
 (out/'turns-ready.json').write_text(json.dumps({'start':time.monotonic()+2}))
ready=wait_file('turns-ready.json')
while time.monotonic()<ready['start']:pump(.1)
G('coop_play_ui_open(0)');mark('turn-order-and-log');pump(7)
G('coop_play_ui_close()');mark('complete');G('showcase_record_close()')
print('DIRECTOR_COMPLETE',index,flush=True)
