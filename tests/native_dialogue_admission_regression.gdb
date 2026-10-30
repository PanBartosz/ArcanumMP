# Isolated writable runtime with a native arcmp-bootstrap campaign save.
# Exercise unavailable native speakers during transition and busy-dialogue
# deferral. Only this diagnostic creates an NPC and transport state.
set pagination off
set confirm off
set debuginfod enabled off
break verify_dialogue_semantics
run
call coop_authority_set(COOP_AUTHORITY_HOST)
print gamelib_load("arcmp-bootstrap")
set $actor=coop_campaign_object(0)
set $buf=(long long*)calloc(2,8)
set $iter=(int*)($buf+1)
set $session=(CoopSession*)calloc(1,sizeof(CoopSession))
set $session->host=1
set $r=(CoopRuntime*)calloc(1,sizeof(CoopRuntime))
set $r->session=$session
set $r->phase=COOP_RUNTIME_RESTING
set 'runtime.c'::runtime=$r
python
import gdb,json,os,re
from pathlib import Path
root=Path(gdb.current_progspace().filename).resolve().parents[4]
flags_header=(root/"upstream/arcanum-ce/src/game/obj_flags.h").read_text()
flags={name:int(value,16) for name,value in re.findall(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)",flags_header)}
E=gdb.parse_and_eval
assert bool(E('coop_campaign_active()'))
gdb.execute('set $more=obj_inst_first($buf,$iter)')
while bool(E('$more')):
 if int(E('obj_field_int32_get(*$buf,OBJ_F_TYPE)'))==int(E('OBJ_TYPE_NPC')):break
 gdb.execute('set $more=obj_inst_next($buf,$iter)')
assert bool(E('$more'))
gdb.execute('set $prototype=obj_field_handle_get(*$buf,OBJ_F_PROTOTYPE_HANDLE)')
assert bool(E('object_create($prototype,obj_field_int64_get($actor,OBJ_F_LOCATION),$buf)'))
gdb.execute('set $speaker=*$buf')
base=int(E('obj_field_int32_get($speaker,OBJ_F_FLAGS)')) & ~(flags['OF_OFF']|flags['OF_DESTROYED']|flags['OF_EXTINCT'])
rows=[]
for bound in [False,True]:
 if bound:
  gdb.execute('set $dialogue=(CoopDialogue*)calloc(1,sizeof(CoopDialogue))')
  assert bool(E('coop_engine_dialogue_bind($dialogue,$session)'))
  gdb.execute("set 'engine_dialogue.c'::conversation_open=1")
 for label,obj_flags,critter in [('off',base|flags['OF_OFF'],0),('muted',base,flags['OCF_MUTE']),('sleeping',base,flags['OCF_SLEEPING']),('available',base,0)]:
  gdb.execute('call coop_engine_dialogue_reset()')
  gdb.execute("set 'engine_dialogue.c'::start_count=0")
  gdb.execute('call obj_field_int32_set($speaker,OBJ_F_FLAGS,%d)'%obj_flags)
  gdb.execute('call obj_field_int32_set($speaker,OBJ_F_CRITTER_FLAGS,%d)'%critter)
  native=int(E('ai_can_speak($speaker,$actor,1)'))
  assert bool(E('coop_engine_dialogue_request($actor,$speaker,1324,0,1)'))
  pending=bool(E('coop_engine_dialogue_pending()'))
  rows.append(dict(bound=bound,speaker=label,native_admission=native,queued=pending))
  if os.getenv('ARCMP_EXPECT_ADMISSION_FIX'):
   assert pending==(label=='available'),rows[-1]
  if not bound and label=='available':
   assert bool(E('coop_engine_dialogue_save()'))
   gdb.execute('call coop_engine_dialogue_reset()')
   assert bool(E('coop_engine_dialogue_load()')) and bool(E('coop_engine_dialogue_pending()'))
print('DIALOGUE_ADMISSION',json.dumps(rows))
end
quit
