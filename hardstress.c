#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

// Para intrinsics AVX/SSE
#if defined(__AVX__) || defined(__SSE__)
#include <immintrin.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <process.h> // Para _beginthreadex
#pragma comment(lib, "pdh.lib")
#else
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#endif

#include <gtk/gtk.h>
#include <cairo.h>

/* -------------------------
   Constantes de Configuração
   ------------------------- */
#define DEFAULT_WINDOW_WIDTH 900
#define DEFAULT_WINDOW_HEIGHT 600
#define DEFAULT_MEM_MIB 256
#define DEFAULT_DURATION_SEC 300
#define CPU_GRAPH_HEIGHT 200
#define CPU_SAMPLE_INTERVAL_MS 500

const double COLOR_BAR_BG_R = 0.12, COLOR_BAR_BG_G = 0.12, COLOR_BAR_BG_B = 0.12;
const double COLOR_BAR_FG_R = 0.15, COLOR_BAR_FG_G = 0.65, COLOR_BAR_FG_B = 0.90;
const double COLOR_TEXT_R = 1.0, COLOR_TEXT_G = 1.0, COLOR_TEXT_B = 1.0;

/* -------------------------
   Estruturas de Dados
   ------------------------- */
typedef struct {
    int threads;
    size_t mem_mib_per_thread;
    int duration_sec;
    gboolean pin_affinity;
} Cfg;

typedef struct {
    int tid;
    size_t buf_bytes;
    uint8_t *buf;
    size_t idx_len;
    uint32_t *idx;
    atomic_int running;
    atomic_uint iters;
} Worker;

// Contexto principal da aplicação para evitar globais
typedef struct AppContext AppContext; // Declaração antecipada

#ifdef _WIN32
typedef HANDLE thread_handle_t;
#else
typedef pthread_t thread_handle_t;
#endif

struct AppContext {
    // Widgets da GUI
    GtkWidget *win;
    GtkWidget *entry_threads, *entry_mem, *entry_dur, *check_pin;
    GtkWidget *btn_start, *btn_stop;
    GtkTextBuffer *log_buffer;
    GtkWidget *cpu_drawing;
    GtkWidget *status_label;

    // Configuração do teste
    Cfg cfg;

    // Estado do teste
    atomic_int g_running;
    atomic_int g_errors;
    atomic_ullong g_total_iters;
    Worker *workers;
    thread_handle_t *worker_threads;
    thread_handle_t cpu_sampler_thread;
    thread_handle_t controller_thread; // Thread para iniciar/parar

    // Dados de uso da CPU
    int cpu_count;
    double *cpu_usage; // normalizado 0.0 - 1.0
    GMutex cpu_mutex;

#ifdef _WIN32
    // Contexto do PDH para Windows
    struct {
        PDH_HQUERY query;
        PDH_HCOUNTER *counters;
        int cores;
    } pdh_ctx;
#endif
};

/* -------------------------
   Protótipos de Funções
   ------------------------- */
static void on_btn_stop(GtkButton *b, AppContext *app);


/* -------------------------
   Utilitários: tempo e log
   ------------------------- */
static void gui_append_log(AppContext *app, const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    char *msg = NULL;
    g_vasprintf(&msg, fmt, ap);
    va_end(ap);
    if (!msg) return;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->log_buffer, &end);
    gtk_text_buffer_insert(app->log_buffer, &end, msg, -1);
    g_free(msg);
}

/* -------------------------
   Abstração de Threads Multiplataforma
   ------------------------- */
#ifdef _WIN32
typedef struct {
    void (*fn)(void*);
    void *arg;
} win_thunk_t;

static unsigned __stdcall thread_start_thunk(void *arg){
    win_thunk_t *thunk = (win_thunk_t*)arg;
    thunk->fn(thunk->arg);
    free(thunk);
    _endthreadex(0);
    return 0;
}
static thread_handle_t spawn_thread(void (*fn)(void*), void *arg){
    win_thunk_t *thunk = malloc(sizeof(win_thunk_t));
    thunk->fn = fn;
    thunk->arg = arg;
    return (HANDLE)_beginthreadex(NULL, 0, thread_start_thunk, thunk, 0, NULL);
}
static void join_thread(thread_handle_t t){
    if (t) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
}
#else
static thread_handle_t spawn_thread(void (*fn)(void*), void *arg){
    pthread_t t;
    pthread_create(&t, NULL, (void*(*)(void*))fn, arg);
    return t;
}
static void join_thread(thread_handle_t t){
    if (t) {
        pthread_join(t, NULL);
    }
}
#endif


/* -------------------------
   Kernels de Stress
   ------------------------- */

/* SplitMix64 para geração de números pseudo-aleatórios */
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

/* Kernel FPU: Versão escalar (fallback) */
static void kernel_fpu_scalar(float *A, float *B, float *C, size_t n, int iters){
    for (int k=0; k < iters; k++){
        for (size_t i=0; i < n; i++){
            C[i] = A[i] * B[i] + C[i]; // FMA
        }
    }
}

#ifdef __AVX__
/* Kernel FPU: Versão otimizada com AVX Intrinsics */
static void kernel_fpu_avx(float *A, float *B, float *C, size_t n, int iters){
    for (int k=0; k < iters; k++){
        for (size_t i=0; i <= n - 8; i += 8){
            __m256 a_vec = _mm256_loadu_ps(&A[i]);
            __m256 b_vec = _mm256_loadu_ps(&B[i]);
            __m256 c_vec = _mm256_loadu_ps(&C[i]);
            c_vec = _mm256_fmadd_ps(a_vec, b_vec, c_vec); // Fused Multiply-Add para 8 floats
            _mm256_storeu_ps(&C[i], c_vec);
        }
    }
}
#endif

static inline uint64_t mix64(uint64_t x){
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33; return x;
}

static void kernel_int(uint64_t *dst, size_t n, int iters){
    uint64_t acc = 0xC0FFEE;
    for (int k=0; k<iters; k++){
        for (size_t i=0; i<n; i++){
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
    for (int r=0; r<rounds; r++){
        for (size_t s=0; s<n; s++){
            i = idx[i];
        }
    }
    (void)i; // Evita warning de "unused variable"
}

/* Worker Principal */
static void worker_main(void *arg){
    Worker *w = (Worker*)arg;
    size_t floats = w->buf_bytes / sizeof(float);
    float *A = (float*)w->buf;
    float *B = (float*)(w->buf + w->buf_bytes/3);
    float *C = (float*)(w->buf + 2*(w->buf_bytes/3));
    uint64_t *I64 = (uint64_t*)w->buf;

    uint64_t seed = 0x12340000 + (uint64_t)w->tid;
    for (size_t i=0; i<floats; i++){
        A[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
        B[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
        C[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
    }
    size_t ints64 = w->buf_bytes / sizeof(uint64_t);
    for (size_t i=0; i<ints64; i++) I64[i] = splitmix64(&seed);

    w->idx_len = (w->buf_bytes / sizeof(uint32_t));
    w->idx = malloc(w->idx_len * sizeof(uint32_t));
    if (!w->idx) {
        // Esta falha acontece em outra thread, então não podemos logar na GUI diretamente
        // Apenas incrementamos o contador de erros.
        atomic_fetch_add(&((AppContext*)((void**)&w[-w->tid]))[1]->g_errors, 1);
        return;
    }
    for (uint32_t i=0; i<w->idx_len; i++) w->idx[i] = i;
    shuffle32(w->idx, w->idx_len, &seed);
    w->idx[w->idx_len-1] = 0;

    while (atomic_load(&w->running)){
        #ifdef __AVX__
        kernel_fpu_avx(A,B,C,floats,8);
        #else
        kernel_fpu_scalar(A,B,C,floats,8);
        #endif
        kernel_int(I64, ints64 > 1024 ? 1024 : ints64, 4);
        kernel_stream(w->buf, w->buf_bytes);
        kernel_ptrchase(w->idx, w->idx_len, 4);
        atomic_fetch_add(&w->iters, 1u);
    }
    free(w->idx);
}

/* -------------------------
   Coleta de Uso de CPU (Multiplataforma)
   ------------------------- */
static int detect_cpu_count(void){
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
#endif
}

#ifndef _WIN32 // Implementação para Linux
typedef struct {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_sample_t;

static int read_proc_stat(cpu_sample_t *out, int maxcpu){
    FILE *f = fopen("/proc/stat","r");
    if (!f) return -1;
    char line[512];
    int idx = -1;
    while (fgets(line, sizeof(line), f)){
        if (strncmp(line, "cpu", 3) != 0) break;
        if (strncmp(line, "cpu ", 4) == 0) continue;
        idx++;
        if (idx >= maxcpu) break;
        sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu",
               &out[idx].user, &out[idx].nice, &out[idx].system, &out[idx].idle,
               &out[idx].iowait, &out[idx].irq, &out[idx].softirq, &out[idx].steal);
    }
    fclose(f);
    return idx + 1;
}

static double compute_usage(const cpu_sample_t *a, const cpu_sample_t *b){
    unsigned long long idle_a = a->idle + a->iowait;
    unsigned long long idle_b = b->idle + b->iowait;
    unsigned long long nonidle_a = a->user + a->nice + a->system + a->irq + a->softirq + a->steal;
    unsigned long long nonidle_b = b->user + b->nice + b->system + b->irq + a->softirq + b->steal;
    unsigned long long total_a = idle_a + nonidle_a;
    unsigned long long total_b = idle_b + nonidle_b;
    if (total_b - total_a == 0) return 0.0;
    double usage = (double)((total_b - total_a) - (idle_b - idle_a)) / (double)(total_b - total_a);
    return (usage > 1.0) ? 1.0 : (usage < 0.0 ? 0.0 : usage);
}

static void platform_sample_cpu(AppContext *app){
    int ncpu = app->cpu_count;
    cpu_sample_t *s1 = calloc(ncpu, sizeof(cpu_sample_t));
    cpu_sample_t *s2 = calloc(ncpu, sizeof(cpu_sample_t));
    if (!s1 || !s2){ free(s1); free(s2); return; }

    if (read_proc_stat(s1, ncpu) > 0) {
        struct timespec req = {0, 200 * 1000000}; nanosleep(&req, NULL);
        if (read_proc_stat(s2, ncpu) > 0) {
            g_mutex_lock(&app->cpu_mutex);
            for (int i=0; i<ncpu; i++) {
                app->cpu_usage[i] = compute_usage(&s1[i], &s2[i]);
            }
            g_mutex_unlock(&app->cpu_mutex);
        }
    }
    free(s1); free(s2);
}

#else // Implementação para Windows
static int pdh_init(AppContext *app){
    PDH_STATUS st = PdhOpenQuery(NULL, 0, &app->pdh_ctx.query);
    if (st != ERROR_SUCCESS) return -1;
    app->pdh_ctx.counters = calloc(app->cpu_count, sizeof(PDH_HCOUNTER));
    if (!app->pdh_ctx.counters) { PdhCloseQuery(app->pdh_ctx.query); return -1; }

    for (int i=0; i<app->cpu_count; i++){
        char path[256];
        snprintf(path, sizeof(path), "\\Processor(%d)\\%% Processor Time", i);
        st = PdhAddCounterA(app->pdh_ctx.query, path, 0, &app->pdh_ctx.counters[i]);
        if (st != ERROR_SUCCESS){
            for (int j=0; j<i; j++) PdhRemoveCounter(app->pdh_ctx.counters[j]);
            free(app->pdh_ctx.counters); app->pdh_ctx.counters = NULL;
            PdhCloseQuery(app->pdh_ctx.query); app->pdh_ctx.query = NULL;
            return -1;
        }
    }
    PdhCollectQueryData(app->pdh_ctx.query); // Coleta inicial
    app->pdh_ctx.cores = app->cpu_count;
    return 0;
}

static void platform_sample_cpu(AppContext *app){
    if (!app->pdh_ctx.query) return;
    PdhCollectQueryData(app->pdh_ctx.query);
    for (int i=0; i < app->pdh_ctx.cores; i++){
        PDH_FMT_COUNTERVALUE val;
        if (PdhGetFormattedCounterValue(app->pdh_ctx.counters[i], PDH_FMT_DOUBLE, NULL, &val) == ERROR_SUCCESS){
            double usage = val.doubleValue / 100.0;
            g_mutex_lock(&app->cpu_mutex);
            app->cpu_usage[i] = (usage > 1.0) ? 1.0 : (usage < 0.0 ? 0.0 : usage);
            g_mutex_unlock(&app->cpu_mutex);
        }
    }
}
#endif

/* Thread de amostragem de CPU */
static void cpu_sampler_thread_main(void *arg){
    AppContext *app = (AppContext*)arg;
    while (atomic_load(&app->g_running)){
        platform_sample_cpu(app);
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, app->cpu_drawing);

        struct timespec req = {0, CPU_SAMPLE_INTERVAL_MS * 1000000};
        nanosleep(&req, NULL);
    }
}

/* -------------------------
   Callbacks e Lógica da GUI
   ------------------------- */

static gboolean on_draw_cpu(GtkWidget *widget, cairo_t *cr, AppContext *app){
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    int w = alloc.width, h = alloc.height;
    int n = app->cpu_count > 0 ? app->cpu_count : 1;
    int bar_w = w / n;

    g_mutex_lock(&app->cpu_mutex);
    for (int i=0; i<n; i++){
        double u = app->cpu_usage ? app->cpu_usage[i] : 0.0;
        int x = i * bar_w;
        int bar_h = (int)(u * h);
        
        cairo_set_source_rgb(cr, COLOR_BAR_BG_R, COLOR_BAR_BG_G, COLOR_BAR_BG_B);
        cairo_rectangle(cr, x, 0, bar_w - 2, h);
        cairo_fill(cr);
        
        cairo_set_source_rgb(cr, COLOR_BAR_FG_R, COLOR_BAR_FG_G, COLOR_BAR_FG_B);
        cairo_rectangle(cr, x + 1, h - bar_h, bar_w - 4, bar_h);
        cairo_fill(cr);
        
        char txt[32];
        snprintf(txt, sizeof(txt), "%.0f%%", u*100.0);
        cairo_set_source_rgb(cr, COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, x + 4, 12);
        cairo_show_text(cr, txt);
    }
    g_mutex_unlock(&app->cpu_mutex);
    return FALSE;
}

static void set_controls_sensitive(AppContext *app, gboolean state){
    gtk_widget_set_sensitive(app->entry_threads, state);
    gtk_widget_set_sensitive(app->entry_mem, state);
    gtk_widget_set_sensitive(app->entry_dur, state);
    gtk_widget_set_sensitive(app->check_pin, state);
    gtk_widget_set_sensitive(app->btn_start, state);
}

/* Funções para atualizar a GUI a partir de outras threads */
static gboolean update_gui_for_start(gpointer user_data){
    AppContext *app = (AppContext*)user_data;
    gtk_widget_set_sensitive(app->btn_stop, TRUE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Rodando...");
    gui_append_log(app, "[GUI] Teste iniciado: %d threads, %zu MiB/thread, %ds, pin=%d\n",
                   app->cfg.threads, app->cfg.mem_mib_per_thread, app->cfg.duration_sec, app->cfg.pin_affinity);
    return G_SOURCE_REMOVE;
}

static gboolean update_gui_for_stop(gpointer user_data){
    AppContext *app = (AppContext*)user_data;
    set_controls_sensitive(app, TRUE);
    gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Parado");
    gui_append_log(app, "[GUI] Teste parado.\n");
    return G_SOURCE_REMOVE;
}

/* Thread de controle que gerencia o ciclo de vida do teste */
void test_controller_thread(void *arg) {
    AppContext *app = (AppContext*)arg;
    atomic_store(&app->g_running, 1);
    atomic_store(&app->g_errors, 0);
    atomic_store(&app->g_total_iters, 0);

    // Inicializa amostragem de CPU
    app->cpu_count = detect_cpu_count();
    g_free(app->cpu_usage);
    app->cpu_usage = g_new0(double, app->cpu_count);
#ifdef _WIN32
    if (pdh_init(app) != 0) {
        gui_append_log(app, "[GUI] Falha ao iniciar PDH para monitoramento de CPU.\n");
    }
#endif

    // Aloca workers
    app->workers = g_new0(Worker, app->cfg.threads);
    app->worker_threads = g_new0(thread_handle_t, app->cfg.threads);

    for (int i=0; i < app->cfg.threads; i++){
        app->workers[i].tid = i;
        app->workers[i].buf_bytes = app->cfg.mem_mib_per_thread * 1024ULL * 1024ULL;
        if (app->workers[i].buf_bytes < 4096) app->workers[i].buf_bytes = 4096;
        app->workers[i].buf = malloc(app->workers[i].buf_bytes);
        if (!app->workers[i].buf){
            gui_append_log(app, "[ERRO] Falha ao alocar buffer para a thread %d.\n", i);
            atomic_fetch_add(&app->g_errors, 1);
        }
        atomic_store(&app->workers[i].running, 1);
    }
    if (atomic_load(&app->g_errors) > 0) {
        on_btn_stop(NULL, app); // Para e limpa
        return;
    }

    // Dispara a thread de amostragem de CPU
    app->cpu_sampler_thread = spawn_thread(cpu_sampler_thread_main, app);

    // Dispara as threads de trabalho (workers)
    for (int i=0; i < app->cfg.threads; i++){
        app->worker_threads[i] = spawn_thread(worker_main, &app->workers[i]);
        if (app->cfg.pin_affinity) {
#ifdef _WIN32
            SetThreadAffinityMask(app->worker_threads[i], (DWORD_PTR)(1ULL << (i % app->cpu_count)));
#else
            cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(i % app->cpu_count, &cpuset);
            pthread_setaffinity_np(app->worker_threads[i], sizeof(cpu_set_t), &cpuset);
#endif
        }
    }
    
    // Atualiza a GUI para refletir o estado de "rodando"
    g_idle_add(update_gui_for_start, app);
}

static void on_btn_start(GtkButton *b, AppContext *app) {
    if (atomic_load(&app->g_running)) return;
    
    // Validação de entrada
    char *endptr;
    const char *threads_str = gtk_entry_get_text(GTK_ENTRY(app->entry_threads));
    long threads = strtol(threads_str, &endptr, 10);
    if (*endptr != '\0' || threads < 0) { gui_append_log(app, "[GUI] Número de threads inválido.\n"); return; }

    const char *mem_str = gtk_entry_get_text(GTK_ENTRY(app->entry_mem));
    long long mem = strtoll(mem_str, &endptr, 10);
    if (*endptr != '\0' || mem <= 0) { gui_append_log(app, "[GUI] Valor de memória inválido.\n"); return; }

    const char *dur_str = gtk_entry_get_text(GTK_ENTRY(app->entry_dur));
    long dur = strtol(dur_str, &endptr, 10);
    if (*endptr != '\0' || dur < 0) { gui_append_log(app, "[GUI] Duração inválida.\n"); return; }
    
    app->cfg.threads = (threads == 0) ? detect_cpu_count() : (int)threads;
    app->cfg.mem_mib_per_thread = (size_t)mem;
    app->cfg.duration_sec = (int)dur;
    app->cfg.pin_affinity = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_pin));

    set_controls_sensitive(app, FALSE); // Desabilita controles imediatamente

    // Dispara a thread de controle para fazer o trabalho pesado
    app->controller_thread = spawn_thread(test_controller_thread, app);
}

void stop_controller_thread(void *arg) {
    AppContext *app = (AppContext*)arg;
    if (!atomic_exchange(&app->g_running, 0)) return;

    // Sinaliza para as threads pararem
    for (int i=0; i < app->cfg.threads; i++) {
        atomic_store(&app->workers[i].running, 0);
    }

    // Espera todas as threads terminarem
    join_thread(app->cpu_sampler_thread);
    app->cpu_sampler_thread = 0;
    for (int i=0; i < app->cfg.threads; i++) {
        join_thread(app->worker_threads[i]);
    }

    // Libera recursos
    for (int i=0; i < app->cfg.threads; i++){
        free(app->workers[i].buf);
    }
    g_free(app->workers); app->workers = NULL;
    g_free(app->worker_threads); app->worker_threads = NULL;

#ifdef _WIN32
    if (app->pdh_ctx.query) {
        PdhCloseQuery(app->pdh_ctx.query);
        app->pdh_ctx.query = NULL;
        free(app->pdh_ctx.counters);
        app->pdh_ctx.counters = NULL;
    }
#endif

    // Atualiza a GUI para o estado "parado"
    g_idle_add(update_gui_for_stop, app);
}

static void on_btn_stop(GtkButton *b, AppContext *app) {
    if (!atomic_load(&app->g_running)) return;

    gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Parando...");

    app->controller_thread = spawn_thread(stop_controller_thread, app);
}

static void on_save_log(GtkButton *b, AppContext *app) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Salvar Log", GTK_WINDOW(app->win),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancelar", GTK_RESPONSE_CANCEL, "_Salvar", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(app->log_buffer, &start);
        gtk_text_buffer_get_end_iter(app->log_buffer, &end);
        char *text = gtk_text_buffer_get_text(app->log_buffer, &start, &end, FALSE);
        
        FILE *f = fopen(filename, "w");
        if (f) {
            fputs(text, f);
            fclose(f);
            gui_append_log(app, "[GUI] Log salvo em %s\n", filename);
        } else {
            gui_append_log(app, "[GUI] Falha ao salvar log em %s\n", filename);
        }
        g_free(text);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static gboolean stats_tick(gpointer user_data) {
    AppContext *app = (AppContext*)user_data;
    if (!atomic_load(&app->g_running)) return G_SOURCE_CONTINUE;

    static guint64 last_total = 0;
    guint64 cur_total = 0;
    for (int i = 0; i < app->cfg.threads; ++i) {
        cur_total += atomic_load(&app->workers[i].iters);
    }
    
    guint64 diff = cur_total - last_total;
    last_total = cur_total;
    
    char buf[128];
    snprintf(buf, sizeof(buf), "Rodando... iters/s=%llu, erros=%d", (unsigned long long)diff, atomic_load(&app->g_errors));
    gtk_label_set_text(GTK_LABEL(app->status_label), buf);

    return G_SOURCE_CONTINUE;
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, AppContext *app) {
    if (atomic_load(&app->g_running)) {
        on_btn_stop(NULL, app);
        // Espera a thread de parada terminar para não fechar abruptamente
        join_thread(app->controller_thread);
    }
    return FALSE; // Permite o fechamento da janela
}

/* -------------------------
   Função Principal
   ------------------------- */
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    AppContext *app = g_new0(AppContext, 1);
    g_mutex_init(&app->cpu_mutex);

    app->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->win), "HardStress GUI (Refatorado)");
    gtk_window_set_default_size(GTK_WINDOW(app->win), DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);

    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(app->win), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    // Controles
    char buffer[64];
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Threads (0=auto):"), 0, 0, 1, 1);
    app->entry_threads = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app->entry_threads), "0");
    gtk_grid_attach(GTK_GRID(grid), app->entry_threads, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Memória por Thread (MiB):"), 0, 1, 1, 1);
    app->entry_mem = gtk_entry_new();
    snprintf(buffer, sizeof(buffer), "%d", DEFAULT_MEM_MIB);
    gtk_entry_set_text(GTK_ENTRY(app->entry_mem), buffer);
    gtk_grid_attach(GTK_GRID(grid), app->entry_mem, 1, 1, 1, 1);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Duração (s, 0=inf):"), 0, 2, 1, 1);
    app->entry_dur = gtk_entry_new();
    snprintf(buffer, sizeof(buffer), "%d", DEFAULT_DURATION_SEC);
    gtk_entry_set_text(GTK_ENTRY(app->entry_dur), buffer);
    gtk_grid_attach(GTK_GRID(grid), app->entry_dur, 1, 2, 1, 1);

    app->check_pin = gtk_check_button_new_with_label("Fixar threads em CPUs (Affinity)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_pin), TRUE);
    gtk_grid_attach(GTK_GRID(grid), app->check_pin, 0, 3, 2, 1);

    app->btn_start = gtk_button_new_with_label("Iniciar");
    app->btn_stop = gtk_button_new_with_label("Parar"); gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gtk_grid_attach(GTK_GRID(grid), app->btn_start, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->btn_stop, 1, 4, 1, 1);

    GtkWidget *btn_save = gtk_button_new_with_label("Salvar Log");
    gtk_grid_attach(GTK_GRID(grid), btn_save, 0, 5, 2, 1);

    app->status_label = gtk_label_new("Parado");
    gtk_grid_attach(GTK_GRID(grid), app->status_label, 0, 6, 2, 1);

    // Gráfico de CPU
    app->cpu_drawing = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->cpu_drawing, 0, CPU_GRAPH_HEIGHT);
    gtk_grid_attach(GTK_GRID(grid), app->cpu_drawing, 0, 7, 2, 1);
    g_signal_connect(G_OBJECT(app->cpu_drawing), "draw", G_CALLBACK(on_draw_cpu), app);

    // Log
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_grid_attach(GTK_GRID(grid), scrolled, 0, 8, 2, 1);
    GtkWidget *text = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    app->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    gui_append_log(app, "[GUI] HardStress inicializado. Pronto para iniciar o teste.\n");

    // Sinais
    g_signal_connect(app->win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app->win, "delete-event", G_CALLBACK(on_window_delete), app);
    g_signal_connect(app->btn_start, "clicked", G_CALLBACK(on_btn_start), app);
    g_signal_connect(app->btn_stop, "clicked", G_CALLBACK(on_btn_stop), app);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_log), app);

    g_timeout_add(1000, stats_tick, app);

    gtk_widget_show_all(app->win);
    gtk_main();

    // Limpeza final
    if (atomic_load(&app->g_running)) {
        on_btn_stop(NULL, app);
        join_thread(app->controller_thread);
    }
    g_free(app->cpu_usage);
    g_mutex_clear(&app->cpu_mutex);
    g_free(app);

    return 0;
}
