"""Native compositor/animation checks, sourced by benchmark_qol.py in GDB."""
# E, G and out come from the native setup in the runner.
gdb.execute('set $r=(TigRect*)calloc(4,sizeof(TigRect))')
gdb.execute('set $wd=(TigWindowData*)calloc(1,sizeof(TigWindowData))')
gdb.execute('set $windows=(tig_window_handle_t*)calloc(3,sizeof(tig_window_handle_t))')
def rect(index,x,y,w,h):
 for field,value in [('x',x),('y',y),('width',w),('height',h)]:gdb.execute('set $r[%d].%s=%d'%(index,field,value))
def shot(name):
 assert int(E('tig_video_screenshot_save(%s)'%json.dumps(str(out/'runtime'/(name+'.bmp')))))==0
def present(name):G('tig_window_display()');shot(name)
for i,(x,y) in enumerate([(100,120),(420,120),(740,120)]):
 for field,value in [('x',x),('y',y),('width',256),('height',180)]:gdb.execute('set $wd->rect.%s=%d'%(field,value))
 gdb.execute('set $wd->background_color=0')
 assert int(E('tig_window_create($wd,$windows+%d)'%i))==0
rect(0,0,0,256,180)
G('tig_window_fill($windows[0],$r,0xCC3311)')
G('tig_window_display()')
rect(1,0,0,120,80);rect(2,30,20,120,80)
assert int(E('tig_window_copy($windows[1],$r+2,$windows[0],$r+1)'))==0
present('copy-actual')
G('tig_window_invalidate_rect(0)');present('copy-expected')

# Surface clipping must equal an unclipped scaled reference followed by a crop.
gdb.execute('set $ci=(TigVideoBufferCreateInfo*)calloc(1,sizeof(TigVideoBufferCreateInfo))')
gdb.execute('set $vb=(TigVideoBuffer**)calloc(2,sizeof(TigVideoBuffer*))')
gdb.execute('set $ci->flags=4');gdb.execute('set $ci->width=128');gdb.execute('set $ci->height=128')
assert int(E('tig_video_buffer_create($ci,$vb)'))==0
G('qol_pattern($vb[0]->surface->pixels,128,128,$vb[0]->surface->pitch)')
gdb.execute('set $ci->width=512');gdb.execute('set $ci->height=512')
assert int(E('tig_video_buffer_create($ci,$vb+1)'))==0
gdb.execute('set $blit=(TigVideoBufferBlitInfo*)calloc(1,sizeof(TigVideoBufferBlitInfo))')
gdb.execute('set $save=(TigVideoBufferSaveToBmpInfo*)calloc(1,sizeof(TigVideoBufferSaveToBmpInfo))')
def save_buffer(buffer,name):
 gdb.execute('set $save->video_buffer='+buffer)
 G('(char*)strcpy($save->path,%s)'%json.dumps(str(out/'runtime'/(name+'.bmp'))))
 assert int(E('tig_video_buffer_save_to_bmp($save)'))==0
for size in [64,96,160,192,256]:
 G('tig_video_buffer_fill($vb[1],0,0)')
 rect(0,0,0,128,128);rect(1,-16,-24,size,size)
 gdb.execute('set $blit->src_video_buffer=$vb[0]');gdb.execute('set $blit->dst_video_buffer=$vb[1]')
 gdb.execute('set $blit->src_rect=$r');gdb.execute('set $blit->dst_rect=$r+1')
 assert int(E('tig_video_buffer_blit($blit)'))==0
 save_buffer('$vb[1]','scale-%d-actual'%(size*100//128))
 G('tig_video_buffer_fill($vb[1],0,0)')
 assert bool(E('SDL_BlitSurfaceScaled($vb[0]->surface,(SDL_Rect*)$r,$vb[1]->surface,(SDL_Rect*)($r+1),SDL_SCALEMODE_NEAREST)'))
 save_buffer('$vb[1]','scale-%d-expected'%(size*100//128))

# Screen-edge blit, including both negative coordinates.
rect(0,0,0,128,128);rect(1,-16,-24,128,128)
assert int(E('tig_video_blit($vb[0],$r,$r+1)'))==0
shot('edge-actual')
assert bool(E("SDL_BlitSurface($vb[0]->surface,(SDL_Rect*)$r,'video.c'::tig_video_state.surface,(SDL_Rect*)($r+1))"))
shot('edge-expected')

# Overlapping translucent windows must be identical after a move and a complete
# recomposition. The scene is static; no game clock or animation runs here.
G('tig_window_opacity_set($windows[1],128)');G('tig_window_opacity_set($windows[2],191)')
G('tig_window_move($windows[1],160,145)');G('tig_window_move($windows[2],180,160)')
G('tig_window_invalidate_rect(0)');G('tig_window_display()')
G('tig_window_move($windows[1],200,175)');present('alpha-move-actual')
G('tig_window_invalidate_rect(0)');present('alpha-move-expected')
for i in [2,1,0]:G('tig_window_destroy($windows[%d])'%i)

# Duplicate samples arriving during a movement blend must not keep delaying its
# completion. Use the same authoritative offset repeatedly, then inspect the
# actual native rendered correction after more than the original blend length.
G('coop_authority_set(COOP_AUTHORITY_REPLICA)');G('object_replica_pose_reset()')
hero=int(E('player_get_local_pc_obj()'));loc=int(E('obj_field_int64_get(%d,OBJ_F_LOCATION)'%hero))
aid=int(E('obj_field_int32_get(%d,OBJ_F_CURRENT_AID)'%hero))
x=int(E('obj_field_int32_get(%d,OBJ_F_OFFSET_X)'%hero));y=int(E('obj_field_int32_get(%d,OBJ_F_OFFSET_Y)'%hero))
G('qol_duplicates((void*)object_replica_pose,(void*)object_ping,(void*)SDL_GetTicks,(void*)SDL_Delay,%d,%d,%d,%d,%d)'%(hero,loc,x,y,aid))
visual=E("*'object.c'::replica_visual(%d,0)"%hero)
checks={'duplicate_pose_residual_pixels':abs(int(visual['x']))+abs(int(visual['y']))}
G('coop_authority_set(COOP_AUTHORITY_HOST)');G('object_replica_pose_reset()')

# Real gameplay pose-message handling must redraw a stationary actor whose
# visibility flags change, even when position and art are identical.
import struct
gdb.execute('set $replica=(CoopGameplay*)calloc(1,sizeof(CoopGameplay))')
gdb.execute('set $replica_config=(CoopConfig*)calloc(1,sizeof(CoopConfig))')
G('(char*)strcpy($replica_config->name,"Visual check")')
gdb.execute('set $replica->session=coop_session_create($replica_config)')
gdb.execute('set $replica->epoch=1');gdb.execute('set $replica->running=1')
gdb.execute('set $replica->map=map_current_map()')
gdb.execute('set $pose_packet=(unsigned char*)calloc(1,205)')
packet=bytearray(205)
struct.pack_into('!III',packet,0,1,1,int(E('map_current_map()')));packet[12]=4
local_slot=None
for i in range(4):
 actor=int(E('coop_campaign_object(%d)'%i))
 if actor==hero:local_slot=i
 identity=E('coop_campaign_member(%d)->object_id.d.g.data'%i)
 at=13+i*48;packet[at:at+16]=bytes(gdb.selected_inferior().read_memory(identity.address,16))
 position=int(E('obj_field_int64_get(%d,OBJ_F_LOCATION)'%actor))
 values=[position&0xffffffff,position>>32]
 for field in ['OBJ_F_OFFSET_X','OBJ_F_OFFSET_Y','OBJ_F_CURRENT_AID','OBJ_F_FLAGS','OBJ_F_HP_DAMAGE','OBJ_F_CRITTER_FATIGUE_DAMAGE']:
  values.append(int(E('obj_field_int32_get(%d,%s)'%(actor,field)))&0xffffffff)
 struct.pack_into('!8I',packet,at+16,*values)
def receive_packet():
 gdb.selected_inferior().write_memory(int(E('$pose_packet')),packet)
 assert bool(E("'gameplay.c'::message($replica,0,25,$pose_packet,205)"))
G('coop_authority_set(COOP_AUTHORITY_REPLICA)');receive_packet()
G('object_ping(SDL_GetTicks())');G('gamelib_redraw()');G('iso_redraw()');G('tig_window_display()')
for i in range(10):G('iso_redraw()');G('tig_window_display()')
assert local_slot is not None
flag_at=13+local_slot*48+36
original_flags=struct.unpack_from('!I',packet,flag_at)[0]
struct.pack_into('!I',packet,4,2);struct.pack_into('!I',packet,flag_at,original_flags|0x100)
receive_packet();G('object_ping(SDL_GetTicks())');G('iso_redraw()');present('pose-visibility-actual')
G('gamelib_redraw()');G('iso_redraw()');present('pose-visibility-expected')
struct.pack_into('!I',packet,4,3);struct.pack_into('!I',packet,flag_at,original_flags)
receive_packet();G('gamelib_redraw()');G('iso_redraw()');G('tig_window_display()')
G('coop_authority_set(COOP_AUTHORITY_HOST)');G('object_replica_pose_reset()')

try:
 E('coop_play_shortcuts')
 gdb.execute('set $hint=(char*)calloc(1,512)')
 G('coop_play_binding_set(6,SDL_SCANCODE_U)')
 G('coop_play_shortcuts("Inventory [I] / (I)",$hint,512)')
 checks['rebound_hint']=E('$hint').string()
 assert checks['rebound_hint']=='Inventory [U] / (U)'
 G('coop_play_binding_reset()')
except gdb.error:checks['rebound_hint']='baseline: unavailable'

# Actual hover panel sizing at the selected screen/text scale. Borrow only the
# UI's playing flag; the network benchmark separately uses four real engines.
gdb.execute("set $saved_runtime='runtime.c'::runtime")
gdb.execute("set 'runtime.c'::runtime=(CoopRuntime*)calloc(1,sizeof(CoopRuntime))")
gdb.execute("set 'runtime.c'::runtime->phase=COOP_RUNTIME_PLAYING")
G('coop_play_pref_set(PLAY_PREF_TEXT,150)')
G('coop_party_ui_tick(SDL_GetTicks())')
party=E("'party_ui.c'::party_rect")
mx=int(party['x'])+25;my=int(party['y'])+30
G('tig_mouse_set_position(%d,%d)'%(mx,my));G('coop_party_ui_tick(SDL_GetTicks())')
G('SDL_Delay(700)');G('coop_party_ui_tick(SDL_GetTicks())')
tip=int(E("'party_ui.c'::tip"))
assert tip!=-1
gdb.execute('set $tipdata=(TigWindowData*)calloc(1,sizeof(TigWindowData))')
assert int(E('tig_window_data(%d,$tipdata)'%tip))==0
r=E('$tipdata->rect');checks['tooltip_bounds']={k:int(r[k]) for k in ['x','y','width','height']}
gdb.execute('set $measured=(TigFont*)calloc(1,sizeof(TigFont))')
gdb.execute("set $measured->str='party_ui.c'::hover_text")
gdb.execute('set $measured->width=%d'%(int(r['width'])-18))
G('tig_font_push(coop_play_font(MM_COLOR_WHITE))');G('tig_font_measure($measured)');G('tig_font_pop()')
checks['tooltip_text_height']=int(E('$measured->height'))
checks['tooltip_fits']=int(r['height'])>=checks['tooltip_text_height']+14
# Compare the measurement-sized write with an unconstrained write. Merely
# checking the measured height could miss a shared measurement/layout error.
G('tig_video_buffer_destroy($vb[0])')
assert int(E('tig_video_buffer_create($ci,$vb)'))==0
G('tig_video_buffer_fill($vb[0],0,0)');G('tig_video_buffer_fill($vb[1],0,0)')
rect(0,0,0,372,checks['tooltip_text_height']);rect(1,0,0,372,500)
G('tig_font_push(coop_play_font(MM_COLOR_WHITE))')
G("tig_font_write($vb[0],'party_ui.c'::hover_text,$r,0)")
G("tig_font_write($vb[1],'party_ui.c'::hover_text,$r+1,0)")
G('tig_font_pop()')
save_buffer('$vb[0]','text-measure-actual');save_buffer('$vb[1]','text-measure-expected')
G('gamelib_redraw()');G('iso_redraw()');present('hover-150')
G('coop_party_ui_close()')
gdb.execute("set 'runtime.c'::runtime=$saved_runtime")
for tab in [1,2,3,4,5]:
 assert bool(E('coop_play_ui_open(%d)'%tab))
 G('gamelib_redraw()');G('iso_redraw()');present('tools-150-%d'%tab)
 G('coop_play_ui_close()')
(out/'visual.json').write_text(json.dumps(checks,indent=2)+'\n')
print('QOL_VISUAL_PASS',flush=True)
