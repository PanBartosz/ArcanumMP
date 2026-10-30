"""Film a staged encounter with four real TCP-connected game processes.

All player movement, attacks, pings and spells use ordinary owner requests.
Native enemies retaliate. Equipment/stats/positions are prepared for filming;
this is development showcase footage, not an ordinary-party balance run.
"""
import gdb,json,os,time,re
from pathlib import Path
ROOT=Path(os.environ['ARCMP_SHOWCASE_ROOT'])
out=Path(os.environ['ARCMP_SHOWCASE_OUT']);index=int(os.environ['ARCMP_SHOWCASE_PLAYER'])
E=gdb.parse_and_eval
def G(s):
 try:return gdb.execute('call '+s,to_string=True)
 except Exception:
  print('DIRECTOR_CALL_FAILED',s,flush=True)
  print(gdb.execute('bt',to_string=True),flush=True)
  raise
V=lambda s:int(E(s))
def P(name):
 return re.search(r'^#define '+name+r'\s+(\d+)',(ROOT/'upstream/arcanum-ce/src/game/descriptions.h').read_text(),re.M)[1] if name.startswith('BP_') else name
print('DIRECTOR_ENTERED',index,flush=True)
G('showcase_init((void*)tig_ping,(void*)gamelib_ping,(void*)iso_redraw,(void*)tig_window_display,(void*)tig_message_dequeue,(void*)intgame_process_event,(void*)tig_mouse_set_position)')
G('showcase_network(coop_runtime_session(),(void*)coop_session_poll,(void*)coop_runtime_tick,(void*)SDL_GetTicks)')
def pump(seconds): G('showcase_pump(%d)'%round(seconds*1000))
def mark(name):
 events.append({'name':name,'monotonic':time.monotonic()})
 (out/f'actions-{index}.json').write_text(json.dumps(events,indent=2))
 print('DIRECTOR',index,name,flush=True)
events=[]
gdb.execute('set $hero=player_get_local_pc_obj()')
gdb.execute('set $ptr=(int64_t*)calloc(1,8)')
gdb.execute('set $portrait=(int*)calloc(1,4)')
gdb.execute('set $td=(TargetDescriptor*)calloc(1,sizeof(TargetDescriptor))')
gdb.execute('set $combat_state=(CombatCampaignState*)calloc(1,sizeof(CombatCampaignState))')
gdb.execute('set $msg=(TigMessage*)calloc(1,sizeof(TigMessage))')
gdb.execute('set $msg->type=TIG_MESSAGE_MOUSE')
G('coop_play_pref_set(PLAY_PREF_AUTOPAUSE,0)')
G('coop_play_pref_set(PLAY_PREF_TEXT,100)');G('coop_play_pref_set(PLAY_PREF_BAR,%d)'%(0 if os.getenv('ARCMP_SHOWCASE_CLEAN') else 1))
G('coop_hud_reset_layout()');G('coop_hud_log_state(0,1)')

if index==0:
 # Prevent the opening scripted conversation during a purpose-built fight.
 G('showcase_hide_npcs((void*)obj_inst_first,(void*)obj_inst_next,(void*)obj_field_int32_get,(void*)object_flags_set,OBJ_F_TYPE,OBJ_TYPE_NPC)')
 origin=V('obj_field_int64_get($hero,OBJ_F_LOCATION)')
 ox=V('location_get_x(%d)'%origin);oy=V('location_get_y(%d)'%origin)
 names=['Silas','Morgan','Alaric','Lucan']
 for i in range(4):
  pc=V('coop_campaign_object(%d)'%i)
  G('obj_field_string_set(%d,OBJ_F_PC_PLAYER_NAME,%s)'%(pc,json.dumps(names[i])))
  G('sub_43E770(%d,location_make(%d,%d),0,0)'%(pc,ox+(i%2)*2,oy+(i//2)*2))
  G('obj_field_int32_set(%d,OBJ_F_HP_PTS,160)'%pc)
  G('obj_field_int32_set(%d,OBJ_F_HP_DAMAGE,0)'%pc)
  G('critter_fatigue_pts_set(%d,250)'%pc);G('critter_fatigue_damage_set(%d,0)'%pc)
  for stat,value in [('STAT_STRENGTH',14),('STAT_DEXTERITY',16),('STAT_PERCEPTION',16),('STAT_INTELLIGENCE',16)]:
   G('stat_base_set(%d,%s,%d)'%(pc,stat,value))
  for skill in ['BASIC_SKILL_MELEE','BASIC_SKILL_BOW','BASIC_SKILL_DODGE']:
   G('basic_skill_points_set(%d,%s,16)'%(pc,skill))
  G('tech_skill_points_set(%d,TECH_SKILL_FIREARMS,16)'%pc)
  for spell in ['SPELL_HARM','SPELL_MINOR_HEALING','SPELL_STRENGTH_OF_EARTH','SPELL_FIREFLASH','SPELL_ORCISH_CHAMPION']:
   G('spell_add(%d,%s,1)'%(pc,spell))
  G('portrait_find_first(%d,$portrait)'%pc)
  for n in range(i*3): G('portrait_find_next(%d,$portrait)'%pc)
  G('obj_field_int32_set(%d,OBJ_F_CRITTER_PORTRAIT,*$portrait)'%pc)
  proto=P(['BP_SWORD','BP_BOW','BP_REVOLVER','BP_SWORD'][i])
  if bool(E('object_create(sub_4685A0(%s),obj_field_int64_get(%d,OBJ_F_LOCATION),$ptr)'%(proto,pc))):
   G('item_transfer_ex(*$ptr,%d,1004)'%pc)
   ammo=V('item_weapon_ammo_type(*$ptr)')
   if ammo!=10000:
    G('item_transfer(item_ammo_create(200,%d,obj_field_int64_get(%d,OBJ_F_LOCATION)),%d)'%(ammo,pc,pc))
 # Shared camera center covers both party and the enemy approach.
 (out/'director-origin.json').write_text(json.dumps({'x':ox,'y':oy,'start':time.monotonic()+3}))
else:
 while not (out/'director-origin.json').exists(): pump(.1)

origin=json.loads((out/'director-origin.json').read_text());ox,oy=origin['x'],origin['y'];start=origin['start']
G('location_origin_set(location_make(%d,%d))'%(ox+(9 if os.getenv('ARCMP_SHOWCASE_CLEAN') else 5),oy+(0 if os.getenv('ARCMP_SHOWCASE_CLEAN') else 2)))
G('iso_interface_refresh()');G('intgame_show()')
G('coop_view_zoom_set(150,640,300)')
G('combat_auto_attack_set(1)')
if index==0 and not os.getenv('ARCMP_SHOWCASE_CLEAN'):
 gdb.execute('set $r=(TigRect*)calloc(1,sizeof(TigRect))')
 for field,value in [('x',840),('y',310),('width',420),('height',180)]:gdb.execute('set $r->%s=%d'%(field,value))
 G('coop_hud_store(HUD_LOG_COMPACT,$r)');G('coop_hud_opacity_set(HUD_LOG_COMPACT,75)')
 G('coop_play_ui_open(0)')

def spawn(wave):
 for j in range(6):
  proto=P('BP_WOLF') if j%2==0 else '28447'
  loc='location_make(%d,%d)'%(ox+11+(j%3)*2,oy-3+(j//3)*3)
  if bool(E('object_create(sub_4685A0(%s),%s,$ptr)'%(proto,loc))):
   G('obj_field_int32_set(*$ptr,OBJ_F_NPC_PAD_I_1,%d)'%(799000+j))
   G('obj_field_int32_set(*$ptr,OBJ_F_HP_PTS,%d)'%(60 if wave==0 else 90))
   G('obj_field_int32_set(*$ptr,OBJ_F_HP_DAMAGE,0)')
   G('ai_attack(*$ptr,coop_campaign_object(%d),0,0)'%(j%4))
 mark('native-enemy-wave-%d'%wave)

def targets():
 return [h for i in range(6) if (h:=V("'magic_probe.c'::target(%d)"%i)) and not bool(E('critter_is_dead(%d)'%h))]
def cast(spell,target,location=False):
 G(('target_descriptor_set_loc' if location else 'target_descriptor_set_obj')+'($td,%d)'%target)
 ok=bool(E('coop_runtime_cast($hero,%s,$td,0)'%spell))
 mark(('cast-' if ok else 'cast-deferred-')+spell)
 return ok
def portrait_attack(target):
 G('combat_campaign_state_get($combat_state)')
 count=V('$combat_state->order_count')
 # Click the actual turn card when it is on this visible page.
 try:
  r=E("'party_ui.c'::turn_rect");width=int(r['width']);capacity=max(1,(width-120)//94)
  idx=next(i for i in range(min(count,capacity)) if V('$combat_state->order[%d]'%i)==target)
  x=int(r['x'])+6+idx*94+20;y=int(r['y'])+44
  G('tig_mouse_set_position(%d,%d)'%(x,y))
  gdb.execute('set $msg->data.mouse.x=%d'%x);gdb.execute('set $msg->data.mouse.y=%d'%y)
  gdb.execute('set $msg->data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP')
  G("'party_ui.c'::turn_events($msg)")
 except (StopIteration,gdb.error):
  G('coop_party_ui_target_actor(%d,1)'%target)
 mark('portrait-target-attack')

sent=set();last_action=-10;last_generation=0;turn_at=0
while time.monotonic()-start<(48 if os.getenv('ARCMP_SHOWCASE_CLEAN') else 64):
 gdb.execute('set $hero=player_get_local_pc_obj()')
 elapsed=time.monotonic()-start
 if elapsed<0: pump(.1);continue
 if 1 not in sent and elapsed>=1:
  G('coop_runtime_move(location_make(%d,%d),1)'%(ox+5+(index%2)*2,oy+(index//2)*2));sent.add(1);mark('party-run')
 if 2 not in sent and elapsed>=5:
  G('coop_play_ping(location_make(%d,%d))'%(ox+10,oy-1));sent.add(2);mark('world-ping')
 if 6 not in sent and elapsed>=8:
  G('coop_runtime_combat(COOP_COMBAT_STANCE,1,0)');sent.add(6);mark('draw-weapon')
 if index==0 and 3 not in sent and elapsed>=9: spawn(0);sent.add(3)
 if index==0 and 4 not in sent and elapsed>=32:
  spawn(1);G('coop_runtime_combat(COOP_COMBAT_POLICY,1,0)');sent.add(4);mark('turn-based-policy')
 if index==0 and 5 not in sent and elapsed>=36:
  G('coop_runtime_combat(COOP_COMBAT_STANCE,1,0)');sent.add(5);mark('turn-based-start')
 if elapsed>=10 and elapsed-last_action>=2.6:
  enemies=targets()
  if enemies and not bool(E('coop_battle_busy(coop_gameplay_battle(coop_runtime_gameplay()))')):
   target=enemies[index%len(enemies)]
   if bool(E('combat_turn_based_is_active()')):
    if V('combat_turn_based_whos_turn_get()')==V('$hero'):
     generation=V('combat_turn_generation_get()')
     if generation!=last_generation:
      portrait_attack(target);last_generation=generation;turn_at=elapsed
     elif elapsed-turn_at>=2:
      G('coop_runtime_combat(COOP_COMBAT_END_TURN,0,0)');mark('end-turn')
   elif index==3:
    if 12<elapsed<17 or 25<elapsed<29:
     cast('SPELL_FIREFLASH',V('obj_field_int64_get(%d,OBJ_F_LOCATION)'%target),True)
    else: cast('SPELL_HARM',target)
   elif index==0 and 19<elapsed<23:
    cast('SPELL_STRENGTH_OF_EARTH',V('$hero'))
   elif index==2 and 22<elapsed<27:
    cast('SPELL_MINOR_HEALING',V('coop_campaign_object(0)'))
   else:
    G('coop_party_ui_target_actor(%d,1)'%target);mark('owner-attack')
   last_action=elapsed
 pump(.12)
mark('complete')
print('DIRECTOR_COMPLETE',index,flush=True)
