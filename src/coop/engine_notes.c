#include "coop/engine_notes.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "game/obj.h"
#include "game/sector.h"
#include "game/townmap.h"
#include "ui/wmap_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static CoopMapNotes notes;
static bool importing;
static const char path[]="Save\\Current\\arcmp-map-notes.bin";
const CoopMapNotes* coop_engine_notes(void) { return &notes; }
void coop_engine_notes_reset(void) { memset(&notes,0,sizeof(notes)); importing=false; wmap_ui_coop_notes_changed(); }
static bool location_valid(const CoopMapNote* note)
{
    if (!note->context) return note->x<2000 && note->y<2000;
    TownMapInfo info;
    if (!townmap_info((int)note->context,&info)) return false;
    double width=info.width*(double)info.scale, height=info.height*(double)info.scale;
    return isfinite(width) && isfinite(height) && width>0 && height>0 && note->x<width && note->y<height;
}
bool coop_engine_notes_matches(const CoopMapNotes* candidate)
{
    for (unsigned i=0;i<candidate->count;i++) if (!location_valid(&candidate->records[i])) return false;
    return true;
}
void coop_engine_notes_install(const CoopMapNotes* candidate) { notes=*candidate; wmap_ui_coop_notes_changed(); }
CoopNoteStatus coop_engine_note_apply(int64_t actor, const CoopNoteRequest* request)
{
    if (!actor || !coop_authority_can_simulate() || coop_campaign_slot_for_object(actor)<0) return COOP_NOTE_OWNER;
    if (request->operation!=COOP_NOTE_REMOVE && !location_valid(&request->note)) return COOP_NOTE_LOCATION;
    if (request->note.context && request->note.context!=(uint32_t)townmap_get(sector_id_from_loc(obj_field_int64_get(actor,OBJ_F_LOCATION)))) return COOP_NOTE_LOCATION;
    CoopNoteStatus status=coop_map_notes_apply(&notes,request);
    if (!status) wmap_ui_coop_notes_changed();
    return status;
}
bool coop_engine_note_import(uint32_t context, int x, int y, unsigned type, const char* text)
{
    if (!importing || !text || x<0 || y<0 || type>3) return false;
    CoopNoteRequest r={.epoch=1,.sequence=1,.operation=COOP_NOTE_ADD,.note={.context=context,.x=(uint32_t)x,.y=(uint32_t)y,.type=(uint8_t)type}};
    r.actor[0]=1; snprintf(r.note.text,sizeof(r.note.text),"%s",text);
    /* Old empty map-note drafts carry no campaign information. */
    bool visible=false; for (const char* p=text;*p;p++) visible|=*p!=' ';
    if (!visible) return true;
    if (!location_valid(&r.note)) return false;
    return (type == 3 ? coop_map_notes_mark(&notes, &r.note) : coop_map_notes_apply(&notes,&r)) == COOP_NOTE_OK;
}
bool coop_engine_note_mark(uint32_t context, int x, int y, const char* text)
{
    if (!coop_campaign_active()) return false;
    if (!coop_authority_can_simulate()) return true;
    CoopMapNote note={.context=context,.x=(uint32_t)x,.y=(uint32_t)y,.type=3};
    snprintf(note.text,sizeof(note.text),"%s",text ? text : "");
    if (!location_valid(&note) || coop_map_notes_mark(&notes,&note)!=COOP_NOTE_OK) return false;
    wmap_ui_coop_notes_changed(); return true;
}
bool coop_engine_notes_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(path,NULL) || tig_file_remove(path)==0;
    size_t size; uint8_t* bytes=coop_map_notes_encode(&notes,&size); if (!bytes) return false;
    TigFile* file=tig_file_fopen(path,"wb");
    bool good=file && tig_file_fwrite(bytes,1,size,file)==size;
    if (file && tig_file_fclose(file)) good=false;
    free(bytes); return good;
}
bool coop_engine_notes_load(void)
{
    coop_engine_notes_reset();
    if (!coop_campaign_active()) return true;
    if (!tig_file_exists(path,NULL)) {
        importing=true; bool good=wmap_ui_coop_import_notes(); importing=false;
        wmap_ui_coop_notes_changed(); return good;
    }
    TigFile* file=tig_file_fopen(path,"rb"); if (!file) return false;
    int size=tig_file_filelength(file);
    uint8_t* bytes=size>=20 && size<=COOP_NOTES_LIMIT ? malloc((size_t)size) : NULL;
    bool good=bytes && tig_file_fread(bytes,1,(size_t)size,file)==(size_t)size && tig_file_fgetc(file)==EOF && !tig_file_ferror(file);
    if (tig_file_fclose(file)) good=false;
    good=good && coop_map_notes_decode(&notes,bytes,(size_t)size) && coop_engine_notes_matches(&notes);
    free(bytes); wmap_ui_coop_notes_changed(); return good;
}
