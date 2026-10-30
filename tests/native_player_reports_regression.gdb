# Run in a private writable retail runtime. Fresh native campaign objects are
# retained; only the diagnostic party positions and transport binding are set.
set pagination off
set confirm off
set debuginfod enabled off
break verify_dialogue_semantics
run
print assemble_party()
call mainmenu_ui_begin_campaign((int)coop_campaign_member(0)->prototype)
call teleport_ping(0)
call coop_authority_set(COOP_AUTHORITY_HOST)
set $actor=coop_campaign_object(0)
set $other=coop_campaign_object(1)
set $buf=(long long*)calloc(2,8)
set $iter=(int*)($buf+1)
set $script=(Script*)calloc(1,sizeof(Script))
set $session=(CoopSession*)calloc(1,sizeof(CoopSession))
set $session->host=1
set $session->phase=COOP_LOBBY
set $session->local_slot=0
set $session->players[0].connected=1
python
import gdb,json,re
from pathlib import Path
E=gdb.parse_and_eval;inf=gdb.selected_inferior()
root=Path(gdb.current_progspace().filename).resolve().parents[4]
flags={n:int(v,16) for n,v in re.findall(r'#define\s+(\w+)\s+(0x[0-9a-fA-F]+)',(root/'upstream/arcanum-ce/src/game/obj_flags.h').read_text())}
actor=int(E('$actor'));member=E('coop_campaign_member(0)').dereference()
owner=bytes(inf.read_memory(int(member['owner'].address),16))
inf.write_memory(int(E('$session->players[0].identity').address),owner)
inf.write_memory(int(E('$session->players[0].name').address),b'Regression player\0')
gdb.execute('set $virgil=0');gdb.execute('set $agent=0')
gdb.execute('set $more=obj_inst_first($buf,$iter)')
while bool(E('$more')):
 gdb.execute('set $obj=*$buf')
 if int(E('obj_field_int32_get($obj,OBJ_F_TYPE)'))==int(E('OBJ_TYPE_NPC')):
  name=int(E('obj_field_int32_get($obj,OBJ_F_NAME)'))
  if name==6409:gdb.execute('set $virgil=$obj')
  if name==6627:gdb.execute('set $agent=$obj')
 gdb.execute('set $more=obj_inst_next($buf,$iter)')
assert int(E('$virgil')) and int(E('$agent'))
gdb.execute('call object_flags_unset($agent,%d)'%flags['OF_OFF'])
for expression in ['$actor','$other','$virgil']:
 gdb.execute('call sub_43E770(%s,obj_field_int64_get($agent,OBJ_F_LOCATION),0,0)'%expression)
assert bool(E('critter_follow($virgil,$actor,1)'))
# Cross-speaker continuation: the normal Virgil script is replaced before its
# queued encounter-specific page is consumed, as in the actual elf exchange.
gdb.execute('set $dialogue=coop_dialogue_create($session,1,0,coop_engine_dialogue_present,coop_engine_dialogue_choose,0)')
assert int(E('$dialogue')) and bool(E('coop_engine_dialogue_bind($dialogue,$session)')), 'bind'
print('OPENING_BEGIN')
gdb.execute('call object_script_execute($actor,$agent,0,SAP_DIALOG,0)')
seen=[]
for step in range(40):
 for _ in range(3):
  assert bool(E('coop_dialogue_poll($dialogue,0)')), 'publication'
  assert bool(E('coop_engine_dialogue_poll(1)')), 'dispatch'
 entry=E("'dialog_ui.c'::stru_66DAB8[0]")
 if not bool(entry['field_1850']):
  if not bool(E('coop_engine_dialogue_pending()')):break
  continue
 state=entry['state']; script=int(entry['script_num']);page=int(state['field_17EC'])
 seen.append([script,page]);print('OPENING_PAGE',script,page)
 assert not (len(seen)>1 and script==1324),seen
 assert int(state['num_options'])>0
 assert bool(E('dialog_ui_coop_choose(0)'))
assert any(s==1715 for s,p in seen) and sum(s==1714 for s,p in seen)>=2,seen
assert not bool(E('coop_engine_dialogue_pending()'))
gdb.execute('call obj_arrayfield_script_get($virgil,OBJ_F_SCRIPTS_IDX,SAP_DIALOG,$script)')
assert int(E('$script->num'))==1324
print('OPENING_HANDOFF_PASS',json.dumps(seen))
# Shared hostility is independent of the prospective attacker's stance.
gdb.execute('call obj_field_int32_set($agent,OBJ_F_NPC_FLAGS,obj_field_int32_get($agent,OBJ_F_NPC_FLAGS)|%d)'%flags['ONF_FIGHTING'])
gdb.execute('call obj_field_handle_set($agent,OBJ_F_NPC_COMBAT_FOCUS,$other)')
assert bool(E('combat_is_party_enemy($agent)'))
gdb.execute('set $game=(CoopGameplay*)calloc(1,sizeof(CoopGameplay))')
gdb.execute('set $game->host=1')
gdb.execute('call sub_4B6C90(0)')
gdb.execute('call combat_critter_activate_combat_mode($actor)')
gdb.execute('call refresh_combat_presentation($game)')
assert int(E('$game->combat_engaged'))&1
gdb.execute('call obj_field_int32_set($agent,OBJ_F_CRITTER_FATIGUE_DAMAGE,critter_fatigue_max($agent)+1)')
gdb.execute('call sub_424070($actor,3,0,0)')
gdb.execute('call refresh_combat_presentation($game)')
assert not bool(E('combat_critter_is_combat_mode_active($actor)'))
print('POST_KNOCKOUT_SHEATH_PASS')
gdb.execute('call obj_field_int32_set($agent,OBJ_F_CRITTER_FATIGUE_DAMAGE,0)')
gdb.execute('call combat_critter_activate_combat_mode($actor)')
gdb.execute('call refresh_combat_presentation($game)')
gdb.execute('call obj_field_int32_set($agent,OBJ_F_HP_DAMAGE,object_hp_max($agent)+10000)')
gdb.execute('call sub_424070($actor,3,0,0)')
gdb.execute('call refresh_combat_presentation($game)')
assert not bool(E('combat_critter_is_combat_mode_active($actor)'))
gdb.execute('set $cursor=(tig_art_id_t*)calloc(1,sizeof(tig_art_id_t))')
gdb.execute('call tig_art_interface_id_create(0,0,0,0,$cursor)')
assert int(E('tig_mouse_cursor_get_art_id()'))==int(E('*$cursor'))
print('POST_COMBAT_CURSOR_PASS')
assert not bool(E('combat_is_party_enemy($agent)'))
print('SHARED_HOSTILITY_PASS')
# Running starts consistently; saved explicit walking survives later installs.
assert int(E('coop_engine_options_defaults()'))&4
old=int(E('coop_engine_options_record($actor)'))
assert bool(E('coop_engine_options_apply($actor,%d,0)'%old))
gdb.execute('set $env=(CoopEnvironmentState*)calloc(1,sizeof(CoopEnvironmentState))')
gdb.execute('call coop_engine_options_capture($env)');gdb.execute('call coop_engine_options_install($env)')
assert int(E('coop_engine_options_get($actor,COOP_OPTION_ALWAYS_RUN)'))==0
gdb.execute('set $env->owner_options[0]=0x80');gdb.execute('call coop_engine_options_install($env)')
assert int(E('coop_engine_options_get($actor,COOP_OPTION_ALWAYS_RUN)'))==1
print('RUN_DEFAULT_AND_EXPLICIT_WALK_PASS')
# Replica presentation advances text timers while campaign time/AI stay still.
gdb.execute('call coop_engine_dialogue_unbind()')
gdb.execute('call coop_authority_set(COOP_AUTHORITY_REPLICA)')
gdb.execute('call tb_add($actor,TB_TYPE_GREEN,"temporary client chat")')
gdb.execute('call tb_expire_in($actor,1)')
assert int(E('obj_field_int32_get($actor,OBJ_F_FLAGS)'))&flags['OF_TEXT']
days=int(E('sub_45A7C0().days'));ms=int(E('sub_45A7C0().milliseconds'))
gdb.execute('call SDL_Delay(1100)');gdb.execute('call gamelib_ping()')
assert not (int(E('obj_field_int32_get($actor,OBJ_F_FLAGS)'))&flags['OF_TEXT'])
assert (int(E('sub_45A7C0().days')),int(E('sub_45A7C0().milliseconds')))==(days,ms)
print('CLIENT_BUBBLE_EXPIRY_WITHOUT_SIMULATION_PASS')
# A presentation tick eases rendering, retaining exact authoritative position.
gdb.execute('call location_origin_set(obj_field_int64_get($virgil,OBJ_F_LOCATION))')
gdb.execute('set $loc=obj_field_int64_get($virgil,OBJ_F_LOCATION)')
gdb.execute('set $aid=obj_field_int32_get($virgil,OBJ_F_CURRENT_AID)')
gdb.execute('call object_replica_pose($virgil,$loc,0,0,$aid)')
gdb.execute('call SDL_Delay(50)')
gdb.execute('call object_replica_pose($virgil,$loc,30,0,$aid)')
gdb.execute('set $visual=replica_visual($virgil,0)')
assert 35<=int(E('$visual->duration'))<=100
# Slow only the diagnostic clock to keep GDB call latency from consuming an
# entire production interpolation interval before the intermediate observation.
gdb.execute('set $visual->duration=1000')
gdb.execute('set $visual->sampled=SDL_GetTicks()')
gdb.execute('set $rect=(TigRect*)calloc(1,sizeof(TigRect))')
gdb.execute('call object_get_rect($virgil,0,$rect)');before=int(E('$rect->x'))
gdb.execute('call SDL_Delay(25)');gdb.execute('call object_ping(0)')
gdb.execute('call object_get_rect($virgil,0,$rect)');middle=int(E('$rect->x'))
gdb.execute('set $visual->sampled=$visual->sampled-1000');gdb.execute('call object_ping(0)')
gdb.execute('call object_get_rect($virgil,0,$rect)');after=int(E('$rect->x'))
assert before<middle<after,(before,middle,after)
assert int(E('obj_field_int32_get($virgil,OBJ_F_OFFSET_X)'))==30
print('NPC_RENDER_INTERPOLATION_PASS',before,middle,after)
# A later bulk snapshot must preserve the newer NPC pose and its spatial index.
gdb.execute('set $newloc=location_make(location_get_x($loc)+128,location_get_y($loc))')
gdb.execute('call object_replica_pose($virgil,$newloc,0,0,$aid)')
gdb.execute('call object_replica_detach($virgil)')
gdb.execute('call obj_field_int64_set($virgil,OBJ_F_LOCATION,$loc)')
gdb.execute('call obj_find_move($virgil)')
gdb.execute('set $visual->sampled=SDL_GetTicks()')
gdb.execute('call object_replica_attach($virgil)')
assert int(E('obj_field_int64_get($virgil,OBJ_F_LOCATION)'))==int(E('$newloc'))
assert int(E('((FindNode*)obj_field_ptr_get($virgil,OBJ_F_FIND_NODE))->sec'))==int(E('sector_id_from_loc($newloc)'))
print('NPC_BULK_REWIND_AND_SECTOR_INDEX_PASS')
print('PLAYER_REPORTS_NATIVE_PASS')
end
quit
