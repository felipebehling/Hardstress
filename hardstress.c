// hardstress.c

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <stdarg.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#pragma comment(lib, "pdh.lib")
#else
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <gtk/gtk.h>
#include <cairo.h>

/* ---------------- Config ---------------- */
#define HISTORY_SAMPLES 240 /* 240 samples -> 2 min @ 500ms */

typedef struct AppContext AppContext;

typedef struct { int tid; size_t buf_bytes; uint8_t *buf; uint32_t *idx; size_t idx_len; atomic_int running; atomic_uint iters; int pinned; AppContext *ctx; } worker_t;

struct AppContext {
    int threads;
    size_t mem_mib_per_thread;
    int duration_sec;
    int pin_affinity;
    atomic_int running;
    atomic_int errors;
    atomic_uint total_iters;
    int cpu_count;
    double *cpu_usage;
    GMutex cpu_mutex;
    unsigned **thread_iters_history;
    int history_pos;
    int history_len;
    GMutex history_mutex;
    double temp_celsius;
    GMutex temp_mutex;
    worker_t *workers;
#ifdef _WIN32
    HANDLE *worker_threads;
#else
    pthread_t *worker_threads;
#endif
    GtkWidget *entry_threads, *entry_mem, *entry_dur, *check_pin;
    GtkWidget *btn_start, *btn_stop, *btn_export_csv;
    GtkTextBuffer *log_buffer;
    GtkWidget *cpu_drawing;
    GtkWidget *iters_drawing;
    GtkWidget *status_label;
};

/* Utility */
static double now_sec(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec*1e-9; }

/* Logger */
static void gui_append_log(AppContext *ctx, const char *fmt, ...){ va_list ap; va_start(ap, fmt); char *msg = NULL; vasprintf(&msg, fmt, ap); va_end(ap); if (!msg) return; GtkTextIter end; gtk_text_buffer_get_end_iter(ctx->log_buffer, &end); gtk_text_buffer_insert(ctx->log_buffer, &end, msg, -1); g_free(msg); }

/* Stress kernels etc. (omiti para brevidade - mantém igual) */
/* ... */

/* compute_usage corrigido */
static double compute_usage(const cpu_sample_t *a,const cpu_sample_t *b){
    unsigned long long idle_a=a->idle+a->iowait;
    unsigned long long idle_b=b->idle+b->iowait;
    unsigned long long nonidle_a=a->user+a->nice+a->system+a->irq+a->softirq+a->steal;
    unsigned long long nonidle_b=b->user+b->nice+b->system+b->irq+b->softirq+b->steal; // FIX aqui
    unsigned long long total_a=idle_a+nonidle_a;
    unsigned long long total_b=idle_b+nonidle_b;
    unsigned long long totald = total_b - total_a;
    unsigned long long idled = idle_b - idle_a;
    if(totald==0) return 0.0;
    double perc = (double)(totald - idled) / (double)totald;
    if(perc<0) perc=0; if(perc>1) perc=1; return perc;
}

/* worker_main com gerenciamento de memória seguro */
static void worker_main(void *arg){
    worker_t *w = (worker_t*)arg;
    AppContext *ctx = w->ctx;
    size_t floats = w->buf_bytes/sizeof(float);
    w->buf = malloc(w->buf_bytes);
    if(!w->buf){ gui_append_log(ctx, "[T%d] buffer alloc failed\n", w->tid); atomic_fetch_add(&ctx->errors,1); return; }
    /* resto igual ... */
}

/* duration_sec implementado */
static gboolean auto_stop_cb(gpointer data){
    AppContext *ctx = (AppContext*)data;
    if(atomic_load(&ctx->running)){
        gui_append_log(ctx, "[GUI] Auto-stop after %d sec\n", ctx->duration_sec);
        gtk_button_clicked(GTK_BUTTON(ctx->btn_stop));
    }
    return G_SOURCE_REMOVE;
}

/* Gráfico de iterações reintroduzido */
static gboolean on_draw_iters(GtkWidget *w, cairo_t *cr, gpointer user_data){
    AppContext *ctx = (AppContext*)user_data;
    GtkAllocation a; gtk_widget_get_allocation(w, &a);
    int W=a.width, H=a.height;
    int tcount = ctx->threads>0?ctx->threads:1;
    int margin=4;
    int area_h=(H - (tcount+1)*margin)/tcount;
    g_mutex_lock(&ctx->history_mutex);
    for(int t=0;t<tcount;t++){
        int y0 = margin + t*(area_h+margin);
        cairo_set_source_rgb(cr,0.06,0.06,0.06);
        cairo_rectangle(cr,0,y0,W,area_h); cairo_fill(cr);
        cairo_set_source_rgb(cr,0.8,0.4,0.1); cairo_set_line_width(cr,1.0);
        int samples = ctx->history_len;
        int step = (samples>1)? (W/(samples-1)) : W;
        int x=0; int idx=(ctx->history_pos+1)%samples;
        unsigned prev = ctx->thread_iters_history ? ctx->thread_iters_history[t][idx] : 0;
        cairo_move_to(cr,0,y0+area_h-(double)prev/1000.0*area_h);
        for(int s=0;s<samples;s++){
            unsigned v = ctx->thread_iters_history ? ctx->thread_iters_history[t][idx] : 0;
            double y = y0+area_h-(double)v/1000.0*area_h;
            if(y<y0) y=y0; if(y>y0+area_h) y=y0+area_h;
            cairo_line_to(cr,x,y);
            x+=step; idx=(idx+1)%samples;
        }
        cairo_stroke(cr);
        cairo_set_source_rgb(cr,1,1,1);
        cairo_move_to(cr,6,y0+12);
        char lbl[64]; snprintf(lbl,sizeof(lbl),"T%02d iters (×1000 scale)",t); cairo_show_text(cr,lbl);
    }
    g_mutex_unlock(&ctx->history_mutex);
    return FALSE;
}

/* README.md sugestão (criar no GitHub):
# HardStress GUI

Stress-test em C com interface GTK3 + gráficos em tempo real.

## Compilar Linux
```bash
sudo apt install build-essential libgtk-3-dev
make
```

## Compilar Windows (MSYS2)
```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3
make
```

## Funcionalidades
- Stress multi-thread (FPU, inteiros, memória).
- Gráficos CPU e iterações.
- Export CSV.
- Monitor de temperatura (Linux via lm-sensors, Windows via WMI).
*/
