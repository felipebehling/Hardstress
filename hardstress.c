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
#include <process.h> // _beginthreadex
// #pragma comment(lib, "pdh.lib") // Usamos -lpdh no Makefile em vez disso
#else
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#endif

#include <gtk/gtk.h>
#include <cairo.h>

/* CONFIG */
#define DEFAULT_MEM_MIB 256
#define DEFAULT_DURATION_SEC 300
#define CPU_SAMPLE_INTERVAL_MS 500
#define HISTORY_SAMPLES 240 /* 240 * 0.5s = 120s */
#define ITER_SCALE 1000.0 /* scale factor for iteration chart */

const double COLOR_BG_R = 0.12, COLOR_BG_G = 0.12, COLOR_BG_B = 0.12;
const double COLOR_FG_R = 0.15, COLOR_FG_G = 0.65, COLOR_FG_B = 0.90;

/* Forward */
typedef struct AppContext AppContext;

/* Worker */
typedef struct {
    int tid;
    size_t buf_bytes;
    uint8_t *buf;
    uint32_t *idx;
    size_t idx_len;
    atomic_int running;
    atomic_uint iters;
    AppContext *app;
} worker_t;

/* AppContext */
struct AppContext {
    /* config */
    int threads;
    size_t mem_mib_per_thread;
    int duration_sec;
    int pin_affinity;

    /* state */
    atomic_int running;
    atomic_int errors;
    atomic_uint total_iters;
    double start_time;

    /* workers */
    worker_t *workers;
#ifdef _WIN32
    HANDLE *worker_threads;
    HANDLE cpu_sampler_handle;
    HANDLE controller_thread_handle; // Handle para a thread de controle
#else
    pthread_t *worker_threads;
    pthread_t cpu_sampler_thread;
    pthread_t controller_thread; // Handle para a thread de controle
#endif

    /* CPU usage */
    int cpu_count;
    double *cpu_usage;
    GMutex cpu_mutex;
#ifdef _WIN32
    PDH_HQUERY pdh_query;
    PDH_HCOUNTER *pdh_counters;
#endif

    /* per-thread history */
    unsigned **thread_history; /* [thread][history_len] */
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
    GtkWidget *btn_start, *btn_stop, *btn_export;
    GtkTextBuffer *log_buffer;
    GtkWidget *cpu_drawing;
    GtkWidget *iters_drawing;
    GtkWidget *status_label;
};

/* UTIL */
static double now_sec(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec*1e-9;
}

static void gui_log(AppContext *app, const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    char *s = NULL; g_vasprintf(&s, fmt, ap);
    va_end(ap);
    if (!s) return;
    GtkTextIter end; gtk_text_buffer_get_end_iter(app->log_buffer, &end);
    gtk_text_buffer_insert(app->log_buffer, &end, s, -1);
    g_free(s);
}

/* RANDOM helpers */
static uint64_t splitmix64(uint64_t *x){
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void shuffle32(uint32_t *a, size_t n, uint64_t *seed){
    for (size_t i = n - 1; i > 0; --i){
        size_t j = (size_t)(splitmix64(seed) % (i + 1));
        uint32_t tmp = a[i]; a[i] = a[j]; a[j] = tmp;
    }
}

/* KERNELS */
static void kernel_fpu(float *A, float *B, float *C, size_t n, int iters){
    for (int k = 0; k < iters; ++k)
        for (size_t i = 0; i < n; ++i) C[i] = A[i]*B[i] + C[i];
}
static inline uint64_t mix64(uint64_t x){
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
    return x;
}
static void kernel_int(uint64_t *dst, size_t n, int iters){
    uint64_t acc = 0xC0FFEE;
    for (int k=0;k<iters;k++){
        for (size_t i=0;i<n;i++){
            acc ^= mix64(dst[i] + i);
            dst[i] = acc + (dst[i] << 1) + (dst[i] >> 3);
        }
    }
}
static void kernel_stream(uint8_t *buf, size_t n){
    memset(buf, 0xA5, n/2);
    memcpy(buf + n/2, buf, n/2);
}
static void kernel_ptrchase(uint32_t *idx, size_t n, int rounds){
    size_t i = 0;
    for (int r=0;r<rounds;r++)
        for (size_t s=0;s<n;s++) i = idx[i];
    (void)i;
}

/* Worker main */
static void worker_main(void *arg){
    worker_t *w = (worker_t*)arg;
    AppContext *app = w->app;
    /* allocate buffer lazily (makes it safer to report to GUI if fail) */
    w->buf = malloc(w->buf_bytes);
    if (!w->buf){
        gui_log(app, "[T%d] buffer aloc failed (%zu bytes)\n", w->tid, w->buf_bytes);
        atomic_fetch_add(&app->errors, 1);
        return;
    }

    size_t floats = w->buf_bytes / sizeof(float);
    float *A = (float*)w->buf;
    float *B = (float*)(w->buf + w->buf_bytes/3);
    float *C = (float*)(w->buf + 2*(w->buf_bytes/3));
    uint64_t *I64 = (uint64_t*)w->buf;
    uint64_t seed = 0x12340000 + (uint64_t)w->tid;

    for (size_t i=0;i<floats;i++){
        A[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
        B[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
        C[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
    }
    size_t ints64 = w->buf_bytes / sizeof(uint64_t);
    for (size_t i=0;i<ints64;i++) I64[i] = splitmix64(&seed);

    w->idx_len = (w->buf_bytes / sizeof(uint32_t));
    w->idx = malloc(w->idx_len * sizeof(uint32_t));
    if (!w->idx){
        gui_log(app, "[T%d] idx alloc failed\n", w->tid);
        atomic_fetch_add(&app->errors, 1);
        free(w->buf);
        return;
    }
    for (uint32_t i=0;i<w->idx_len;i++) w->idx[i] = i;
    shuffle32(w->idx, w->idx_len, &seed);
    w->idx[w->idx_len-1] = 0;

    atomic_store(&w->running, 1u);

    while (atomic_load(&w->running) && atomic_load(&app->running)){
        kernel_fpu(A,B,C,floats,4);
        kernel_int(I64, ints64 > 1024 ? 1024 : ints64, 4);
        kernel_stream(w->buf, w->buf_bytes);
        kernel_ptrchase(w->idx, w->idx_len, 4);
        atomic_fetch_add(&w->iters, 1u);
        atomic_fetch_add(&app->total_iters, 1u);

        /* store into history slot safely */
        g_mutex_lock(&app->history_mutex);
        if (app->thread_history) app->thread_history[w->tid][app->history_pos] = atomic_load(&w->iters);
        g_mutex_unlock(&app->history_mutex);
    }

    free(w->idx);
    free(w->buf);
}

/* CPU sampling */
static int detect_cpu_count(void){
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN); return n>0?(int)n:1;
#endif
}

#ifndef _WIN32
/* Linux: /proc/stat */
typedef struct { unsigned long long user,nice,system,idle,iowait,irq,softirq,steal; } cpu_sample_t;
static int read_proc_stat(cpu_sample_t *out, int maxcpu){
    FILE *f = fopen("/proc/stat","r"); if(!f) return -1;
    char line[512]; int idx=-1;
    while (fgets(line,sizeof(line),f)){
        if (strncmp(line,"cpu",3)!=0) break;
        if (strncmp(line,"cpu ",4)==0) continue;
        idx++; if (idx>=maxcpu) break;
        sscanf(line,"cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu",
               &out[idx].user,&out[idx].nice,&out[idx].system,&out[idx].idle,&out[idx].iowait,&out[idx].irq,&out[idx].softirq,&out[idx].steal);
    }
    fclose(f); return idx+1;
}

static double compute_usage(const cpu_sample_t *a,const cpu_sample_t *b){
    unsigned long long idle_a=a->idle + a->iowait;
    unsigned long long idle_b=b->idle + b->iowait;
    unsigned long long nonidle_a=a->user + a->nice + a->system + a->irq + a->softirq + a->steal;
    unsigned long long nonidle_b=b->user + b->nice + b->system + b->irq + b->softirq + b->steal;
    unsigned long long total_a = idle_a + nonidle_a;
    unsigned long long total_b = idle_b + nonidle_b;
    unsigned long long totald = total_b - total_a;
    unsigned long long idled = idle_b - idle_a;
    if (totald == 0) return 0.0;
    double perc = (double)(totald - idled) / (double)totald;
    if (perc < 0.0) perc = 0.0; if (perc > 1.0) perc = 1.0;
    return perc;
}

static void sample_cpu_linux(AppContext *app){
    int n = app->cpu_count;
    cpu_sample_t *s1 = calloc(n, sizeof(cpu_sample_t));
    cpu_sample_t *s2 = calloc(n, sizeof(cpu_sample_t));
    if (!s1 || !s2){ free(s1); free(s2); return; }
    if (read_proc_stat(s1, n) <= 0){ free(s1); free(s2); return; }
    struct timespec r = {0, 200*1000000}; nanosleep(&r,NULL);
    if (read_proc_stat(s2, n) <= 0){ free(s1); free(s2); return; }
    g_mutex_lock(&app->cpu_mutex);
    for (int i=0;i<n;i++) app->cpu_usage[i] = compute_usage(&s1[i], &s2[i]);
    g_mutex_unlock(&app->cpu_mutex);
    free(s1); free(s2);
}
#else
/* Windows sampling via PDH - IMPLEMENTAÇÃO CORRIGIDA */
static int pdh_init_query(AppContext *app){
    if (PdhOpenQuery(NULL, 0, &app->pdh_query) != ERROR_SUCCESS) {
        return -1;
    }
    app->pdh_counters = calloc(app->cpu_count, sizeof(PDH_HCOUNTER));
    if (!app->pdh_counters) {
        PdhCloseQuery(app->pdh_query);
        return -1;
    }
    for (int i = 0; i < app->cpu_count; i++) {
        char path[256];
        snprintf(path, sizeof(path), "\\Processor(%d)\\%% Processor Time", i);
        if (PdhAddCounterA(app->pdh_query, path, 0, &app->pdh_counters[i]) != ERROR_SUCCESS) {
            for (int j = 0; j < i; j++) PdhRemoveCounter(app->pdh_counters[j]);
            free(app->pdh_counters);
            app->pdh_counters = NULL;
            PdhCloseQuery(app->pdh_query);
            app->pdh_query = NULL;
            return -1;
        }
    }
    return PdhCollectQueryData(app->pdh_query);
}
static void pdh_close_query(AppContext *app) {
    if (app->pdh_query) {
        PdhCloseQuery(app->pdh_query);
        app->pdh_query = NULL;
    }
    if (app->pdh_counters) {
        free(app->pdh_counters);
        app->pdh_counters = NULL;
    }
}
static void sample_cpu_windows(AppContext *app){
    if (!app->pdh_query) return;
    PdhCollectQueryData(app->pdh_query);
    g_mutex_lock(&app->cpu_mutex);
    for (int i = 0; i < app->cpu_count; i++) {
        PDH_FMT_COUNTERVALUE val;
        if (PdhGetFormattedCounterValue(app->pdh_counters[i], PDH_FMT_DOUBLE, NULL, &val) == ERROR_SUCCESS) {
            double usage = val.doubleValue / 100.0;
            app->cpu_usage[i] = (usage < 0.0) ? 0.0 : (usage > 1.0 ? 1.0 : usage);
        } else {
            app->cpu_usage[i] = 0.0;
        }
    }
    g_mutex_unlock(&app->cpu_mutex);
}
#endif

/* Temperature sampling */
#ifndef _WIN32
static void sample_temp_linux(AppContext *app){
    /* Try to call `sensors -u` and parse first temperature input */
    FILE *p = popen("sensors -u 2>/dev/null", "r");
    if (!p){ g_mutex_lock(&app->temp_mutex); app->temp_celsius = -1.0; g_mutex_unlock(&app->temp_mutex); return; }
    char line[256];
    double found = -1.0;
    while (fgets(line, sizeof(line), p)){
        char *k = strstr(line, "_input:");
        if (k){
            double v;
            if (sscanf(k+7, "%lf", &v) == 1){
                found = v;
                break;
            }
        }
    }
    pclose(p);
    g_mutex_lock(&app->temp_mutex); app->temp_celsius = found; g_mutex_unlock(&app->temp_mutex);
}
#else
static void sample_temp_windows(AppContext *app){
    /* Use PowerShell WMI query for thermal zones (may not exist) */
    FILE *p = _popen("powershell -Command \"try { Get-WmiObject MSAcpi_ThermalZoneTemperature -Namespace root\\wmi | Select-Object -ExpandProperty CurrentTemperature -First 1 } catch {}\" 2> $null", "r");
    if (!p){ g_mutex_lock(&app->temp_mutex); app->temp_celsius = -1.0; g_mutex_unlock(&app->temp_mutex); return; }
    char buf[128]; double found = -1.0;
    if (fgets(buf, sizeof(buf), p)){
        double raw = atof(buf);
        if (raw > 0) found = (raw/10.0) - 273.15;
    }
    _pclose(p);
    g_mutex_lock(&app->temp_mutex); app->temp_celsius = found; g_mutex_unlock(&app->temp_mutex);
}
#endif

/* CPU sampler thread */
static void cpu_sampler_thread_func(void *arg){
    AppContext *app = (AppContext*)arg;
    while (atomic_load(&app->running)){
#ifndef _WIN32
        sample_cpu_linux(app);
        sample_temp_linux(app);
#else
        sample_cpu_windows(app);
        sample_temp_windows(app);
#endif
        /* redraw UI via idle */
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, app->cpu_drawing);
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, app->iters_drawing);
        /* advance history position */
        g_mutex_lock(&app->history_mutex);
        app->history_pos = (app->history_pos + 1) % app->history_len;
        /* clear next history slot for all threads */
        if (app->thread_history){
            for (int t=0;t<app->threads;t++){
                app->thread_history[t][app->history_pos] = 0;
            }
        }
        g_mutex_unlock(&app->history_mutex);
        struct timespec r = {0, CPU_SAMPLE_INTERVAL_MS * 1000000};
        nanosleep(&r,NULL);
    }
}

/* UI: CPU draw */
static gboolean on_draw_cpu(GtkWidget *widget, cairo_t *cr, gpointer user_data){
    AppContext *app = (AppContext*)user_data;
    GtkAllocation alloc; gtk_widget_get_allocation(widget, &alloc);
    int w = alloc.width, h = alloc.height;
    int n = app->cpu_count > 0 ? app->cpu_count : 1;
    int bw = w / n;
    g_mutex_lock(&app->cpu_mutex);
    for (int i=0;i<n;i++){
        double u = (app->cpu_usage && i < app->cpu_count) ? app->cpu_usage[i] : 0.0;
        int x = i * bw;
        int bar_h = (int)(u * h);
        cairo_set_source_rgb(cr, COLOR_BG_R, COLOR_BG_G, COLOR_BG_B);
        cairo_rectangle(cr, x, 0, bw-2, h); cairo_fill(cr);
        cairo_set_source_rgb(cr, COLOR_FG_R, COLOR_FG_G, COLOR_FG_B);
        cairo_rectangle(cr, x+1, h - bar_h, bw-4, bar_h); cairo_fill(cr);
        char txt[32]; snprintf(txt, sizeof(txt), "%.0f%%", u*100.0);
        cairo_set_source_rgb(cr, 1,1,1);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, x+4, 12); cairo_show_text(cr, txt);
    }
    g_mutex_unlock(&app->cpu_mutex);
    /* temp */
    g_mutex_lock(&app->temp_mutex);
    double temp = app->temp_celsius;
    g_mutex_unlock(&app->temp_mutex);
    if (temp > -200){
        char tbuf[64]; snprintf(tbuf, sizeof(tbuf), "Temp: %.2f C", temp);
        cairo_set_source_rgb(cr, 1,1,0.8);
        cairo_move_to(cr, 6, h - 6);
        cairo_show_text(cr, tbuf);
    }
    return FALSE;
}

/* UI: iterations per thread draw */
static gboolean on_draw_iters(GtkWidget *widget, cairo_t *cr, gpointer user_data){
    AppContext *app = (AppContext*)user_data;
    if (!app->running) return FALSE;

    GtkAllocation alloc; gtk_widget_get_allocation(widget, &alloc);
    int W = alloc.width, H = alloc.height;
    int tcount = app->threads > 0 ? app->threads : 1;
    int margin = 4;
    int area_h = (H - (tcount+1)*margin) / tcount;

    g_mutex_lock(&app->history_mutex);
    for (int t=0; t < app->threads; t++){
        int y0 = margin + t*(area_h+margin);
        cairo_set_source_rgb(cr, 0.06,0.06,0.06);
        cairo_rectangle(cr, 0, y0, W, area_h); cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.8,0.4,0.1);
        cairo_set_line_width(cr, 1.0);
        int samples = app->history_len;
        double step = (samples > 1) ? ((double)W / (samples - 1)) : W;
        
        int start_idx = (app->history_pos + 1) % samples;
        unsigned last_v = app->thread_history ? app->thread_history[t][start_idx] : 0;

        for (int s = 0; s < samples; s++) {
            int current_idx = (start_idx + s) % samples;
            unsigned current_v = app->thread_history ? app->thread_history[t][current_idx] : 0;
            
            if (s == 0) { // Move to the first point
                double y = y0 + area_h - (((double)(current_v - last_v)) / ITER_SCALE) * area_h;
                if (y < y0) y = y0; else if (y > y0 + area_h) y = y0 + area_h;
                cairo_move_to(cr, s * step, y);
            } else {
                double y = y0 + area_h - (((double)(current_v - last_v)) / ITER_SCALE) * area_h;
                if (y < y0) y = y0; else if (y > y0 + area_h) y = y0 + area_h;
                cairo_line_to(cr, s * step, y);
            }
            last_v = current_v;
        }
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 1,1,1);
        cairo_move_to(cr, 6, y0 + 12);
        char lbl[64]; snprintf(lbl, sizeof(lbl), "T%02d iters/s (x%.0f)", t, ITER_SCALE / (CPU_SAMPLE_INTERVAL_MS/1000.0));
        cairo_show_text(cr, lbl);
    }
    g_mutex_unlock(&app->history_mutex);
    return FALSE;
}

/* Utility to set controls sensitive */
static void set_controls_sensitive(AppContext *app, gboolean state){
    gtk_widget_set_sensitive(app->entry_threads, state);
    gtk_widget_set_sensitive(app->entry_mem, state);
    gtk_widget_set_sensitive(app->entry_dur, state);
    gtk_widget_set_sensitive(app->check_pin, state);
    gtk_widget_set_sensitive(app->btn_start, state);
}

/* GUI updates from other threads */
static gboolean gui_update_started(gpointer ud){
    AppContext *app = (AppContext*)ud;
    gtk_widget_set_sensitive(app->btn_stop, TRUE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Rodando...");
    gui_log(app, "[GUI] Teste iniciado: threads=%d mem/thread=%zu dur=%ds pin=%d\n",
            app->threads, app->mem_mib_per_thread, app->duration_sec, app->pin_affinity);
    return G_SOURCE_REMOVE;
}
static gboolean gui_update_stopped(gpointer ud){
    AppContext *app = (AppContext*)ud;
    set_controls_sensitive(app, TRUE);
    gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Parado");
    gui_log(app, "[GUI] Teste parado.\n");
    return G_SOURCE_REMOVE;
}

/* CSV export */
static void export_csv_dialog(AppContext *app){
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Exportar CSV", GTK_WINDOW(app->win),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancelar", GTK_RESPONSE_CANCEL, "_Salvar", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT){
        char *fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        FILE *f = fopen(fname, "w");
        if (!f){ gui_log(app, "[GUI] Falha ao abrir %s para escrita\n", fname); g_free(fname); gtk_widget_destroy(dialog); return; }
        fprintf(f, "timestamp");
        for (int c=0;c<app->cpu_count;c++) fprintf(f, ",cpu%d", c);
        for (int t=0;t<app->threads;t++) fprintf(f, ",thread%d_iters", t);
        fprintf(f, ",temp_c\n");
        for (int s=0;s<app->history_len;s++){
            int idx = (app->history_pos + 1 + s) % app->history_len;
            double ts = app->start_time + s * (CPU_SAMPLE_INTERVAL_MS / 1000.0) - (app->history_len * (CPU_SAMPLE_INTERVAL_MS / 1000.0));
            fprintf(f, "%.3f", ts);
            g_mutex_lock(&app->cpu_mutex);
            for (int c=0;c<app->cpu_count;c++) fprintf(f, ",%.6f", app->cpu_usage[c]);
            g_mutex_unlock(&app->cpu_mutex);
            g_mutex_lock(&app->history_mutex);
            for (int t=0;t<app->threads;t++) fprintf(f, ",%u", app->thread_history[t][idx]);
            g_mutex_unlock(&app->history_mutex);
            g_mutex_lock(&app->temp_mutex);
            double temp = app->temp_celsius;
            g_mutex_unlock(&app->temp_mutex);
            fprintf(f, ",%.3f\n", temp);
        }
        fclose(f);
        gui_log(app, "[GUI] CSV exportado para %s\n", fname);
        g_free(fname);
    }
    gtk_widget_destroy(dialog);
}

/* Controller thread: starts workers, sampler and monitors duration */
static void controller_thread_func(void *arg){
    AppContext *app = (AppContext*)arg;
    atomic_store(&app->running, 1);
    atomic_store(&app->errors, 0);
    atomic_store(&app->total_iters, 0);
    app->start_time = now_sec();

    /* init CPU arrays */
    app->cpu_count = detect_cpu_count();
    app->cpu_usage = calloc(app->cpu_count, sizeof(double));
#ifdef _WIN32
    if(pdh_init_query(app) != ERROR_SUCCESS) {
        gui_log(app, "[ERRO] Falha ao inicializar PDH para monitoramento de CPU.\n");
    }
#endif

    app->history_len = HISTORY_SAMPLES;
    app->history_pos = 0;
    app->thread_history = calloc(app->threads, sizeof(unsigned*));
    for (int t=0;t<app->threads;t++) app->thread_history[t] = calloc(app->history_len, sizeof(unsigned));

    app->workers = calloc(app->threads, sizeof(worker_t));
#ifdef _WIN32
    app->worker_threads = calloc(app->threads, sizeof(HANDLE));
#else
    app->worker_threads = calloc(app->threads, sizeof(pthread_t));
#endif
    for (int i=0;i<app->threads;i++){
        app->workers[i] = (worker_t){ .tid = i, .mem_mib_per_thread = app->mem_mib_per_thread, .app = app };
        app->workers[i].buf_bytes = app->mem_mib_per_thread * 1024ULL * 1024ULL;
    }

#ifdef _WIN32
    app->cpu_sampler_handle = (HANDLE)_beginthreadex(NULL, 0, (unsigned(__stdcall*)(void*))cpu_sampler_thread_func, app, 0, NULL);
#else
    pthread_create(&app->cpu_sampler_thread, NULL, (void*(*)(void*))cpu_sampler_thread_func, app);
#endif

    for (int i=0;i<app->threads;i++){
#ifdef _WIN32
        app->worker_threads[i] = (HANDLE)_beginthreadex(NULL, 0, (unsigned(__stdcall*)(void*))worker_main, &app->workers[i], 0, NULL);
#else
        pthread_create(&app->worker_threads[i], NULL, (void*(*)(void*))worker_main, &app->workers[i]);
#endif
        if (app->pin_affinity){
#ifdef _WIN32
            if(app->worker_threads[i]) SetThreadAffinityMask(app->worker_threads[i], (DWORD_PTR)(1ULL << (i % app->cpu_count)));
#else
            cpu_set_t set; CPU_ZERO(&set); CPU_SET(i % app->cpu_count, &set);
            pthread_setaffinity_np(app->worker_threads[i], sizeof(cpu_set_t), &set);
#endif
        }
    }

    g_idle_add(gui_update_started, app);

    double end_time = (app->duration_sec > 0) ? app->start_time + app->duration_sec : 0;
    while (atomic_load(&app->running)){
        if (end_time > 0 && now_sec() >= end_time){
             gui_log(app, "[GUI] Duração de %d s atingida. Parando...\n", app->duration_sec);
             atomic_store(&app->running, 0);
             break;
        }
        Sleep(200);
    }

    for (int i=0;i<app->threads;i++) atomic_store(&app->workers[i].running, 0);
    for (int i=0;i<app->threads;i++){
#ifdef _WIN32
        if(app->worker_threads[i]) { WaitForSingleObject(app->worker_threads[i], INFINITE); CloseHandle(app->worker_threads[i]); }
#else
        pthread_join(app->worker_threads[i], NULL);
#endif
    }
#ifdef _WIN32
    if(app->cpu_sampler_handle) { WaitForSingleObject(app->cpu_sampler_handle, INFINITE); CloseHandle(app->cpu_sampler_handle); }
#else
    pthread_join(app->cpu_sampler_thread, NULL);
#endif

    for (int i=0;i<app->threads;i++) free(app->thread_history[i]);
    free(app->thread_history); app->thread_history = NULL;
    free(app->workers); app->workers = NULL;
    free(app->worker_threads); app->worker_threads = NULL;
    free(app->cpu_usage); app->cpu_usage = NULL;
#ifdef _WIN32
    pdh_close_query(app);
#endif

    g_idle_add(gui_update_stopped, app);
#ifdef _WIN32
    CloseHandle(app->controller_thread_handle);
    app->controller_thread_handle = NULL;
#endif
}

/* on_start handler */
static void on_btn_start_clicked(GtkButton *b, gpointer ud){
    (void)b;
    AppContext *app = (AppContext*)ud;
    if (atomic_load(&app->running)) return;

    char *end;
    long threads = strtol(gtk_entry_get_text(GTK_ENTRY(app->entry_threads)), &end, 10);
    if (*end != '\0' || threads < 0){ gui_log(app, "[GUI] threads invalido\n"); return; }
    long mem = strtol(gtk_entry_get_text(GTK_ENTRY(app->entry_mem)), &end, 10);
    if (*end != '\0' || mem <= 0){ gui_log(app, "[GUI] memoria invalida\n"); return; }
    long dur = strtol(gtk_entry_get_text(GTK_ENTRY(app->entry_dur)), &end, 10);
    if (*end != '\0' || dur < 0){ gui_log(app, "[GUI] duracao invalida\n"); return; }

    app->threads = (threads == 0) ? detect_cpu_count() : (int)threads;
    app->mem_mib_per_thread = (size_t)mem;
    app->duration_sec = (int)dur;
    app->pin_affinity = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_pin));

    set_controls_sensitive(app, FALSE);

#ifdef _WIN32
    app->controller_thread_handle = (HANDLE)_beginthreadex(NULL, 0, (unsigned(__stdcall*)(void*))controller_thread_func, app, 0, NULL);
#else
    pthread_create(&app->controller_thread, NULL, (void*(*)(void*))controller_thread_func, app);
    pthread_detach(app->controller_thread);
#endif
}

/* on_stop handler */
static void on_btn_stop_clicked(GtkButton *b, gpointer ud){
    (void)b;
    AppContext *app = (AppContext*)ud;
    if (!atomic_load(&app->running)) return;
    atomic_store(&app->running, 0);
    gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gui_log(app, "[GUI] stop requested by user\n");
}

/* on_export handler */
static void on_btn_export_clicked(GtkButton *b, gpointer ud){
    (void)b;
    AppContext *app = (AppContext*)ud;
    export_csv_dialog(app);
}

/* UI periodic tick to update status (iters/s) */
static gboolean ui_tick(gpointer ud){
    AppContext *app = (AppContext*)ud;
    if (!atomic_load(&app->running)) return TRUE;
    static unsigned long long last_total = 0;
    unsigned long long cur = atomic_load(&app->total_iters);
    unsigned long long diff = cur - last_total;
    last_total = cur;
    char buf[128];
    snprintf(buf, sizeof(buf), "iters/s=%llu errs=%d", diff, atomic_load(&app->errors));
    gtk_label_set_text(GTK_LABEL(app->status_label), buf);
    return TRUE;
}

/* window delete (clean stop) */
static gboolean on_window_delete(GtkWidget *w, GdkEvent *e, gpointer ud){
    (void)w; (void)e;
    AppContext *app = (AppContext*)ud;
    if (atomic_load(&app->running)){
        gui_log(app, "[GUI] fechando: solicitando parada...\n");
        atomic_store(&app->running, 0);
#ifdef _WIN32
        if(app->controller_thread_handle) WaitForSingleObject(app->controller_thread_handle, 1000);
#else
        struct timespec r = {1,0}; nanosleep(&r,NULL);
#endif
    }
    return FALSE;
}

/* MAIN */
int main(int argc, char **argv){
    gtk_init(&argc, &argv);
    AppContext *app = calloc(1, sizeof(AppContext));
    g_mutex_init(&app->cpu_mutex);
    g_mutex_init(&app->history_mutex);
    g_mutex_init(&app->temp_mutex);
    
    app->mem_mib_per_thread = DEFAULT_MEM_MIB;
    app->duration_sec = DEFAULT_DURATION_SEC;
    app->pin_affinity = 1;
    app->history_len = HISTORY_SAMPLES;
    app->temp_celsius = -274.0;

    app->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(app->win), 1000, 800);
    gtk_window_set_title(GTK_WINDOW(app->win), "HardStress GUI - Complete");

    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(app->win), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    /* Controls */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Threads (0=auto):"), 0, 0, 1, 1);
    app->entry_threads = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app->entry_threads), "0");
    gtk_grid_attach(GTK_GRID(grid), app->entry_threads, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Mem (MiB/thread):"), 0, 1, 1, 1);
    app->entry_mem = gtk_entry_new(); { char buf[32]; snprintf(buf,sizeof(buf), "%zu", app->mem_mib_per_thread); gtk_entry_set_text(GTK_ENTRY(app->entry_mem), buf); }
    gtk_grid_attach(GTK_GRID(grid), app->entry_mem, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Duração (s, 0=inf):"), 0, 2, 1, 1);
    app->entry_dur = gtk_entry_new(); { char buf[32]; snprintf(buf,sizeof(buf), "%d", app->duration_sec); gtk_entry_set_text(GTK_ENTRY(app->entry_dur), buf); }
    gtk_grid_attach(GTK_GRID(grid), app->entry_dur, 1, 2, 1, 1);

    app->check_pin = gtk_check_button_new_with_label("Pin threads to CPUs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_pin), TRUE);
    gtk_grid_attach(GTK_GRID(grid), app->check_pin, 0, 3, 2, 1);

    app->btn_start = gtk_button_new_with_label("Start");
    app->btn_stop = gtk_button_new_with_label("Stop"); gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gtk_grid_attach(GTK_GRID(grid), app->btn_start, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->btn_stop, 1, 4, 1, 1);

    app->btn_export = gtk_button_new_with_label("Export CSV");
    gtk_grid_attach(GTK_GRID(grid), app->btn_export, 0, 5, 2, 1);

    app->status_label = gtk_label_new("idle");
    gtk_grid_attach(GTK_GRID(grid), app->status_label, 0, 6, 2, 1);

    app->cpu_drawing = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->cpu_drawing, 0, 200);
    gtk_grid_attach(GTK_GRID(grid), app->cpu_drawing, 0, 7, 2, 1);
    g_signal_connect(app->cpu_drawing, "draw", G_CALLBACK(on_draw_cpu), app);

    app->iters_drawing = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->iters_drawing, 0, 300);
    gtk_grid_attach(GTK_GRID(grid), app->iters_drawing, 0, 8, 2, 1);
    g_signal_connect(app->iters_drawing, "draw", G_CALLBACK(on_draw_iters), app);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL); gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_grid_attach(GTK_GRID(grid), scrolled, 0, 9, 2, 1);
    GtkWidget *text = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    app->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    gtk_container_add(GTK_CONTAINER(scrolled), text);

    g_signal_connect(app->win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app->win, "delete-event", G_CALLBACK(on_window_delete), app);
    g_signal_connect(app->btn_start, "clicked", G_CALLBACK(on_btn_start_clicked), app);
    g_signal_connect(app->btn_stop, "clicked", G_CALLBACK(on_btn_stop_clicked), app);
    g_signal_connect(app->btn_export, "clicked", G_CALLBACK(on_btn_export_clicked), app);

    g_timeout_add(1000, ui_tick, app);

    gui_log(app, "[GUI] ready\n");
    gtk_widget_show_all(app->win);
    gtk_main();

    if (atomic_load(&app->running)) atomic_store(&app->running, 0);
    g_mutex_clear(&app->cpu_mutex);
    g_mutex_clear(&app->history_mutex);
    g_mutex_clear(&app->temp_mutex);
    free(app);
    return 0;
}
