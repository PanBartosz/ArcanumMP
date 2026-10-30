#include "ui/slide_ui.h"
#include "coop/runtime.h"
#include "coop/authority.h"

#include <stdio.h>

#include "game/gamelib.h"
#include "game/gfade.h"
#include "game/gsound.h"
#include "game/hrp.h"
#include "game/mes.h"
#include "game/script.h"

/**
 * The maximum number of enqueued slides.
 */
#define MAX_QUEUE_SIZE 100

static void slide_ui_prepare(int type);
static bool slide_ui_do_slide(tig_window_handle_t window_handle, int slide);
static bool slide_ui_do_slide_local(tig_window_handle_t window_handle, int slide);
static bool slide_ui_get_assets(int slide, char* bmp_path, size_t bmp_path_maxlen, char* speech_path, size_t speech_path_maxlen);
static void slide_ui_fade_out(void);
static void slide_ui_fade_in(void);
static void slide_ui_queue_clear(void);

/**
 * Current number of slides in the queue.
 *
 * 0x67B970
 */
static int slide_ui_queue_size;

/**
 * "slide.mes"
 *
 * 0x67B974
 */
static mes_file_handle_t slide_ui_slide_mes_file;

/**
 * Array storing the numbers of queued slides.
 *
 * 0x67B978
 */
static int slide_ui_queue[MAX_QUEUE_SIZE];

/**
 * Flag indicating if the slide UI is active.
 *
 * 0x67BB08
 */
static bool slide_ui_active;
static int slide_ui_coop_displayed = -1;
int slide_ui_coop_current(void) { return slide_ui_coop_displayed; }

/**
 * Called when a module is being loaded.
 *
 * 0x5695B0
 */
bool slide_ui_mod_load(void)
{
    mes_load("rules\\slide.mes", &slide_ui_slide_mes_file);
    return true;
}

/**
 * Called when a module is being unloaded.
 *
 * 0x5695D0
 */
void slide_ui_mod_unload(void)
{
    mes_unload(slide_ui_slide_mes_file);
    slide_ui_slide_mes_file = MES_FILE_HANDLE_INVALID;
}

/**
 * Checks if the slide UI is active.
 *
 * 0x5695F0
 */
bool slide_ui_is_active(void)
{
    return slide_ui_active;
}

/**
 * Starts slide UI.
 *
 * 0x569600
 */
void slide_ui_start(int type)
{
    TigWindowData window_data;
    tig_window_handle_t window_handle;
    tig_sound_handle_t sound_handle;
    int index;
    if (coop_runtime_phase() != COOP_RUNTIME_OFF && !coop_authority_can_simulate()) return;

    slide_ui_active = true;

    // Remove all enqeueued slides.
    slide_ui_queue_clear();

    // Run script associated with slide UI type.
    slide_ui_prepare(type);

    // From `slide.mes`: "Slide 0 is the DEFAULT death slide. If the player dies
    // and no slide is queued by script 998, then slide 0 is used."
    if (type == SLIDE_UI_TYPE_DEATH
        && slide_ui_queue_size == 0
        && slide_ui_slide_mes_file != MES_FILE_HANDLE_INVALID) {
        slide_ui_enqueue(0);
    }

    // Perform initial fade-out.
    slide_ui_fade_out();

    // Display slides if queue is not empty.
    if (slide_ui_queue_size > 0) {
        // Set up window properties.
        window_data.flags = TIG_WINDOW_ALWAYS_ON_TOP;
        window_data.rect.x = 0;
        window_data.rect.y = 0;
        window_data.rect.width = 800;
        window_data.rect.height = 600;
        window_data.background_color = 0;
        hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

        // Create the slide window.
        if (tig_window_create(&window_data, &window_handle) == TIG_OK) {
            gsound_stop_all(25);
            tig_mouse_hide();
            sound_handle = gsound_play_music_indefinitely("sound\\music\\DwarvenMusic.mp3", 25);

            // Present each slide in the queue.
            for (index = 0; index < slide_ui_queue_size; index++) {
                if (!slide_ui_do_slide(window_handle, slide_ui_queue[index])) {
                    // Escape was pressed - stop slideshow.
                    break;
                }
            }

            // Clean up resources.
            tig_sound_destroy(sound_handle);
            tig_window_destroy(window_handle);

            tig_mouse_show();
            tig_window_display();
        }
    }

    // Perform fade-in when we're presenting credits. Otherwise the caller is
    // responsible to remove fading.
    if (type == SLIDE_UI_TYPE_CREDITS) {
        slide_ui_fade_in();
    }

    slide_ui_active = false;
}

/**
 * Configures the slide UI by executing a script for the given type.
 *
 * The script is assumed to call `slide_ui_enqueue` via appropriate action, up
 * to `MAX_QUEUE_SIZE` number of times.
 *
 * 0x569720
 */
void slide_ui_prepare(int type)
{
    Script scr = { 0 };
    ScriptInvocation invocation;

    scr.num = dword_5A5700[type];

    invocation.script = &scr;
    invocation.attachment_point = SAP_USE;
    invocation.triggerer_obj = OBJ_HANDLE_NULL;
    invocation.attachee_obj = OBJ_HANDLE_NULL;
    invocation.extra_obj = OBJ_HANDLE_NULL;
    invocation.line = 0;
    script_execute(&invocation);
}

/**
 * Performs the display of a single slide.
 *
 * Returns `true` if slideshow show continue, `false` if slide was interrupted
 * by pressing ESC.
 *
 * CE: The original code uses TIG BMP API. This implementation prefers to load
 * slide directly into the video buffer.
 *
 * 0x569770
 */
bool slide_ui_do_slide(tig_window_handle_t window_handle, int slide)
{
    if (coop_runtime_present(COOP_PRESENT_SLIDE, (unsigned)slide, NULL, 0, 0)) return coop_runtime_phase() != COOP_RUNTIME_FAILED;
    return slide_ui_do_slide_local(window_handle, slide);
}
bool slide_ui_do_slide_local(tig_window_handle_t window_handle, int slide)
{
    TigMessage msg;
    TigRect rect;
    TigVideoBuffer* video_buffer;
    TigWindowBlitInfo blit_info;
    char bmp_path[TIG_MAX_PATH];
    char speech_path[TIG_MAX_PATH];
    tig_sound_handle_t speech_handle;
    bool cont = true;
    bool stop;
    tig_timestamp_t start;
    tig_duration_t elapsed;

    // Set slide rect.
    rect.x = 0;
    rect.y = 0;
    rect.width = 800;
    rect.height = 600;

    if (slide_ui_get_assets(slide, bmp_path, sizeof(bmp_path), speech_path, sizeof(speech_path))) {
        // Load image.
        if (tig_video_buffer_load_from_bmp(bmp_path, &video_buffer, 0x01) == TIG_OK) {
            // Put image into window.
            blit_info.type = TIG_WINDOW_BLIT_VIDEO_BUFFER_TO_WINDOW;
            blit_info.vb_blit_flags = 0;
            blit_info.src_rect = &rect;
            blit_info.src_video_buffer = video_buffer;
            blit_info.dst_rect = &rect;
            blit_info.dst_window_handle = window_handle;
            tig_window_blit(&blit_info);
            tig_video_buffer_destroy(video_buffer);

            // Refresh screen.
            sub_51E850(window_handle);
            tig_window_display();

            // Perform fade-in effect.
            slide_ui_fade_in();

            // Play voiceover.
            speech_handle = gsound_play_voice(speech_path, 0);
            if (coop_runtime_present_active()) slide_ui_coop_displayed = slide;

            // Clear initial message queue.
            while (tig_message_dequeue(&msg) == TIG_OK) {
                if (msg.type == TIG_MESSAGE_REDRAW) {
                    gamelib_redraw();
                }
            }

            // NOTE: There is an interesting bug related to the slideshow.
            // Occasionally, the slides are played in rapid succession. This
            // happens because the underlying `tig_sound_play_streamed_once`
            // does not start the speech immediately. Instead, it marks the
            // speech handle as active and ready to fade in (even with a fade
            // duration of 0). If the host drains the message queue (below in
            // the loop) too quickly, it may not trigger `tig_sound_update`,
            // which is responsible for starting the stream. Consequently, by
            // the time the iteration completes, the stream may not have started
            // at all, causing `tig_sound_is_playing` to report `false` and
            // immediately ending the loop.
            //
            // The fix is adapted from ToEE, where this function has been moved
            // to TIG as `tig_movie_play_slide`. The solution involves adding a
            // minimum delay of 3 seconds per slide. This delay not only serves
            // as a fallback in case the voiceover is missing for any reason,
            // but it also gives the sound system enough time to start the
            // voiceover (which is typically longer than 3 seconds).
            tig_timer_now(&start);

            stop = false;
            do {
                elapsed = tig_timer_elapsed(start);

                // Pump events.
                tig_ping();
                if (coop_runtime_present_active()) {
                    TigRect bar = { 0, 566, 800, 34 }, label = { 12, 572, 776, 24 };
                    tig_window_fill(window_handle, &bar, 0);
                    tig_window_text_write(window_handle, coop_runtime_present_status(), &label);
                    tig_window_display();
                }
                if (coop_runtime_present_active() && coop_runtime_present_pump()) { stop = true; break; }

                // Process input.
                while (tig_message_dequeue(&msg) == TIG_OK) {
                    if (msg.type == TIG_MESSAGE_KEYBOARD) {
                        if (msg.data.keyboard.pressed == false) {
                            if (coop_runtime_present_active()) { coop_runtime_present_input(); continue; }
                            stop = true;
                            if (msg.data.keyboard.scancode == SDL_SCANCODE_ESCAPE) {
                                // Interrupt slideshow.
                                cont = false;
                            }
                        }
                    } else if (msg.type == TIG_MESSAGE_MOUSE) {
                        if (msg.data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP
                            || msg.data.mouse.event == TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP
                            || msg.data.mouse.event == TIG_MESSAGE_MOUSE_MIDDLE_BUTTON_UP) {
                            if (coop_runtime_present_active()) { coop_runtime_present_input(); continue; }
                            // Interrupt this slide.
                            stop = true;
                        }
                    } else if (msg.type == TIG_MESSAGE_REDRAW) {
                        gamelib_redraw();
                    }
                }

                if (stop) {
                    break;
                }
                SDL_Delay(2);
            } while ((speech_handle != TIG_SOUND_HANDLE_INVALID && tig_sound_is_playing(speech_handle)) || elapsed < 3000);

            // Clean up voiceover.
            tig_sound_destroy(speech_handle);
        }

        // Clear remaining message queue.
        while (tig_message_dequeue(&msg) == TIG_OK) {
            if (msg.type == TIG_MESSAGE_REDRAW) {
                gamelib_redraw();
            }
        }

        // Perform fade-out effect.
        slide_ui_fade_out();
    }

    return cont;
}
bool slide_ui_coop_probe(int slide)
{
    char bmp[TIG_MAX_PATH], speech[TIG_MAX_PATH]; TigVideoBuffer* buffer = NULL;
    if (!slide_ui_get_assets(slide, bmp, sizeof(bmp), speech, sizeof(speech)) || tig_video_buffer_load_from_bmp(bmp, &buffer, 1) != TIG_OK) return false;
    tig_video_buffer_destroy(buffer);
    TigFile* voice = tig_file_fopen(speech, "rb"); if (!voice) return false;
    return tig_file_fclose(voice) == 0;
}
static bool slide_coop_input(TigMessage* message)
{
    if (message->type == TIG_MESSAGE_KEYBOARD) { if (!message->data.keyboard.pressed) coop_runtime_present_input(); return true; }
    if (message->type == TIG_MESSAGE_MOUSE) { if (message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) coop_runtime_present_input(); return true; }
    return message->type == TIG_MESSAGE_TEXT_INPUT;
}
bool slide_ui_coop_present(int slide)
{
    TigWindowData data = { .flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER, .rect = { 0, 0, 800, 600 }, .message_filter = slide_coop_input };
    hrp_apply(&data.rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);
    tig_window_handle_t window;
    if (tig_window_create(&data, &window) != TIG_OK) return false;
    bool was_active = slide_ui_active; slide_ui_active = true;
    bool result = slide_ui_do_slide_local(window, slide);
    slide_ui_coop_displayed = -1;
    slide_ui_active = was_active; tig_window_destroy(window); return result;
}

/**
 * Retrieves asset paths for a slide from the message file.
 *
 * 0x569930
 */
bool slide_ui_get_assets(int slide, char* bmp_path, size_t bmp_path_maxlen, char* speech_path, size_t speech_path_maxlen)
{
    MesFileEntry mes_file_entry;
    char temp[MAX_STRING];
    char* tok;

    // Retrieve message file entry.
    mes_file_entry.num = slide;
    if (!mes_search(slide_ui_slide_mes_file, &mes_file_entry)) {
        return false;
    }

    // Each slide contains BMP file name and MP3 voiceover file name. Each piece
    // is mandatory.
    strcpy(temp, mes_file_entry.str);

    // Build BMP path.
    tok = strtok(temp, " \t\n");
    if (tok == NULL) {
        return false;
    }
    snprintf(bmp_path, bmp_path_maxlen, "slide\\%s", tok);

    // Build MP3 path.
    tok = strtok(NULL, " \t\n");
    if (tok == NULL) {
        return false;
    }
    snprintf(speech_path, speech_path_maxlen, "sound\\speech\\slide\\%s", tok);

    return true;
}

/**
 * Performs a fade-out effect for the slide UI.
 *
 * 0x5699F0
 */
void slide_ui_fade_out(void)
{
    FadeData fade_data;

    fade_data.flags = 0;
    fade_data.duration = 2.0f;
    fade_data.steps = 48;
    fade_data.color = tig_color_make(0, 0, 0);
    gfade_run(&fade_data);
}

/**
 * Performs a fade-in effect for the slide UI.
 *
 * 0x569A60
 */
void slide_ui_fade_in(void)
{
    FadeData fade_data;

    fade_data.flags = FADE_IN;
    fade_data.duration = 2.0f;
    fade_data.steps = 48;
    gfade_run(&fade_data);
}

/**
 * Clears the slide queue.
 *
 * 0x569A90
 */
void slide_ui_queue_clear(void)
{
    slide_ui_queue_size = 0;
}

/**
 * Queues a slide for display.
 *
 * 0x569AA0
 */
void slide_ui_enqueue(int slide)
{
    if (slide_ui_queue_size < MAX_QUEUE_SIZE) {
        slide_ui_queue[slide_ui_queue_size++] = slide;
    }
}
