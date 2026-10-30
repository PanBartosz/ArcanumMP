/* Capture-session frame pump, loaded by GDB into a disposable native process.
 * Calls the game's normal event/render functions. Never linked into releases. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

static void (*ping)(void), (*game)(void), (*redraw)(void), (*display)(void);
static int (*dequeue)(void*);
static void (*process)(void*);
static void (*mouse)(int,int);
static void* network_session;
static void (*poll_session)(void*,uint64_t), (*tick_runtime)(uint64_t);
static uint64_t (*ticks)(void);
static int record_fd=-1,record_width,record_height,record_pitch;
static unsigned char* record_pixels;
static unsigned long record_frames;
static double record_started;
static char record_report[1024];
static double clock_seconds(void);

int showcase_record_open(const char* fifo,const char* report,void* pixels,int width,int height,int pitch)
{
    record_fd=open(fifo,O_WRONLY);
    if(record_fd<0)return -1;
    record_pixels=pixels;record_width=width;record_height=height;record_pitch=pitch;
    record_frames=0;record_started=0;snprintf(record_report,sizeof(record_report),"%s",report);
    return 0;
}
static void record_frame(void)
{
    if(record_fd<0)return;
    if(!record_started)record_started=clock_seconds();
    for(int y=0;y<record_height;y++){
        unsigned char* p=record_pixels+(size_t)y*record_pitch;
        size_t remaining=(size_t)record_width*4;
        while(remaining){ssize_t n=write(record_fd,p,remaining);
            if(n<0&&errno==EINTR)continue;
            if(n<=0){close(record_fd);record_fd=-1;return;}
            remaining-=(size_t)n;p+=n;
        }
    }
    record_frames++;
}
void showcase_record_close(void)
{
    if(record_fd<0)return;
    close(record_fd);record_fd=-1;
    FILE* report=fopen(record_report,"w");
    if(report){fprintf(report,"{\"frames\":%lu,\"started_monotonic\":%.9f,\"fps\":30,\"backend\":\"completed native frames\"}\n",record_frames,record_started);fclose(report);}
}

void showcase_network(void* session,void* poll,void* tick,void* clock)
{ network_session=session;poll_session=poll;tick_runtime=tick;ticks=clock; }

void showcase_hide_npcs(void* first_fn,void* next_fn,void* get_fn,void* flags_fn,
    int type_field,int npc_type)
{
    int (*first)(int64_t*,int*)=first_fn;
    int (*next)(int64_t*,int*)=next_fn;
    int (*get)(int64_t,int)=get_fn;
    void (*set_flags)(int64_t,unsigned)=flags_fn;
    int64_t object;int iterator;
    for (int more=first(&object,&iterator);more;more=next(&object,&iterator))
        if (get(object,type_field)==npc_type) set_flags(object,2u);
}

void showcase_init(void* a,void* b,void* c,void* d,void* e,void* f,void* g)
{
    ping=a; game=b; redraw=c; display=d; dequeue=e; process=f; mouse=g;
}
static double clock_seconds(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return now.tv_sec+now.tv_nsec/1e9;
}
static void frame(void)
{
    union { uint64_t alignment; unsigned char bytes[1024]; } message;
    if (network_session) { uint64_t now=ticks();poll_session(network_session,now);tick_runtime(now); }
    ping(); game(); redraw(); display();record_frame();
    while (dequeue(&message)==0) process(&message);
}
/* Film a continuous native pixel drag in one call: no debugger stops or
 * whole-tile camera changes between frames. The output is one frame per draw. */
void showcase_pan(void* scroll_fn,int dx,int dy,int milliseconds)
{
    void (*scroll)(int,int)=scroll_fn;
    int frames=milliseconds*30/1000,old_x=0,old_y=0;
    double start=clock_seconds();
    for(int n=1;n<=frames;n++){
        int x=dx*n/frames,y=dy*n/frames;
        scroll(x-old_x,y-old_y);old_x=x;old_y=y;frame();
        double remaining=start+n/30.0-clock_seconds();
        if(remaining>0){struct timespec delay={0,(long)(remaining*1e9)};nanosleep(&delay,0);}
    }
}
void showcase_pump(int milliseconds)
{
    double start=clock_seconds(), next=start;
    while (clock_seconds()-start < milliseconds/1000.0) {
        frame(); next+=1.0/30.0;
        double remaining=next-clock_seconds();
        if (remaining>0) { struct timespec delay={0,(long)(remaining*1e9)}; nanosleep(&delay,0); }
    }
}
/* Schedule ordinary owner inputs inside the uninterrupted frame loop. Debugger
 * stops between actions otherwise produce visible animation skips on film. */
typedef struct ShowcaseCue { int frame,kind; int64_t target; int spell; void* descriptor; } ShowcaseCue;
void showcase_sequence(void* move_fn,void* attack_fn,void* cast_fn,int64_t hero,
    const ShowcaseCue* cues,int count,int milliseconds,void* get_fn,int location_field,void* target_fn)
{
    bool (*move_to)(int64_t,bool)=move_fn;
    bool (*attack)(int64_t,bool)=attack_fn;
    bool (*cast)(int64_t,int,const void*,bool)=cast_fn;
    int64_t (*get_location)(int64_t,int)=get_fn;
    void (*set_target)(void*,int64_t)=target_fn;
    int frames=milliseconds*30/1000;
    double start=clock_seconds();
    for(int n=0;n<frames;n++) {
        for(int c=0;c<count;c++) if(cues[c].frame==n) {
            if(cues[c].kind==0)move_to(cues[c].target,1);
            else if(cues[c].kind==1)attack(cues[c].target,1);
            else if(cues[c].kind==2){
                if(cues[c].target)set_target(cues[c].descriptor,get_location(cues[c].target,location_field));
                cast(hero,cues[c].spell,cues[c].descriptor,0);
            }
        }
        frame();
        double remaining=start+(n+1)/30.0-clock_seconds();
        if(remaining>0){struct timespec delay={0,(long)(remaining*1e9)};nanosleep(&delay,0);}
    }
}
void showcase_drag(void* msg,int* event,int* x,int* y,void* callback,
    int x0,int y0,int x1,int y1,int down,int move,int up,int milliseconds,void* button_fn)
{
    int (*filter)(void*)=callback;
    void (*button)(int,bool)=button_fn;
    button(0,true);
    *event=down; *x=x0; *y=y0; mouse(x0,y0); filter(msg); showcase_pump(200);
    int steps=milliseconds*30/1000;
    for (int i=1;i<=steps;i++) {
        *event=move; *x=x0+(x1-x0)*i/steps; *y=y0+(y1-y0)*i/steps;
        mouse(*x,*y); filter(msg); showcase_pump(33);
    }
    button(0,false); *event=up; filter(msg); showcase_pump(400);
}
