"""GDB-only stage for benchmark_qol_network.py; isolated native engines."""
import gdb
import json
import os
from pathlib import Path
import time

out=Path(os.environ['ARCMP_QOL_OUT'])
index=int(os.environ['ARCMP_QOL_PLAYER'])
E=gdb.parse_and_eval
def G(s):return gdb.execute('call '+s,to_string=True)
def V(s):return int(E(s))
def L(x,y):return x|(y<<32)
def pump(seconds):G('showcase_pump(%d)'%round(seconds*1000))
G('showcase_init((void*)tig_ping,(void*)gamelib_ping,(void*)iso_redraw,(void*)tig_window_display,(void*)tig_message_dequeue,(void*)intgame_process_event,(void*)tig_mouse_set_position)')
G('showcase_network(coop_runtime_session(),(void*)coop_session_poll,(void*)coop_runtime_tick,(void*)SDL_GetTicks)')
G("SDL_SetRenderVSync('video.c'::tig_video_state.renderer,0)")
G('coop_play_pref_set(PLAY_PREF_AUTOPAUSE,0)');G('coop_play_ui_close()')
gdb.execute('set $hero=player_get_local_pc_obj()')
gdb.execute('set $ptr=(int64_t*)calloc(1,8)')
gdb.execute('set $td=(TargetDescriptor*)calloc(1,sizeof(TargetDescriptor))')
if index==0:
 G('showcase_hide_npcs((void*)obj_inst_first,(void*)obj_inst_next,(void*)obj_field_int32_get,(void*)object_flags_set,OBJ_F_TYPE,OBJ_TYPE_NPC)')
 loc=V('obj_field_int64_get($hero,OBJ_F_LOCATION)')
 ox=loc&0xffffffff;oy=loc>>32
 for n in range(64):
  assert bool(E('object_create(sub_4685A0(28319),%d,$ptr)'%L(ox+8+n%8,oy+8+n//8)))
  G('obj_field_int32_set(*$ptr,OBJ_F_NPC_PAD_I_1,%d)'%(799000+n))
  G('obj_field_int32_set(*$ptr,OBJ_F_NPC_FLAGS,0x808)')
  G('obj_field_int32_set(*$ptr,OBJ_F_CRITTER_FLAGS,obj_field_int32_get(*$ptr,OBJ_F_CRITTER_FLAGS)|0x1000)')
 for n in range(4):
  actor=V('coop_campaign_object(%d)'%n)
  G('stat_base_set(%d,STAT_WILLPOWER,20)'%actor)
  G('spell_add(%d,SPELL_STRENGTH_OF_EARTH,1)'%actor)
  G('critter_fatigue_pts_set(%d,250)'%actor)
 (out/'origin.json').write_text(json.dumps({'x':ox,'y':oy,'start':time.monotonic()+3}))
else:
 while not (out/'origin.json').exists():pump(.1)
origin=json.loads((out/'origin.json').read_text())
ox,oy,start=origin['x'],origin['y'],origin['start']
G('iso_interface_refresh()');G('intgame_show()')
initial=V('obj_field_int64_get($hero,OBJ_F_LOCATION)')
sent=set();samples=[];request_at=None;observed_at=None
while time.monotonic()-start<27:
 elapsed=time.monotonic()-start
 if elapsed>=9 and 'move' not in sent:
  request_at=time.monotonic()
  assert bool(E('coop_runtime_move(%d,1)'%L(ox+2+(index%2)*2,oy+2+(index//2)*2)))
  sent.add('move')
 if elapsed>=18 and 'spell' not in sent:
  G('target_descriptor_set_obj($td,$hero)')
  assert bool(E('coop_runtime_cast($hero,SPELL_STRENGTH_OF_EARTH,$td,0)'))
  sent.add('spell')
 if request_at and observed_at is None and V('obj_field_int64_get($hero,OBJ_F_LOCATION)')!=initial:
  observed_at=time.monotonic()
 samples.append({'at':elapsed,'revision':V('coop_gameplay_world_revision(coop_runtime_gameplay())')})
 pump(.08)
 assert V('coop_runtime_phase()')==V('COOP_RUNTIME_PLAYING'),gdb.execute('p coop_runtime_error()',to_string=True)
G('gamelib_redraw()');G('iso_redraw()');G('tig_window_display()')
assert V('tig_video_screenshot_save("qol-network.bmp")')==0
(out/f'peer-{index}.json').write_text(json.dumps({'player':index,'started':start,'move_observed_ms':(observed_at-request_at)*1000 if observed_at else None,
 'world_revisions':samples,'ended_playing':True,'commands':sorted(sent)},indent=2)+'\n')
print('QOL_NETWORK_PASS',index,flush=True)
