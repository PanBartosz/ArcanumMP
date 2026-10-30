"""Additional native inventory/crafting, scenery and display-option takes."""
import os
from pathlib import Path
exec(open(Path(os.environ['ARCMP_SHOWCASE_ROOT'])/'tools/showcase/stage.py').read().split('# A little native world movement')[0])
D('set $tp=(TeleportData*)calloc(1,sizeof(TeleportData))')
D('set $tp->obj=$hero');D('set $tp->map=1');D('set $tp->loc=area_get_location(21)')
G('teleport_do($tp)');G('teleport_ping(0)');G('dialog_ui_reset()');G('intgame_mode_set(INTGAME_MODE_MAIN)');G('intgame_show()')
D('set $ptr=(int64_t*)calloc(1,8)')
import re
def P(name):return re.search(r'^#define '+name+r'\s+(\d+)',(ROOT/'upstream/arcanum-ce/src/game/descriptions.h').read_text(),re.M)[1]
for proto in ['BP_DAGGER','BP_FINE_STEEL_DAGGER','BP_SWORD','BP_BOW','BP_REVOLVER','BP_LEATHER_ARMOUR','BP_LEATHER_GLOVES','BP_BOOTS','BP_HELMET','BP_FANCY_RING','BP_HEALING_SALVE','BP_FATIGUE_RESTORER','BP_GINKA_ROOT','BP_KADURA_STEM']:
 if bool(E('object_create(sub_4685A0(%s),obj_field_int64_get($hero,OBJ_F_LOCATION),$ptr)'%P(proto))):
  G('item_transfer(*$ptr,$hero)')
  if proto=='BP_DAGGER':G('item_wield_set(*$ptr,1004)')
G('tech_degree_inc($hero,TECH_HERBOLOGY)');G('tech_degree_inc($hero,TECH_HERBOLOGY)')
for spell in ['SPELL_HARM','SPELL_MINOR_HEALING','SPELL_FIREFLASH','SPELL_STRENGTH_OF_EARTH']:
 G('spell_add($hero,%s,1)'%spell)
G('coop_play_inventory($rows,"Salve",0)');G('coop_play_action_assign(4,$rows)')
G('coop_play_actions($rows,"Harm")');G('coop_play_action_assign(0,$rows)')
G('coop_play_actions($rows,"Minor Healing")');G('coop_play_action_assign(1,$rows)')
G('coop_play_actions($rows,"Fireflash")');G('coop_play_action_assign(2,$rows)')
G('coop_play_actions($rows,"Repair")');G('coop_play_action_assign(3,$rows)')
G('coop_play_inventory($rows,"Dagger",0)');G('coop_play_action_assign(5,$rows)')
tab(1);shot('inventory-populated',4)
mark('inventory-search')
for q in ['D','Da','Dag','Dagger']:query(q);pump(.4)
shot('inventory-equipped-comparison',4)
G("'play_ui.c'::activate(2)");G("'play_ui.c'::refresh()");shot('inventory-junk-mark',3)
query('');G("'play_ui.c'::activate(1)");G("'play_ui.c'::refresh()");shot('inventory-equipment-filter',3)
tab(4);shot('crafting-learned-recipes',4)
G("'play_ui.c'::activate(1)");G("'play_ui.c'::refresh()");shot('craftable-filter',3)
G("'play_ui.c'::activate(2)");G("'play_ui.c'::refresh()");shot('crafting-native-result',3)
G('coop_play_ui_close()');shot('quickslots-all-icons',4)
G('coop_play_pref_set(PLAY_PREF_BAR,0)')

# Correct area names from the game's own catalog. Travel is staged for filming.
for area_id,label in [(21,'Tarant'),(6,'Qintarra'),(11,'Tulla'),(4,'Caladon'),(19,'Isle of Despair'),(22,'Vendigroth Ruins')]:
 G('dialog_ui_reset()');D('set $tp->map=1');D('set $tp->loc=area_get_location(%d)'%area_id)
 G('teleport_do($tp)');G('teleport_ping(0)');G('dialog_ui_reset()');G('intgame_mode_set(INTGAME_MODE_MAIN)');G('intgame_show()')
 # Shift the Tarant view toward its streets, rather than the world-map entry.
 location=V('obj_field_int64_get($hero,OBJ_F_LOCATION)');x=V('location_get_x(%d)'%location);y=V('location_get_y(%d)'%location)
 G('coop_view_zoom_set(125,640,270)')
 shot('scenic-'+label,3)
 mark('pan-'+label)
 G('showcase_pan((void*)scroll_drag_by,-270,0,3000)')
 shot('scenic-end-'+label,2)

# An original interior complements the overworld locations.
for map_id,label in [(7,'Wheel Clan'),(6,'Black Mountain Mines')]:
 G('dialog_ui_reset()');G('move_party(%d)'%map_id);G('intgame_mode_set(INTGAME_MODE_MAIN)');G('intgame_show()')
 G('coop_view_zoom_set(125,640,270)');shot('interior-'+label,5)

# Native settings retain the new display/interaction controls.
G('coop_play_ui_close()')
G('mainmenu_ui_start(MM_TYPE_DEFAULT)');G('mainmenu_ui_create_options()')
shot('native-menu',2)
mark('complete')
if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):G('showcase_record_close()')
print('DETAILS_COMPLETE',flush=True)
