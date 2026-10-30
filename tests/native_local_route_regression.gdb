# Isolated writable runtime with an arcmp-bootstrap four-owner save.
set pagination off
set confirm off
set debuginfod enabled off
break verify_dialogue_semantics
run
call coop_authority_set(COOP_AUTHORITY_HOST)
print gamelib_load("arcmp-bootstrap")
set $g=(CoopGameplay*)calloc(1,sizeof(CoopGameplay))
set $g->dialogue=(CoopDialogue*)calloc(1,sizeof(CoopDialogue))
set $g->host=1
set $g->running=1
set $g->epoch=1
set $g->map=map_current_map()
set $g->peers[0].member=0
set $g->peers[0].hello=1
set $actor=coop_campaign_object(0)
set $id=(AnimID*)calloc(1,sizeof(AnimID))
set $run=(AnimRunInfo**)calloc(1,sizeof(AnimRunInfo*))
set $packet=(unsigned char*)calloc(1,128)
python
import gdb,struct,json
E=gdb.parse_and_eval
inf=gdb.selected_inferior()
actor=int(E('$actor')); assert actor
member=E('coop_campaign_member(0)').dereference()
guid=bytes(inf.read_memory(int(member['object_id']['d']['g']['data'].address),16))
loc=int(E('obj_field_int64_get($actor,OBJ_F_LOCATION)'))
x,y=loc&0xffffffff,loc>>32
points=[(x+1,y),(x+1,y+1),(x+1,y+2),(x,y+2),(x,y+3),(x,y+4)]
seq=0; results=[]
def packet(points,run=False,owner=guid):
 global seq
 seq+=1
 p=struct.pack('>III',1,seq,int(E('map_current_map()')))+owner+struct.pack('>IIBI',*points[0],run,int(E('combat_turn_generation_get()')))
 if len(points)>1: p+=bytes([len(points)])+b''.join(struct.pack('>II',*v) for v in points[1:])
 return p
def send(p,expected=True):
 inf.write_memory(int(E('$packet')),p)
 assert bool(E('execute_move($g,0,$packet,%d)'%len(p)))==expected
 return bool(E('$g->peers[0].accepted'))
# AGDATA_TARGET_TILE is resolved from the engine enum below, not assumed.
def stack():
 moving=bool(E('anim_is_current_goal_type($actor,AG_MOVE_TO_TILE,$id)')) or bool(E('anim_is_current_goal_type($actor,AG_RUN_TO_TILE,$id)'))
 if not moving:return []
 assert bool(E('anim_id_to_run_info($id,$run)'))
 r=E('*$run');n=int(r['current_goal'])+1;target=int(E('AGDATA_TARGET_TILE'))
 return [int(r['goals'][i]['params'][target]['loc']) for i in range(n-1,-1,-1)]
def locations(ps):return [a|(b<<32) for a,b in ps]
assert bool(E('coop_engine_options_apply($actor,coop_engine_options_record($actor),4)'))
for run in [False,True]:
 assert send(packet(points,run)) and stack()==locations(points)
 assert bool(E('anim_is_current_goal_type($actor,%s,$id)'%('AG_RUN_TO_TILE' if run else 'AG_MOVE_TO_TILE')))
 results.append('explicit-movement-mode-overrides-host-preference')
 results.append('six-waypoint-'+('run' if run else 'walk')+'-native-order')
 before=stack()
 assert not send(packet([points[0],(0xffffffff,0xffffffff)],run)) and stack()==before
 results.append('invalid-tail-preserves-existing-route')
 assert not send(packet(points,run,bytes(16))) and stack()==before
 results.append('forged-route-preserves-existing-route')
 assert not send(packet([points[0],points[0]],run)) and stack()==before
 results.append('duplicate-waypoints-refused')
 good=packet(points,run)
 for bad in [good[:41]+bytes([0])+good[42:],good[:41]+bytes([7])+good[42:],good[:-1],good+b'\0',good[:42]]:
  send(bad,False);assert stack()==before
 results.append('malformed-route-shapes-refused')
 replacement=[(x,y+1)]
 assert send(packet(replacement,run)) and stack()==locations(replacement)
 results.append('new-click-replaces-entire-route')
 assert send(packet(points,run))
 seq+=1
 halt=struct.pack('>III',1,seq,int(E('map_current_map()')))+guid+struct.pack('>III',x,y,int(E('combat_turn_generation_get()')))
 inf.write_memory(int(E('$packet')),halt)
 assert bool(E('execute_halt($g,0,$packet,%d)'%len(halt))) and bool(E('$g->peers[0].accepted')) and not stack()
 results.append('halt-cancels-entire-route')
print('LOCAL_ROUTE_NATIVE '+json.dumps(dict(verified=True,checks=results)))
end
quit
