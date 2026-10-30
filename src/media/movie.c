#include "media/movie.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdlib.h>
#include <string.h>
struct ArcMovie {
    AVFormatContext* format;
    AVCodecContext *video, *sound;
    AVFrame *frame, *audio_frame;
    AVPacket* packet;
    struct SwsContext* scaler;
    SwrContext* resampler;
    int video_stream, audio_stream;
    uint8_t *pixels, *pcm;
    unsigned pcm_size;
    bool draining, failed;
    char error[128];
    ArcMovieInfo info;
    ArcMovieAudio audio; void* context;
};
static int failed(ArcMovie* m, int error)
{
    m->failed = true; av_strerror(error, m->error, sizeof(m->error)); return -1;
}
static AVCodecContext* codec(AVStream* stream)
{
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* c = decoder ? avcodec_alloc_context3(decoder) : NULL;
    if (!c) return NULL;
    c->thread_count = 1;
    if (avcodec_parameters_to_context(c, stream->codecpar) < 0 || avcodec_open2(c, decoder, NULL) < 0) avcodec_free_context(&c);
    return c;
}
ArcMovie* arc_movie_open(const char* path, unsigned track)
{
    if (!path || !*path || track > 65535) return NULL;
    ArcMovie* m = calloc(1, sizeof(*m)); if (!m) return NULL;
    m->audio_stream = -1;
    /* Explicitly selecting the sole enabled demuxer avoids probing unrelated
     * protocols, playlists or external resources. */
    const AVInputFormat* bink = av_find_input_format("bink");
    if (!bink || avformat_open_input(&m->format, path, bink, NULL) < 0 || avformat_find_stream_info(m->format, NULL) < 0) goto bad;
    m->video_stream = av_find_best_stream(m->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (m->video_stream < 0 || m->format->streams[m->video_stream]->codecpar->codec_id != AV_CODEC_ID_BINKVIDEO) goto bad;
    m->video = codec(m->format->streams[m->video_stream]);
    if (!m->video || m->video->width <= 0 || m->video->height <= 0 || m->video->width > 4096 || m->video->height > 4096) goto bad;
    for (unsigned i = 0; i < m->format->nb_streams; i++) if (m->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        m->info.tracks++;
        if ((unsigned)m->format->streams[i]->id == track) m->audio_stream = (int)i;
    }
    if (m->info.tracks && m->audio_stream < 0) {
        if (track) goto bad;
        m->audio_stream = av_find_best_stream(m->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    } else if (!m->info.tracks && track) goto bad;
    if (m->audio_stream >= 0) {
        m->sound = codec(m->format->streams[m->audio_stream]);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (!m->sound || swr_alloc_set_opts2(&m->resampler, &stereo, AV_SAMPLE_FMT_S16, 48000,
            &m->sound->ch_layout, m->sound->sample_fmt, m->sound->sample_rate, 0, NULL) < 0 || swr_init(m->resampler) < 0) goto bad;
    }
    m->frame = av_frame_alloc(); m->audio_frame = av_frame_alloc(); m->packet = av_packet_alloc();
    m->info.width = (unsigned)m->video->width; m->info.height = (unsigned)m->video->height;
    AVRational rate = av_guess_frame_rate(m->format, m->format->streams[m->video_stream], NULL);
    if (rate.num <= 0 || rate.den <= 0 || av_q2d(rate) > 240) goto bad;
    m->info.fps_num = (unsigned)rate.num; m->info.fps_den = (unsigned)rate.den;
    int64_t frames = m->format->streams[m->video_stream]->nb_frames;
    if (!frames && m->format->streams[m->video_stream]->duration > 0) frames = av_rescale_q(m->format->streams[m->video_stream]->duration,
        m->format->streams[m->video_stream]->time_base, av_inv_q(rate));
    if (frames > 0 && frames <= 10000000) m->info.frames = (unsigned)frames;
    if (m->format->duration > 0) m->info.duration_ms = (uint64_t)(m->format->duration / (AV_TIME_BASE / 1000));
    m->pixels = av_malloc((size_t)m->info.width * m->info.height * 4);
    if (!m->frame || !m->audio_frame || !m->packet || !m->pixels) goto bad;
    return m;
bad:
    arc_movie_close(m); return NULL;
}
void arc_movie_close(ArcMovie* m)
{
    if (!m) return;
    sws_freeContext(m->scaler); swr_free(&m->resampler);
    av_frame_free(&m->frame); av_frame_free(&m->audio_frame); av_packet_free(&m->packet);
    avcodec_free_context(&m->video); avcodec_free_context(&m->sound); avformat_close_input(&m->format);
    av_free(m->pixels); av_free(m->pcm); free(m);
}
void arc_movie_audio(ArcMovie* m, ArcMovieAudio audio, void* context) { if (m) { m->audio = audio; m->context = context; } }
static bool audio(ArcMovie* m, const AVPacket* packet)
{
    int result = avcodec_send_packet(m->sound, packet);
    if (result < 0 && result != AVERROR_EOF) return failed(m, result), false;
    while ((result = avcodec_receive_frame(m->sound, m->audio_frame)) >= 0) {
        int capacity = swr_get_out_samples(m->resampler, m->audio_frame->nb_samples);
        if (capacity <= 0 || capacity > 1048576) return failed(m, AVERROR_INVALIDDATA), false;
        av_fast_malloc(&m->pcm, &m->pcm_size, (size_t)capacity * 4);
        if (!m->pcm) return failed(m, AVERROR(ENOMEM)), false;
        uint8_t* out[] = { m->pcm };
        int samples = swr_convert(m->resampler, out, capacity, (const uint8_t**)m->audio_frame->extended_data, m->audio_frame->nb_samples);
        if (samples < 0) return failed(m, samples), false;
        m->info.audio_samples += (uint64_t)samples;
        if (samples && m->audio) m->audio(m->context, (const int16_t*)m->pcm, (size_t)samples);
        av_frame_unref(m->audio_frame);
    }
    if (!packet && result == AVERROR_EOF) {
        int capacity = swr_get_out_samples(m->resampler, 0);
        if (capacity > 0 && capacity <= 1048576) {
            av_fast_malloc(&m->pcm, &m->pcm_size, (size_t)capacity * 4);
            if (!m->pcm) return failed(m, AVERROR(ENOMEM)), false;
            uint8_t* out[] = { m->pcm };
            int samples = swr_convert(m->resampler, out, capacity, NULL, 0);
            if (samples < 0) return failed(m, samples), false;
            m->info.audio_samples += (uint64_t)samples;
            if (samples && m->audio) m->audio(m->context, (const int16_t*)m->pcm, (size_t)samples);
        }
    }
    return result == AVERROR(EAGAIN) || result == AVERROR_EOF || (failed(m, result), false);
}
int arc_movie_next(ArcMovie* m)
{
    if (!m || m->failed) return -1;
    for (;;) {
        int result = avcodec_receive_frame(m->video, m->frame);
        if (result >= 0) {
            if (m->frame->width != (int)m->info.width || m->frame->height != (int)m->info.height) return failed(m, AVERROR_INVALIDDATA);
            m->scaler = sws_getCachedContext(m->scaler, m->frame->width, m->frame->height, m->frame->format,
                m->frame->width, m->frame->height, AV_PIX_FMT_BGRA, SWS_BILINEAR | SWS_BITEXACT, NULL, NULL, NULL);
            uint8_t* output[] = { m->pixels }; int lines[] = { (int)m->info.width * 4 };
            if (!m->scaler || sws_scale(m->scaler, (const uint8_t* const*)m->frame->data, m->frame->linesize, 0, m->frame->height, output, lines) != m->frame->height) return failed(m, AVERROR_INVALIDDATA);
            if (m->frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                int64_t ms = av_rescale_q(m->frame->best_effort_timestamp, m->format->streams[m->video_stream]->time_base, (AVRational) { 1, 1000 });
                m->info.position_ms = ms < 0 ? 0 : (uint64_t)ms;
            } else m->info.position_ms = (uint64_t)m->info.decoded_frames * 1000 * m->info.fps_den / m->info.fps_num;
            m->info.decoded_frames++; av_frame_unref(m->frame); return 1;
        }
        if (result == AVERROR_EOF) return 0;
        if (result != AVERROR(EAGAIN)) return failed(m, result);
        if (m->draining) return 0;
        result = av_read_frame(m->format, m->packet);
        if (result < 0) {
            if (result != AVERROR_EOF) return failed(m, result);
            m->draining = true;
            if (m->sound && !audio(m, NULL)) return -1;
            result = avcodec_send_packet(m->video, NULL);
        } else if (m->packet->stream_index == m->video_stream) result = avcodec_send_packet(m->video, m->packet);
        else if (m->packet->stream_index == m->audio_stream && !audio(m, m->packet)) result = AVERROR_INVALIDDATA;
        else result = 0;
        av_packet_unref(m->packet);
        if (result < 0 && result != AVERROR_EOF) return failed(m, result);
    }
}
const uint8_t* arc_movie_pixels(const ArcMovie* m) { return m ? m->pixels : NULL; }
const ArcMovieInfo* arc_movie_info(const ArcMovie* m) { return m ? &m->info : NULL; }
const char* arc_movie_error(const ArcMovie* m) { return m ? m->error : "Movie unavailable"; }
