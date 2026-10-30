#include "mss_compat.h"

#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

typedef struct AudioProbe {
    SDL_Mutex* probe_lock;
    MIX_Group* probe_group;
    MssAudioSignal signal;
} AudioProbe;

struct AUDIO {
    MIX_Audio* audio;
    MIX_Track* track;
    AudioProbe probe;
};

struct STREAM {
    MIX_Audio* audio;
    MIX_Track* track;
    int loops;
    AudioProbe probe;
};

static MIX_Mixer* mixer;
static bool probe_enabled;

static void SDLCALL probe_mixed(void* userdata, MIX_Group* group, const SDL_AudioSpec* spec, float* pcm, int samples)
{
    (void)group;
    AudioProbe* audio = userdata;
    if (spec->channels < 2) return;
    SDL_LockMutex(audio->probe_lock);
    for (int i = 0; i + spec->channels <= samples; i += spec->channels) {
        audio->signal.left += (double)pcm[i] * pcm[i];
        audio->signal.right += (double)pcm[i + 1] * pcm[i + 1];
        audio->signal.frames++;
    }
    SDL_UnlockMutex(audio->probe_lock);
}

void mss_compat_probe_enable(bool enabled) { probe_enabled = enabled; }
bool mss_compat_probe_signal(HAUDIO audio, MssAudioSignal* out)
{
    if (!audio || !audio->probe.probe_lock || !out) return false;
    SDL_LockMutex(audio->probe.probe_lock); *out = audio->probe.signal; SDL_UnlockMutex(audio->probe.probe_lock);
    return true;
}

bool mss_compat_probe_stream(HSTREAM stream, MssAudioSignal* out)
{
    if (!stream || !stream->probe.probe_lock || !out) return false;
    SDL_LockMutex(stream->probe.probe_lock); *out = stream->probe.signal; SDL_UnlockMutex(stream->probe.probe_lock);
    return true;
}

static void probe_init(AudioProbe* probe, MIX_Track* track)
{
    if (!probe_enabled) return;
    probe->probe_lock = SDL_CreateMutex(); probe->probe_group = MIX_CreateGroup(mixer);
    if (probe->probe_lock && probe->probe_group) {
        MIX_SetTrackGroup(track, probe->probe_group);
        MIX_SetGroupPostMixCallback(probe->probe_group, probe_mixed, probe);
    } else {
        if (probe->probe_group) MIX_DestroyGroup(probe->probe_group);
        SDL_DestroyMutex(probe->probe_lock); probe->probe_group = NULL; probe->probe_lock = NULL;
    }
}

static void probe_exit(AudioProbe* probe)
{
    if (probe->probe_group) MIX_DestroyGroup(probe->probe_group);
    SDL_DestroyMutex(probe->probe_lock);
}

void AILCALL AIL_close_stream(HSTREAM stream)
{
    MIX_DestroyTrack(stream->track);
    MIX_DestroyAudio(stream->audio);
    probe_exit(&stream->probe);
    free(stream);
}

int AILCALL AIL_digital_handle_reacquire(HDIGDRIVER drvr)
{
    return 1;
}

int AILCALL AIL_digital_handle_release(HDIGDRIVER drvr)
{
    return 1;
}

HSTREAM AILCALL AIL_open_stream(HDIGDRIVER dig, const char* filename, int stream_mem)
{
    HSTREAM stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->track = MIX_CreateTrack(mixer);
    stream->audio = MIX_LoadAudio(mixer, filename, false);
    if (!stream->track || !stream->audio || !MIX_SetTrackAudio(stream->track, stream->audio)) {
        MIX_DestroyTrack(stream->track); MIX_DestroyAudio(stream->audio); free(stream); return NULL;
    }
    probe_init(&stream->probe, stream->track);
    return stream;
}

void AILCALL AIL_quick_handles(HDIGDRIVER* pdig, HMDIDRIVER* pmdi, HDLSDEVICE* pdls)
{
    if (pdig != NULL) {
        *pdig = NULL;
    }

    if (pmdi != NULL) {
        *pmdi = NULL;
    }

    if (pdls != NULL) {
        *pdls = NULL;
    }
}

HAUDIO AILCALL AIL_quick_load_mem(void const* mem, unsigned size)
{
    HAUDIO audio = calloc(1, sizeof(*audio));
    if (audio == NULL) return NULL;
    audio->track = MIX_CreateTrack(mixer);
    audio->audio = MIX_LoadAudio_IO(mixer, SDL_IOFromConstMem(mem, size), true, true);
    if (audio->track == NULL || audio->audio == NULL || !MIX_SetTrackAudio(audio->track, audio->audio)) {
        MIX_DestroyTrack(audio->track); MIX_DestroyAudio(audio->audio); free(audio); return NULL;
    }
    /* A one-track group exposes actual post-pan PCM for both effects and
     * streams without mixing unrelated channels into diagnostic counters. */
    probe_init(&audio->probe, audio->track);
    return audio;
}

int AILCALL AIL_quick_play(HAUDIO audio, unsigned loop_count)
{
    SDL_PropertiesID props;
    bool ret;

    props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loop_count ? (Sint64)loop_count - 1 : -1);
    ret = MIX_PlayTrack(audio->track, props);
    SDL_DestroyProperties(props);

    return ret ? 1 : 0;
}

void AILCALL AIL_quick_set_volume(HAUDIO audio, int volume, int extravol)
{
    extravol = extravol < 0 ? 0 : extravol > 127 ? 127 : extravol;
    MIX_StereoGains gains = { .left = extravol <= 64 ? 1.0f : (127 - extravol) / 63.0f,
        .right = extravol >= 64 ? 1.0f : extravol / 64.0f };
    MIX_SetTrackStereo(audio->track, &gains);
    MIX_SetTrackGain(audio->track, (float)volume / 128.0f);
}

void AILCALL AIL_quick_shutdown(void)
{
    MIX_DestroyMixer(mixer);
}

int AILCALL AIL_quick_startup(int use_digital, int use_MIDI, unsigned output_rate, int output_bits, int output_channels)
{
    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (mixer == NULL) {
        return 0;
    }

    return 1;
}

int AILCALL AIL_quick_status(HAUDIO audio)
{
    if (MIX_TrackPlaying(audio->track)) {
        return QSTAT_PLAYING;
    } else if (MIX_GetTrackRemaining(audio->track) == 0) {
        return QSTAT_DONE;
    } else {
        return 0;
    }
}

void AILCALL AIL_quick_unload(HAUDIO audio)
{
    MIX_DestroyTrack(audio->track);
    MIX_DestroyAudio(audio->audio);
    probe_exit(&audio->probe);
    free(audio);
}

void AILCALL AIL_set_stream_loop_count(HSTREAM stream, int count)
{
    stream->loops = count;
}

void AILCALL AIL_set_stream_volume(HSTREAM stream, int volume)
{
    MIX_SetTrackGain(stream->track, (float)volume / 128.0f);
}

void AILCALL AIL_start_stream(HSTREAM stream)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, stream->loops - 1);
    MIX_PlayTrack(stream->track, props);
    SDL_DestroyProperties(props);
}

int AILCALL AIL_stream_status(HSTREAM stream)
{
    if (MIX_TrackPlaying(stream->track)) {
        return SMP_PLAYING;
    } else {
        return 0;
    }
}

bool mss_compat_init(void)
{
    if (!MIX_Init()) {
        return false;
    }

    return true;
}

void mss_compat_exit(void)
{
    MIX_Quit();
}
