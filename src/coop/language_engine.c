#include "coop/language_engine.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include <stdlib.h>
#include <string.h>
#include <tig/tig.h>
static bool read_resource(const char* path, char** data, size_t* size)
{
    *data = NULL;
    *size = 0;
    TigFile* file = tig_file_fopen(path, "rb");
    if (!file) return false;
    int length = tig_file_filelength(file);
    char* bytes = length > 0 && length <= 16 * 1024 * 1024 ? malloc((size_t)length) : NULL;
    bool good = bytes && tig_file_fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (tig_file_fclose(file)) good = false;
    if (!good) {
        free(bytes);
        return false;
    }
    *data = bytes;
    *size = (size_t)length;
    return true;
}
void coop_language_engine_init(void)
{
    coop_language_init();
    coop_language_reader(read_resource);
    tig_file_read_override_set(coop_language_asset);
}
static void names(int64_t pc, int64_t npc, char pc_name[2000], char npc_name[2000])
{
    pc_name[0] = npc_name[0] = 0;
    if (pc && obj_handle_is_valid(pc)) object_examine(pc, OBJ_HANDLE_NULL, pc_name);
    if (npc && obj_handle_is_valid(npc)) object_examine(npc, pc, npc_name);
}
const char* coop_language_dialogue(const char* text, int64_t pc, int64_t npc, char* buffer, size_t size)
{
    CoopTextRef ref = coop_language_reference(text);
    char pc_name[2000], npc_name[2000];
    names(pc, npc, pc_name, npc_name);
    return coop_language_resolve(&ref, pc_name, npc_name, text, buffer, size) ? buffer : text;
}
void coop_language_view(CoopDialogueView* view)
{
    int64_t pc = OBJ_HANDLE_NULL, npc = obj_pool_perm_lookup(coop_object_id_to_engine(view->npc));
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if (member && !memcmp(member->object_id.d.g.data, view->pc_guid, 16)) {
            pc = coop_campaign_object(i);
            break;
        }
    }
    char pc_name[2000], npc_name[2000];
    names(pc, npc, pc_name, npc_name);
    for (unsigned i = 0; i < view->option_count + 2; i++) {
        char* text = i == 0 ? view->reply : i == 1 ? view->selected
                                                   : view->options[i - 2];
        coop_language_resolve(&view->text_refs[i], pc_name, npc_name, text, text, COOP_DIALOGUE_TEXT);
    }
}
