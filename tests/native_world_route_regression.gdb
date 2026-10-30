# Run from an isolated writable runtime with arcmp-bootstrap saved on map1.
# gdb -batch -x /absolute/path/to/this/file --args /path/to/arcanum-mp --coop-engine-probe=dialogue-semantics
set pagination off
set confirm off
set debuginfod enabled off
break verify_dialogue_semantics
run
call coop_authority_set(COOP_AUTHORITY_HOST)
print gamelib_load("arcmp-bootstrap")
call sub_40DAF0(coop_campaign_object(0))
set $loc=obj_field_int64_get(player_get_local_pc_obj(),OBJ_F_LOCATION)
printf "ORIGIN %lld,%lld blocked=%d town=%d\n",$loc&0xffffffff,$loc>>32,sector_is_blocked(sector_id_from_loc($loc)),townmap_get(sector_id_from_loc($loc))
call wmap_ui_open()
set $path=(WmapPathInfo*)calloc(1,sizeof(WmapPathInfo))
set $path->max_rotations=5000
set $path->rotations=(signed char*)malloc(5000)
python
import gdb,json
p=gdb.parse_and_eval('$path');buf=p['rotations']; inf=gdb.selected_inferior();rows=[]
for a,b in [((32251,62592),(62560,65632)),((62560,65632),(83690,48174)),((83690,48174),(62560,65632))]:
 for capacity in [5000,475,474,473,330,329,1]:
  gdb.execute('set $path->from=sector_id_from_loc(location_make(%d,%d))'%a)
  gdb.execute('set $path->to=sector_id_from_loc(location_make(%d,%d))'%b)
  gdb.execute('set $path->max_rotations=%d'%capacity)
  inf.write_memory(int(buf),bytes([127])*5000)
  n=int(gdb.parse_and_eval('wmap_path_make($path)'))
  raw=bytes(inf.read_memory(int(buf),5000))
  row=dict(source=a,target=b,capacity=capacity,count=n,bounds=n<=capacity,guard=raw[capacity:]==bytes([127])*(5000-capacity))
  if n:
   at=int(p['from']);ok=True
   for rot in raw[:n]:
    if rot>7:ok=False;break
    gdb.execute('set $sector=%d'%at)
    if not bool(gdb.parse_and_eval('sector_in_dir($sector,%d,&$path->from)'%rot)):ok=False;break
    at=int(p['from'])
    if bool(gdb.parse_and_eval('sector_is_blocked(%d)'%at)) or (not bool(gdb.parse_and_eval('sector_exists(%d)'%at)) and bool(gdb.parse_and_eval('terrain_is_blocked(%d)'%at))):ok=False;break
   row['decoded_valid']=ok and at==int(p['to'])
  rows.append(row)
print('ORACLE_JSON='+json.dumps(rows))
assert all(r['bounds'] and r['guard'] and r.get('decoded_valid',True) for r in rows)
assert any(r['count'] == r['capacity'] for r in rows)
end
quit
