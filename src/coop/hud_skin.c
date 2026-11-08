#include "coop/hud.h"
#include "coop/play_data.h"
#include "game/magictech.h"
#include "game/obj_pool.h"
#include "game/spell.h"
#include "ui/intgame.h"
#include "ui/skill_ui.h"
#include "hud_skin_data.h"
#include <string.h>

/* Art is embedded and decoded lazily once per component. Drawing never reads
 * loose files or regenerates textures, including during portrait animation. */
typedef struct Skin {
    const char* name;
    const unsigned char* bytes;
    size_t length;
    int width, height, border;
    TigVideoBuffer* buffer;
    bool attempted;
} Skin;
#define SKIN(name,w,h,b) {#name,hud_##name##_bmp,sizeof(hud_##name##_bmp),w,h,b,NULL,false}
static Skin skins[HUD_ART_COUNT] = {
    SKIN(iron_panel,256,256,22), SKIN(combat_journal,64,64,0),
    SKIN(status_dead,48,48,0), SKIN(status_unconscious,48,48,0),
    SKIN(status_poisoned,48,48,0), SKIN(status_tired,48,48,0),
    SKIN(status_stunned,48,48,0), SKIN(status_paralyzed,48,48,0),
    SKIN(status_blinded,48,48,0), SKIN(status_injured,48,48,0),
    SKIN(status_encumbered,48,48,0),
    SKIN(control_close,48,48,0), SKIN(control_compact,48,48,0),
    SKIN(control_expand,48,48,0), SKIN(control_wide,48,48,0),
    SKIN(control_opacity,48,48,0), SKIN(control_move,48,48,0),
    SKIN(control_resize,48,48,0),
    SKIN(turn_card,256,112,18), SKIN(turn_card_active,256,112,18),
    SKIN(action_slot,256,96,14), SKIN(portrait_bezel,256,256,22),
    SKIN(button_face,256,96,14),
    SKIN(quick_save,48,48,0), SKIN(quick_load,48,48,0)
};
#undef SKIN
static TigVideoBuffer* decode(Skin* skin)
{
    if (skin->attempted) return skin->buffer;
    skin->attempted=true;
    SDL_Surface* bmp=SDL_LoadBMP_IO(SDL_IOFromConstMem(skin->bytes,skin->length),true);
    if (!bmp) return NULL;
    SDL_Surface* rgb=SDL_ConvertSurface(bmp,SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(bmp);
    if (!rgb) return NULL;
    if (rgb->w!=skin->width || rgb->h!=skin->height) { SDL_DestroySurface(rgb); return NULL; }
    TigVideoBufferCreateInfo info={.flags=TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY,.width=rgb->w,.height=rgb->h};
    TigVideoBuffer* buffer=NULL;
    if (tig_video_buffer_create(&info,&buffer)==TIG_OK) {
        TigVideoBufferData data; bool copied=false;
        if (tig_video_buffer_lock(buffer)==TIG_OK) {
            if (tig_video_buffer_data(buffer,&data)==TIG_OK && data.bpp==32) {
                for (int y=0;y<rgb->h;y++) memcpy((uint8_t*)data.pixels+y*data.pitch,(uint8_t*)rgb->pixels+y*rgb->pitch,(size_t)rgb->w*4);
                copied=true;
            }
            tig_video_buffer_unlock(buffer);
        }
        if (!copied) { tig_video_buffer_destroy(buffer); buffer=NULL; }
    }
    SDL_DestroySurface(rgb); skin->buffer=buffer; return buffer;
}
static void blit(tig_window_handle_t window,TigVideoBuffer* buffer,TigRect source,TigRect target,bool smooth)
{
    if (target.width<=0 || target.height<=0) return;
    TigWindowBlitInfo info={.type=TIG_WINDOW_BLIT_VIDEO_BUFFER_TO_WINDOW,.src_video_buffer=buffer,
        .src_rect=&source,.dst_window_handle=window,.dst_rect=&target,
        .vb_blit_flags=smooth ? TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR : 0};
    tig_window_blit(&info);
}
const char* coop_hud_art_name(unsigned art) { return art<HUD_ART_COUNT ? skins[art].name : "Unknown"; }
bool coop_hud_art(tig_window_handle_t window,unsigned art,TigRect r)
{
    if (art>=HUD_ART_COUNT) return false;
    Skin* skin=&skins[art]; TigVideoBuffer* buffer=decode(skin);
    if (!buffer) return false;
    blit(window,buffer,(TigRect){0,0,skin->width,skin->height},r,true); return true;
}
void coop_hud_surface(tig_window_handle_t window,unsigned art,TigRect r,bool active)
{
    Skin* skin=art<HUD_ART_COUNT ? &skins[art] : &skins[HUD_ART_PANEL];
    TigVideoBuffer* buffer=decode(skin);
    if (buffer && skin->border) {
        int b=art==HUD_ART_PANEL ? (r.height<45 ? 4 : 8) : art==HUD_ART_PORTRAIT ? 3 : r.height<40 ? 3 : 4;
        b=SDL_min(b,SDL_min(r.width/3,r.height/3));
        int sx[4]={0,skin->border,skin->width-skin->border,skin->width};
        int sy[4]={0,skin->border,skin->height-skin->border,skin->height};
        int x[4]={r.x,r.x+b,r.x+r.width-b,r.x+r.width},y[4]={r.y,r.y+b,r.y+r.height-b,r.y+r.height};
        for (int row=0;row<3;row++) for (int col=0;col<3;col++)
            blit(window,buffer,(TigRect){sx[col],sy[row],sx[col+1]-sx[col],sy[row+1]-sy[row]},
                (TigRect){x[col],y[row],x[col+1]-x[col],y[row+1]-y[row]},false);
    } else {
        tig_window_fill(window,&r,tig_color_make(24,20,16));
        tig_window_box(window,&r,tig_color_make(118,93,58));
    }
    if (active) { r.x+=2;r.y+=2;r.width-=4;r.height-=4;tig_window_box(window,&r,tig_color_make(230,190,100)); }
}
void coop_hud_frame(tig_window_handle_t window,TigRect r,bool active)
{ coop_hud_surface(window,HUD_ART_PANEL,r,active); }
void coop_hud_icon(tig_window_handle_t window,TigRect r,bool active)
{
    if (!coop_hud_art(window,HUD_ART_JOURNAL,r)) coop_hud_frame(window,r,active);
    if (active) tig_window_box(window,&r,tig_color_make(245,212,130));
}
bool coop_hud_action_icon(tig_window_handle_t window,const PlayRow* row,TigRect r)
{
    if (!row) return false;
    tig_art_id_t art=TIG_ART_ID_INVALID;
    if (row->kind==HOTKEY_ITEM && row->object && obj_handle_is_valid(row->object)) art=sub_554BE0(row->object);
    else if ((row->kind==HOTKEY_SPELL || row->kind==HOTKEY_ITEM_SPELL) && row->id>=0 && row->id<MT_SPELL_COUNT)
        tig_art_interface_id_create(spell_icon(row->id),0,0,0,&art);
    else if (row->kind==HOTKEY_SKILL && row->id>=0 && row->id<4)
        tig_art_interface_id_create(sub_579F70(row->id),0,0,0,&art);
    TigArtFrameData frame;
    if (art==TIG_ART_ID_INVALID || tig_art_frame_data(art,&frame)!=TIG_OK || frame.width<=0 || frame.height<=0) return false;
    double scale=SDL_min((double)r.width/frame.width,(double)r.height/frame.height);
    int width=SDL_max(1,(int)(frame.width*scale)),height=SDL_max(1,(int)(frame.height*scale));
    TigRect src={0,0,frame.width,frame.height},dst={r.x+(r.width-width)/2,r.y+(r.height-height)/2,width,height};
    TigArtBlitInfo blit_info={.art_id=art,.src_rect=&src,.dst_rect=&dst};
    if (!row->available) { blit_info.flags=TIG_ART_BLT_BLEND_COLOR_CONST; blit_info.color=tig_color_make(110,105,95); }
    return tig_window_blit_art(window,&blit_info)==TIG_OK;
}
