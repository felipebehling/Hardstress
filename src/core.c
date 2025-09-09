#include "core.h"
#include "metrics.h" // Para cpu_sampler_thread_func
#include "utils.h"   // Para now_sec, shuffle32, etc.
#include "ui.h"      // Para gui_log

/* --- Protótipos Estáticos --- */
static void worker_main(void *arg);
static void kernel_fpu(float *A, float *B, float *C, size_t n, int iters);
static inline uint64_t mix64(uint64_t x);
static void kernel_int(uint64_t *dst, size_t n, int iters);
static void kernel_stream(uint8_t *buf, size_t n);
static void kernel_ptrchase(uint32_t *idx, size_t n, int rounds);

/* --- Implementação da Thread Controladora --- */

void controller_thread_func(void *arg){
    AppContext *app = (AppContext*)arg;
    atomic_store(&app->running, 1);
    atomic_store(&app->errors, 0);
    atomic_store(&app->total_iters, 0);
    app->start_time = now_sec();

    if(app->csv_realtime_en) {
        char fname[256];
        snprintf(fname, sizeof(fname), "hardstress_log_%.0f.csv", app->start_time);
        app->csv_log_file = fopen(fname, "w");
        if(app->csv_log_file) {
            gui_log(app, "[Logger] Log CSV em tempo real ativo: %s\n", fname);
        } else {
            gui_log(app, "[Logger] ERRO: Nao foi possivel abrir o arquivo de log CSV.\n");
            app->csv_realtime_en = 0; // Desativa se falhar
        }
    }

    app->cpu_count = detect_cpu_count();
    app->cpu_usage = calloc(app->cpu_count, sizeof(double));

    app->history_len = HISTORY_SAMPLES;
    app->history_pos = 0;
    app->thread_history = calloc(app->threads, sizeof(unsigned*));
    for (int t=0; t<app->threads; t++) app->thread_history[t] = calloc(app->history_len, sizeof(unsigned));

    app->workers = calloc(app->threads, sizeof(worker_t));
    app->worker_threads = calloc(app->threads, sizeof(thread_handle_t));
    for (int i=0; i<app->threads; i++){
        app->workers[i] = (worker_t){ .tid = i, .app = app };
        app->workers[i].buf_bytes = app->mem_mib_per_thread * 1024ULL * 1024ULL;
        atomic_init(&app->workers[i].status, WORKER_OK);
    }

    thread_create(&app->cpu_sampler_thread, (thread_func_t)cpu_sampler_thread_func, app);

    for (int i=0; i<app->threads; i++){
        thread_create(&app->worker_threads[i], (thread_func_t)worker_main, &app->workers[i]);
        if (app->pin_affinity){
#ifdef _WIN32
            if(app->worker_threads[i]) SetThreadAffinityMask(app->worker_threads[i], (DWORD_PTR)(1ULL << (i % app->cpu_count)));
#else
            cpu_set_t set; CPU_ZERO(&set); CPU_SET(i % app->cpu_count, &set);
            pthread_setaffinity_np(app->worker_threads[i], sizeof(cpu_set_t), &set);
#endif
        }
    }

    double end_time = (app->duration_sec > 0) ? app->start_time + app->duration_sec : 0;
    while (atomic_load(&app->running)){
        if (end_time > 0 && now_sec() >= end_time){
             gui_log(app, "[GUI] Duração de %d s atingida. Parando...\n", app->duration_sec);
             atomic_store(&app->running, 0);
             break;
        }
        struct timespec r = {0, 200*1000000}; nanosleep(&r,NULL);
    }

    for (int i=0; i<app->threads; i++) atomic_store(&app->workers[i].running, 0);
    for (int i=0; i<app->threads; i++) thread_join(app->worker_threads[i]);
    
    // Sinaliza para a thread sampler parar e aguarda sua finalização
    atomic_store(&app->running, 0); 
    thread_join(app->cpu_sampler_thread);

    if (app->csv_log_file) {
        fclose(app->csv_log_file);
        app->csv_log_file = NULL;
    }

    // Limpeza final dos buffers, mas NÃO da estrutura 'app'
    for (int i=0; i<app->threads; i++) free(app->thread_history[i]);
    free(app->thread_history); app->thread_history = NULL;
    free(app->workers); app->workers = NULL;
    free(app->worker_threads); app->worker_threads = NULL;
    free(app->cpu_usage); app->cpu_usage = NULL;

    // A thread se desvincula. Não há mais um handle para ela ser aguardada (joined).
    thread_detach(app->controller_thread); 
    app->controller_thread = 0;

    // MODIFICADO: Em vez de fechar o programa, reativamos a UI para um novo teste.
    g_idle_add((GSourceFunc)gui_update_stopped, app);
}

/* --- Implementação da Thread Worker e Kernels --- */

static void worker_main(void *arg){
    worker_t *w = (worker_t*)arg;
    AppContext *app = w->app;
    
    atomic_store(&w->status, WORKER_OK);
    if (w->buf_bytes > 0) {
        w->buf = malloc(w->buf_bytes);
        if (!w->buf){
            gui_log(app, "[T%d] falha na alocacao do buffer (%zu bytes)\n", w->tid, w->buf_bytes);
            atomic_fetch_add(&app->errors, 1);
            atomic_store(&w->status, WORKER_ALLOC_FAIL);
            return;
        }
    }

    size_t floats = w->buf_bytes / sizeof(float);
    float *A = (float*)w->buf;
    float *B = (float*)(w->buf + w->buf_bytes/3);
    float *C = (float*)(w->buf + 2*(w->buf_bytes/3));
    uint64_t *I64 = (uint64_t*)w->buf;
    uint64_t seed = 0x12340000 + (uint64_t)w->tid;

    if (app->kernel_fpu_en && w->buf) {
        for (size_t i=0; i < (floats / 3); i++){
            A[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
            B[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
            C[i] = (float)(splitmix64(&seed) & 0xFFFF) / 65535.0f;
        }
    }
    if (app->kernel_int_en && w->buf) {
        size_t ints64 = w->buf_bytes / sizeof(uint64_t);
        for (size_t i=0;i<ints64;i++) I64[i] = splitmix64(&seed);
    }
    
    if(app->kernel_ptr_en && w->buf) {
        w->idx_len = (w->buf_bytes / sizeof(uint32_t));
        w->idx = malloc(w->idx_len * sizeof(uint32_t));
        if (!w->idx){
            gui_log(app, "[T%d] falha na alocacao do indice\n", w->tid);
            atomic_fetch_add(&app->errors, 1);
            atomic_store(&w->status, WORKER_ALLOC_FAIL);
            free(w->buf);
            return;
        }
        for (uint32_t i=0; i<w->idx_len; i++) w->idx[i] = i;
        shuffle32(w->idx, w->idx_len, &seed);
        w->idx[w->idx_
