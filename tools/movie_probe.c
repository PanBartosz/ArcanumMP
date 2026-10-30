#include "media/movie.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
static uint64_t energy;
static void audio(void* context, const int16_t* samples, size_t count)
{
    (void)context;
    for (size_t i = 0; i < count * 2; i++) { int64_t value = samples[i]; energy += (uint64_t)(value * value); }
}
int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    unsigned track = 0; int first = 1;
    if (argc > 2 && sscanf(argv[1], "--track=%u", &track) == 1) first = 2;
    for (int i = first; i < argc; i++) {
        ArcMovie* m = arc_movie_open(argv[i], track);
        if (!m) { fprintf(stderr, "Could not open %s\n", argv[i]); return 1; }
        energy = 0; arc_movie_audio(m, audio, NULL);
        int next; uint32_t hash = UINT32_C(2166136261);
        while ((next = arc_movie_next(m)) == 1) {
            const ArcMovieInfo* info = arc_movie_info(m); const uint8_t* pixels = arc_movie_pixels(m);
            size_t size = (size_t)info->width * info->height * 4;
            for (size_t at = 0; at < size; at += size / 251 + 1) hash = (hash ^ pixels[at]) * UINT32_C(16777619);
        }
        const ArcMovieInfo* info = arc_movie_info(m);
        bool good = !next && info->decoded_frames > 0 && (!info->frames || info->decoded_frames == info->frames)
            && (!info->tracks || (info->audio_samples && energy));
        printf("{\"file\":\"%u\",\"width\":%u,\"height\":%u,\"frames\":%u,\"tracks\":%u,\"samples\":%" PRIu64 ",\"energy\":%" PRIu64 ",\"video_digest\":%u,\"verified\":%s}\n",
            (unsigned)i, info->width, info->height, info->decoded_frames, info->tracks, info->audio_samples, energy, hash, good ? "true" : "false");
        if (!good) fprintf(stderr, "Decode failed: %s\n", arc_movie_error(m));
        arc_movie_close(m); if (!good) return 1;
    }
    return 0;
}
