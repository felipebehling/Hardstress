#ifndef HARDSTRESS_H
#define HARDSTRESS_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <Wbemidl.h>
#include <pdh.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include <gtk/gtk.h>
#include <cairo.h>

/* --- CONFIG --- */
#define DEFAULT_MEM_MIB 256
#define DEFAULT_DURATION_SEC 300
#define CPU_SAMPLE_INTERVAL_MS 500
#define HISTORY_SAMPLES 240
#define ITER_SCALE 1000.0
#define TEMP_UNAVAILABLE -274.0

/* --- THEME --- */
typedef struct { double r, g, b; } color_t;
extern const color_t COLOR_BG, COLOR_FG, COLOR_WARN, COLOR_ERR, COLOR_TEXT, COLOR_TEMP;

/* --- THREAD ABSTRACTION --- */
#ifdef _WIN32
typedef HANDLE thread_handle_t;
typedef unsigned (__stdcall *thread_func_t)(void *);
#else
typedef pthread_t thread_handle_t;
typedef void *(*thread_func_t)(void *);
#endif

int thread_create(thread_handle_t *t, thread_func_t func, void *arg);
int thread_join(thread_handle_t t);
int thread_detach(thread_handle_t t);

/* --- FORWARD DECLARATIONS --- */
typedef struct AppContext AppContext;
typedef struct worker_t worker_t;

/* --- WORKER --- */
typedef enum { WORKER_OK = 0, WORKER_ALLOC_FAIL } worker_status_t;

struct worker_t {
    int tid;
    size_t buf_bytes;
    uint8_t *buf;
    uint32_t *idx;
    size_t idx_len;
    atomic_int running;
    atomic_uint iters;
    atomic_int status;
    AppContext *app;
};

/* --- APP CONTEXT --- */
struct AppContext {
    /* config */
    int threads;
    size_t mem_mib_per_thread;
    int duration_sec;
    int pin_affinity;
    int kernel_fpu_en, kernel_int_en, kernel_stream_en, kernel_ptr_en;
    int csv_realtime_en;

    /* state */
    atomic_int running;
    atomic_int errors;
    atomic_uint total_iters;
    double start_time;
    FILE *csv_log_file;

    /* workers & threads */
    worker_t *workers;
    thread_handle_t *worker_threads;
    thread_handle_t cpu_sampler_thread;
    thread_handle_t controller_thread;

    /* CPU usage */
    int cpu_count;
    double *cpu_usage;
    GMutex cpu_mutex;
#ifdef _WIN32
    PDH_HQUERY pdh_query;
    PDH_HCOUNTER *pdh_counters;
    IWbemServices *pSvc;
    IWbemLocator *pLoc;
#endif

    /* per-thread history */
    unsigned **thread_history;
    int history_pos;
    int history_len;
    GMutex history_mutex;

    /* temp */
    double temp_celsius;
    GMutex temp_mutex;

    /* GUI widgets */
    GtkWidget *win;
    GtkWidget *entry_threads, *entry_mem, *entry_dur;
    GtkWidget *check_pin;
    GtkWidget *check_fpu, *check_int, *check_stream, *check_ptr;
    GtkWidget *check_csv_realtime;
    GtkWidget *btn_start, *btn_stop, *btn_save_metrics, *btn_defaults, *btn_clear_log;
    GtkTextBuffer *log_buffer;
    GtkWidget *log_view;
    GtkWidget *cpu_drawing;
    GtkWidget *iters_drawing;
    GtkWidget *status_label;
    GtkWidget *mem_warning_label;
};

/* --- FUNCTION PROTOTYPES --- */
// ui.c
GtkWidget* create_main_window(AppContext *app);
void gui_log(AppContext *app, const char *fmt, ...);

// core.c
void controller_thread_func(void *arg);

// metrics.c
void cpu_sampler_thread_func(void *arg);
int detect_cpu_count(void);

// utils.c
double now_sec(void);
uint64_t splitmix64(uint64_t *x);
void shuffle32(uint32_t *a, size_t n, uint64_t *seed);

#endif // HARDSTRESS_H
