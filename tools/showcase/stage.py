"""GDB-side staged *solo* feature footage; uses actual native controls/rendering.

Items/known spells/recipes are prepared for legible demonstrations. Location
cuts are teleports for filming, not claims about campaign completion. Separate
four-process takes establish working multiplayer. No simulated multiplayer HUD.
"""
import gdb
import json
import os
from pathlib import Path
import time
import re

ROOT=Path(os.environ['ARCMP_SHOWCASE_ROOT'])
out=Path(os.environ['ARCMP_SHOWCASE_OUT'])
def expression(s):
 s=re.sub(r'location_make\((-?\d+),\s*(-?\d+)\)',lambda m:str((int(m[1])&0xffffffff)|((int(m[2])&0xffffffff)<<32)),s)
 return re.sub(r'location_get_([xy])\((\d+)\)',lambda m:str(int(m[2])&0xffffffff if m[1]=='x' else int(m[2])>>32),s)
E=lambda s:gdb.parse_and_eval(expression(s))
G=lambda s:gdb.execute('call '+expression(s),to_string=True)
D=lambda s:gdb.execute(expression(s))
V=lambda s:int(E(s))
def string(s): return json.dumps(s)
G('(void*)dlopen('+string(os.environ.get('ARCMP_SHOWCASE_PUMP',str(ROOT/'evidence/showcase/pump.so')))+',2)')
G('showcase_init((void*)tig_ping,(void*)gamelib_ping,(void*)iso_redraw,(void*)tig_window_display,(void*)tig_message_dequeue,(void*)intgame_process_event,(void*)tig_mouse_set_position)')
s=E("'video.c'::tig_video_state.surface")
info={k:int(s[k]) for k in ('w','h','pitch','format')}
info.update(pixels=int(s['pixels']),pid=gdb.selected_inferior().pid)
Path(os.environ['ARCMP_SHOWCASE_INFO']).write_text(json.dumps(info))
shots=[]
def mark(name):
 entry={'name':name,'monotonic':time.monotonic()}
 if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):entry.update(frame=V("'pump.c'::record_frames"),fps=30)
 shots.append(entry)
 (out/'shots.json').write_text(json.dumps(shots,indent=2))
 print('SHOWCASE_SHOT',name,flush=True)
def pump(seconds): G('showcase_pump(%d)'%round(seconds*1000))
def shot(name,seconds): mark(name);pump(seconds)
def refresh(): G('gamelib_redraw()');G('iso_redraw()');G('tig_window_display()')
def mouse(event,x,y,handler="'play_ui.c'::input"):
 gdb.execute('set $msg->data.mouse.event='+event)
 gdb.execute('set $msg->data.mouse.x=%d'%x);gdb.execute('set $msg->data.mouse.y=%d'%y)
 G('tig_mouse_set_position(%d,%d)'%(x,y))
 return G(handler+'($msg)')
def drag(handler,x,y,x1,y1,seconds):
 G('tig_mouse_show()')
 G('showcase_drag($msg,&$msg->data.mouse.event,&$msg->data.mouse.x,&$msg->data.mouse.y,(void*)%s,%d,%d,%d,%d,TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN,TIG_MESSAGE_MOUSE_MOVE,TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP,%d,(void*)tig_mouse_set_button)'%(handler,x,y,x1,y1,seconds*1000))
def tab(t):
 G('coop_play_ui_close()');G('coop_play_ui_open(%d)'%t);pump(.5)
def rect(expr):
 r=E(expr);return {k:int(r[k]) for k in ('x','y','width','height')}
def select(index):
 gdb.execute("set 'play_ui.c'::selected=%d"%index);G("'play_ui.c'::refresh()");refresh()
def query(text):
 G('(char*)strcpy('+"'play_ui.c'::query,"+string(text)+')');G("'play_ui.c'::refresh()");refresh()

G('mainmenu_ui_begin_campaign(16067)');G('teleport_ping(0)')
G('coop_authority_set(COOP_AUTHORITY_LOCAL)')
gdb.execute('set $hero=player_get_local_pc_obj()')
G('obj_field_string_set($hero,OBJ_F_PC_PLAYER_NAME,"Silas")')
G('dialog_ui_reset()');G('iso_interface_refresh()');G('intgame_show()')
G('coop_hud_reset_layout()');G('coop_hud_log_state(0,1)')
G('coop_play_pref_set(PLAY_PREF_TEXT,100)');G('coop_play_pref_set(PLAY_PREF_AUTOPAUSE,0)')
G('coop_play_pref_set(PLAY_PREF_SYMBOLS,0)');G('coop_play_pref_set(PLAY_PREF_PREVIEW,0)')
G('coop_play_pref_set(PLAY_PREF_BAR,1)')
gdb.execute('set $msg=(TigMessage*)calloc(1,sizeof(TigMessage))');gdb.execute('set $msg->type=TIG_MESSAGE_MOUSE')
gdb.execute('set $rows=(PlayRow*)calloc(PLAY_ROWS,sizeof(PlayRow))')
gdb.execute('set $gear=(int64_t*)calloc(1,8)')
def P(name):return re.search(r'^#define '+name+r'\s+(\d+)',(ROOT/'upstream/arcanum-ce/src/game/descriptions.h').read_text(),re.M)[1]
for proto,slot in [('BP_CHAINMAIL',1006),('BP_BOOTS',1008)]:
 assert bool(E('object_create(sub_4685A0(%s),obj_field_int64_get($hero,OBJ_F_LOCATION),$gear)'%P(proto)))
 G('item_transfer_ex(*$gear,$hero,%d)'%slot)
if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):
 G('showcase_record_open(%s,%s,(void*)%d,%d,%d,%d)'%(string(str(out/'peer-0.bgr')),string(str(out/'peer-0.frames.json')),int(s['pixels']),int(s['w']),int(s['h']),int(s['pitch'])))

# Read native archive catalogs for the soundtrack and scenic locations.
gdb.execute('set $fl=(TigFileList*)calloc(1,sizeof(TigFileList))')
catalog=[]
for pattern in ['sound\\music\\*','sound\\*.mp3','music\\*']:
 G('tig_file_list_create($fl,'+string(pattern)+')')
 for i in range(V('$fl->count')):
  catalog.append(E('$fl->entries[%d].path'%i).string())
 G('tig_file_list_destroy($fl)')
(out/'music-catalog.json').write_text(json.dumps(catalog,indent=2))
for src,dst in [('Rules\\MapList.mes','map-list.mes'),('Rules\\WorldMap.mes','world-map.mes')]:
 G('tig_file_copy('+string(src)+','+string(str(out/dst))+')')
areas=[{'id':i,'name':E('area_get_name(%d)'%i).string(),'location':V('area_get_location(%d)'%i)} for i in range(1,V('area_count()'))]
(out/'areas.json').write_text(json.dumps(areas,indent=2))

# A little native world movement before the detail sequence.
G('dialog_ui_reset()');G('intgame_mode_set(INTGAME_MODE_MAIN)')
gdb.execute('set $tp=(TeleportData*)calloc(1,sizeof(TeleportData))')
gdb.execute('set $tp->obj=$hero');gdb.execute('set $tp->map=1');gdb.execute('set $tp->loc=area_get_location(21)')
G('teleport_do($tp)');G('teleport_ping(0)');G('dialog_ui_reset()')
G('intgame_mode_set(INTGAME_MODE_MAIN)');G('intgame_show()');refresh()
G('coop_view_zoom_set(125,640,270)');refresh()
shot('opening-shrouded-hills',5)
G('coop_view_zoom_set(100,640,270)')

# Prepare known actions and ordinary native items for browsing/assignment.
for spell in ['SPELL_HARM','SPELL_MINOR_HEALING','SPELL_STRENGTH_OF_EARTH','SPELL_FIREFLASH','SPELL_ORCISH_CHAMPION']:
 G('spell_add($hero,%s,1)'%spell)
G('tech_degree_inc($hero,TECH_HERBOLOGY)')
G('quest_state_set($hero,1000,QUEST_STATE_ACCEPTED,0)')
G('coop_play_pin(1000,1)');G('coop_play_note_set("Find the truth behind the crash. Regroup in Shrouded Hills.")')
G('coop_play_actions($rows,"Harm")');G('coop_play_action_assign(0,$rows)')
G('coop_play_actions($rows,"Minor Healing")');G('coop_play_action_assign(1,$rows)')
G('coop_play_actions($rows,"Fireflash")');G('coop_play_action_assign(2,$rows)')
G('coop_play_actions($rows,"Repair")');G('coop_play_action_assign(3,$rows)')

tab(2);shot('actions-known',3)
mark('actions-search')
for part in ['F','Fi','Fir','Fire']:
 query(part);pump(.35)
shot('actions-fireflash',3)
query('');select(0);shot('actions-assign',2)
G('coop_play_ui_close()');shot('quickslots-native-icons',3)

tab(1);shot('inventory-browse',3)
mark('inventory-search')
for part in ['D','Da','Dag','Dagger']:
 query(part);pump(.3)
shot('inventory-comparison',3)
G("'play_ui.c'::activate(2)");G("'play_ui.c'::refresh()");shot('inventory-mark-junk',2)

tab(3);shot('quests-note-and-pin',4)
tab(4);shot('crafting-missing-ingredients',4)

tab(5);shot('settings-text-125',3)
G('coop_play_pref_set(PLAY_PREF_TEXT,150)');G("'play_ui.c'::refresh()");shot('settings-text-150',3)
G('coop_play_pref_set(PLAY_PREF_TEXT,100)');G("'play_ui.c'::refresh()")
select(8);shot('settings-autopause',3)
select(13);shot('settings-transparency-and-layout',3)
select(29);shot('settings-remapping',3)
G('coop_play_ui_close()')

# Real damage/healing events populate the same log used in network games.
gdb.execute('set $combat=(CombatContext*)calloc(1,sizeof(CombatContext))')
assert bool(E('object_create(sub_4685A0(%s),obj_field_int64_get($hero,OBJ_F_LOCATION),$gear)'%P('BP_ORC_GRUNT')))
G('object_flags_set(*$gear,2)')
G('sub_4B2210($hero,*$gear,$combat)');gdb.execute('set $combat->flags=6')
gdb.execute('set $combat->dam[0]=3');gdb.execute('set $combat->dam[4]=2')
G('combat_dmg($combat)');G('combat_heal($combat)')
tab(0);shot('combat-log-compact',3)
r=rect("'play_ui.c'::bounds")
mark('combat-log-drag')
drag("'play_ui.c'::input",r['x']+55,r['y']+12,190,166,2)
shot('combat-log-moved',2)
r=rect("'play_ui.c'::bounds")
mark('combat-log-resize')
drag("'play_ui.c'::input",r['x']+r['width']-5,r['y']+r['height']-5,r['x']+560,r['y']+190,2)
shot('combat-log-resized',2)
mark('combat-log-transparency')
for alpha in [75,50,25,75]:
 G('coop_hud_opacity_set(HUD_LOG_COMPACT,%d)'%alpha);refresh();pump(.9)
G("'play_ui.c'::activate(10)");shot('combat-log-expanded',4)
G("'play_ui.c'::activate(1)");G("'play_ui.c'::refresh()");shot('combat-log-filter',2)
G('coop_play_ui_close()');shot('log-hide',1)
G('coop_play_ui_open(0)');shot('log-show',2)
G('coop_play_ui_close()')

G('coop_hud_edit_toggle()');shot('hud-edit-handles',3)
# Additional action rows use the same edit controls, not a video overlay.
r=rect("'play_ui.c'::bar_rect")
mark('hud-drag-action-rows')
drag("'play_ui.c'::bar_input",r['x']+45,r['y']+8,r['x']+85,r['y']-45,2)
shot('hud-custom-placement',2)
G('coop_hud_edit_toggle()')

# Original map scenes. The camera pans while world animations keep running.
gdb.execute('set $tp=(TeleportData*)calloc(1,sizeof(TeleportData))')
for area_id,label in [(1,'shrouded-hills'),(21,'tarant'),(6,'qintarra'),(4,'caladon'),(19,'isle-of-despair')]:
 G('coop_play_ui_close()');G('dialog_ui_reset()')
 gdb.execute('set $tp->obj=$hero');gdb.execute('set $tp->map=1')
 gdb.execute('set $tp->loc=area_get_location(%d)'%area_id)
 G('teleport_do($tp)');G('teleport_ping(0)')
 G('dialog_ui_reset()');G('intgame_mode_set(INTGAME_MODE_MAIN)');G('intgame_show()')
 G('scroll_set_center(obj_field_int64_get($hero,OBJ_F_LOCATION))')
 G('coop_view_zoom_set(125,640,270)');refresh();pump(.4)
 shot('scenic-'+label,4)
 mark('zoom-'+label)
 G('coop_view_zoom_set(150,640,270)');refresh();pump(1.2)
 G('coop_view_zoom_set(100,640,270)');refresh();pump(1.2)
 G('coop_view_zoom_set(75,640,270)');refresh();pump(1.6)

mark('complete')
if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):G('showcase_record_close()')
print('SHOWCASE_SOLO_COMPLETE',flush=True)
