/* Optional native timing helper, loaded only in isolated benchmark processes.
 * Function pointers come from GDB; this is never linked into the game. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static double milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
static int compare(const void* a, const void* b)
{
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}
void qol_pattern(void* pixels, int width, int height, int pitch)
{
    for (int y = 0; y < height; y++) {
        uint32_t* row = (uint32_t*)((unsigned char*)pixels + y*pitch);
        for (int x = 0; x < width; x++) row[x] = ((x*2u) << 16) | ((y*2u) << 8) | (x^y);
    }
}
void qol_duplicates(void* pose_fn, void* ping_fn, void* ticks_fn, void* delay_fn,
    int64_t object, int64_t location, int x, int y, uint32_t aid)
{
    void (*pose)(int64_t,int64_t,int,int,uint32_t) = pose_fn;
    void (*ping)(unsigned) = ping_fn;
    uint64_t (*ticks)(void) = ticks_fn;
    void (*delay)(uint32_t) = delay_fn;
    pose(object,location,x,y,aid); delay(50);
    pose(object,location,x+200,y,aid);
    for (unsigned i=0; i<6; i++) {
        delay(10); ping((unsigned)ticks()); pose(object,location,x+200,y,aid);
    }
    ping((unsigned)ticks());
}
int qol_render(void* draw_fn, void* display_fn, void* invalidate_fn,
    void* scroll_fn, int mode, int frames, const char* output)
{
    void (*draw)(void) = draw_fn, (*display)(void) = display_fn;
    void (*invalidate)(void*) = invalidate_fn;
    void (*scroll)(int64_t, int64_t) = scroll_fn;
    if (frames < 20 || frames > 10000) return -1;
    double* samples = calloc((size_t)frames, sizeof(double));
    if (!samples) return -1;
    double drawing = 0, presenting = 0;
    /* Warm the art, sectors, fonts and compositor before timing. */
    for (int i = -20; i < frames; i++) {
        if (mode == 1) invalidate(NULL);
        if (mode == 2) { scroll((i / 30) % 2 ? 4 : -4, 0); invalidate(NULL); }
        double a = milliseconds();
        draw();
        double b = milliseconds();
        display();
        double c = milliseconds();
        if (i >= 0) { samples[i] = c-a; drawing += b-a; presenting += c-b; }
    }
    qsort(samples, (size_t)frames, sizeof(double), compare);
    FILE* f = fopen(output, "w");
    if (f) {
        fprintf(f, "{\"frames\":%d,\"median_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f,"
            "\"draw_mean_ms\":%.6f,\"present_mean_ms\":%.6f}\n", frames,
            samples[frames/2], samples[frames*95/100], samples[frames*99/100],
            drawing/frames, presenting/frames);
        fclose(f);
    }
    free(samples);
    return f ? 0 : -1;
}
