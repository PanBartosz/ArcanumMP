#include "tig/movie.h"
#include "tig/color.h"
#include "tig/core.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <string.h>
static int screen_width, screen_height;
static bool playing, skip_input, quit_input, ignore_input, audio_failed;
static TigMovieCallbacks callbacks;
static ArcMovieInfo progress;
static bool probe_mode;
void tig_movie_probe_mode(bool enabled) { probe_mode = enabled; }
void tig_movie_callbacks(const TigMovieCallbacks* value) { callbacks = value ? *value : (TigMovieCallbacks) { 0 }; }
int tig_movie_pulse(void) { return callbacks.pump ? callbacks.pump(callbacks.context) : 0; }
void tig_movie_progress(ArcMovieInfo* info) { if (info) *info = progress; }
bool tig_movie_probe(const char* path, unsigned track)
{
    ArcMovie* movie = arc_movie_open(path, track);
    bool good = movie && arc_movie_next(movie) == 1;
    arc_movie_close(movie); return good;
}
int tig_movie_init(TigInitInfo* info) { screen_width = info->width; screen_height = info->height; return TIG_OK; }
void tig_movie_exit(void) { }
bool tig_movie_is_playing(void) { return playing; }
static bool input(TigMessage* message)
{
    if (message->type == TIG_MESSAGE_QUIT) { quit_input = true; return true; }
    if (message->type == TIG_MESSAGE_KEYBOARD) { if (message->data.keyboard.pressed && !ignore_input) skip_input = true; return true; }
    if (message->type == TIG_MESSAGE_MOUSE) {
        if (!ignore_input && (message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP || message->data.mouse.event == TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP)) skip_input = true;
        return true;
    }
    return message->type == TIG_MESSAGE_TEXT_INPUT;
}
static int pulse(void)
{
    tig_ping(); TigMessage message;
    while (tig_message_dequeue(&message) == TIG_OK) input(&message);
    if (quit_input || audio_failed) return -1;
    if (skip_input) {
        skip_input = false;
        if (callbacks.skip) callbacks.skip(callbacks.context); else return 1;
    }
    return tig_movie_pulse();
}
static void sound(void* context, const int16_t* samples, size_t count)
{
    SDL_AudioStream* stream = context;
    if (count > INT_MAX / 4 || !SDL_PutAudioStreamData(stream, samples, (int)count * 4)) audio_failed = true;
}
static bool frame(tig_window_handle_t window, TigVideoBuffer* buffer, ArcMovie* movie)
{
    const ArcMovieInfo* info = arc_movie_info(movie); TigVideoBufferData data;
    if (tig_video_buffer_lock(buffer) != TIG_OK) return false;
    if (tig_video_buffer_data(buffer, &data) != TIG_OK || data.bpp != 32) { tig_video_buffer_unlock(buffer); return false; }
    for (unsigned y = 0; y < info->height; y++) memcpy((uint8_t*)data.pixels + (size_t)y * data.pitch,
        arc_movie_pixels(movie) + (size_t)y * info->width * 4, (size_t)info->width * 4);
    tig_video_buffer_unlock(buffer);
    TigRect source = { 0, 0, (int)info->width, (int)info->height };
    double scale = (double)screen_width / info->width;
    if ((double)screen_height / info->height < scale) scale = (double)screen_height / info->height;
    TigRect destination = { 0, 0, (int)(info->width * scale), (int)(info->height * scale) };
    destination.x = (screen_width - destination.width) / 2; destination.y = (screen_height - destination.height) / 2;
    tig_window_fill(window, NULL, 0);
    TigWindowBlitInfo blit = { .type = TIG_WINDOW_BLIT_VIDEO_BUFFER_TO_WINDOW, .vb_blit_flags = TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR,
        .src_rect = &source, .src_video_buffer = buffer, .dst_rect = &destination, .dst_window_handle = window };
    if (tig_window_blit(&blit) != TIG_OK) return false;
    const char* status = callbacks.status ? callbacks.status(callbacks.context) : NULL;
    if (status && *status) {
        TigRect bar = { 0, screen_height - 34, screen_width, 34 }, label = { 12, screen_height - 28, screen_width - 24, 24 };
        tig_window_fill(window, &bar, 0); tig_window_text_write(window, status, &label);
    }
    progress = *info;
    if (probe_mode && info->decoded_frames >= 3 && !ignore_input) skip_input = true;
    tig_window_display(); return true;
}
int tig_movie_play(const char* path, TigMovieFlags flags, int track)
{
    if (!path || track < 0 || playing) return TIG_ERR_INVALID_PARAM;
    ArcMovie* movie = arc_movie_open(path, (unsigned)track); if (!movie) return TIG_ERR_IO;
    TigVideoBuffer* buffer = NULL; tig_window_handle_t window = TIG_WINDOW_HANDLE_INVALID;
    SDL_AudioStream* stream = NULL; int status = TIG_OK;
    const ArcMovieInfo* info = arc_movie_info(movie);
    TigVideoBufferCreateInfo create = { .flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY, .width = (int)info->width, .height = (int)info->height };
    TigWindowData window_data = { .flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER,
        .rect = { 0, 0, screen_width, screen_height }, .message_filter = input };
    playing = true; ignore_input = (flags & TIG_MOVIE_IGNORE_KEYBOARD) != 0; skip_input = quit_input = audio_failed = false;
    memset(&progress, 0, sizeof(progress));
    if (tig_video_buffer_create(&create, &buffer) != TIG_OK || tig_window_create(&window_data, &window) != TIG_OK) { status = TIG_ERR_GENERIC; goto end; }
    if (info->tracks) {
        SDL_AudioSpec spec = { .format = SDL_AUDIO_S16, .channels = 2, .freq = 48000 };
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (!stream) { status = TIG_ERR_GENERIC; goto end; }
        arc_movie_audio(movie, sound, stream);
    }
    tig_video_fade(0, 0, 0.0f, 1);
    int decoded = arc_movie_next(movie);
    if (decoded != 1 || !frame(window, buffer, movie)) { status = TIG_ERR_IO; goto end; }
    uint64_t started = SDL_GetTicks();
    if (stream) SDL_ResumeAudioStreamDevice(stream);
    if (flags & TIG_MOVIE_FADE_IN) tig_video_fade(0, 32, 0.3f, 1);
    for (;;) {
        int control = pulse();
        if (control) { if (control < 0) status = TIG_ERR_GENERIC; break; }
        if (SDL_GetTicks() - started < info->position_ms) { SDL_Delay(1); continue; }
        if (!frame(window, buffer, movie)) { status = TIG_ERR_GENERIC; break; }
        decoded = arc_movie_next(movie);
        if (decoded <= 0) { if (decoded < 0) status = TIG_ERR_IO; break; }
    }
    if (!decoded && stream) {
        uint64_t until = SDL_GetTicks() + 5000;
        while (SDL_GetAudioStreamQueued(stream) > 0 && SDL_GetTicks() < until) {
            if (pulse()) break;
            SDL_Delay(2);
        }
    }
    progress = *info;
end:
    if (stream) SDL_DestroyAudioStream(stream);
    if (buffer) tig_video_buffer_destroy(buffer);
    if (window != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(window);
    arc_movie_close(movie); playing = false;
    tig_window_invalidate_rect(NULL);
    if (flags & TIG_MOVIE_FADE_OUT) tig_video_fade(0, 32, 0.3f, 0);
    if (!(flags & TIG_MOVIE_NO_FINAL_FLIP)) { tig_video_fill(NULL, tig_color_make(0, 0, 0)); tig_video_flip(); }
    return status;
}
