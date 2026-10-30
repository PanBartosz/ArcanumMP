#ifndef TIG_MOVIE_H_
#define TIG_MOVIE_H_

#include "tig/types.h"
#include "media/movie.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int TigMovieFlags;

#define TIG_MOVIE_IGNORE_KEYBOARD 0x01u
#define TIG_MOVIE_FADE_IN 0x02u
#define TIG_MOVIE_FADE_OUT 0x04u
#define TIG_MOVIE_NO_FINAL_FLIP 0x08u
typedef struct TigMovieCallbacks {
    /* 0 continues, 1 is a synchronized skip, -1 is a connection failure. */
    int (*pump)(void* context);
    void (*skip)(void* context);
    const char* (*status)(void* context);
    void* context;
} TigMovieCallbacks;
void tig_movie_callbacks(const TigMovieCallbacks* callbacks);
int tig_movie_pulse(void);
void tig_movie_progress(ArcMovieInfo* info);
/* Readiness checks decode an actual first frame without displaying it. */
bool tig_movie_probe(const char* path, unsigned track);
/* Explicit engine-fixture opt-in: request a skip after three decoded frames. */
void tig_movie_probe_mode(bool enabled);

// Initializes movie system.
int tig_movie_init(TigInitInfo* init_info);

// Shutdowns movie system.
void tig_movie_exit(void);

// Play given movie file.
int tig_movie_play(const char* path, TigMovieFlags movie_flags, int sound_track);

// Returns `true` if movie is currently being played.
bool tig_movie_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* TIG_MOVIE_H_ */
