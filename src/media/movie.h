#ifndef ARCMP_MEDIA_MOVIE_H
#define ARCMP_MEDIA_MOVIE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct ArcMovie ArcMovie;
typedef void (*ArcMovieAudio)(void* context, const int16_t* stereo, size_t samples);
typedef struct ArcMovieInfo {
    unsigned width, height, frames, tracks, decoded_frames;
    uint64_t audio_samples, position_ms, duration_ms;
    unsigned fps_num, fps_den;
} ArcMovieInfo;
/* Local-file Bink only. Track is the original Bink audio track ID. Audio is
 * converted to interleaved stereo S16 at 48000 Hz. Pixels are native XRGB8888. */
ArcMovie* arc_movie_open(const char* path, unsigned track);
void arc_movie_close(ArcMovie* movie);
void arc_movie_audio(ArcMovie* movie, ArcMovieAudio audio, void* context);
/* 1: new video frame, 0: drained EOF, -1: decode error. */
int arc_movie_next(ArcMovie* movie);
const uint8_t* arc_movie_pixels(const ArcMovie* movie);
const ArcMovieInfo* arc_movie_info(const ArcMovie* movie);
const char* arc_movie_error(const ArcMovie* movie);
#endif
