#include "metrics.h"
#include "ui.h" // Para gui_log

/* --- Protótipos Estáticos --- */
#ifdef _WIN32
static int pdh_init_query(AppContext *app);
static void pdh_close_query(AppContext *app);
static void sample_cpu_windows(AppContext *app);
static void wmi_init(AppContext *app);
static void wmi_deinit(AppContext *app);
static void sample_temp_windows(AppContext *app);
#else
static void sample_cpu_linux(AppContext *app);
static void sample_temp_linux(AppContext *app);
#endif

static void log_csv_header(AppContext *app);
static void log_csv_sample(AppContext *app);

/* --- Implementação da Thread Sampler --- */

void cpu_sampler_thread_func(void *arg){
    AppContext *app = (AppContext*)arg;
    
#ifdef _WIN32
    wmi_init(app);
    if(pdh_init_query(app) != ERROR_SUCCESS) {
        gui_log(app, "[ERRO] Falha ao inicializar PDH para monitoramento de CPU.\n");
    }
#endif

    if(app->csv_realtime_en) {
        log_csv_header(app);
    }

    while (atomic_load(&app->running)){
#ifndef _WIN32
        sample_cpu_linux(app);
        sample_temp_linux(app);
#else
        sample_cpu_windows(app);
        sample_temp_windows(app);
#endif
        // Pede para a thread da UI redesenhar os widgets
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, app->cpu_drawing);
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, app->iters_drawing);
        
        if (app->csv_realtime_en) {
            log_csv_sample(app);
        }

        g_mutex_lock(&app->history_mutex);
        app->history_pos = (app->history_pos + 1) % app->history_len;
        if (app->thread_history){
            for (int t=0; t<app->threads; t++){
                // Zera a posição atual para o próximo ciclo de amostragem
                app->thread_history[t][app->history_pos] = 0;
            }
        }
        g_mutex_unlock(&app->history_mutex);

        struct timespec r = {0, CPU_SAMPLE_INTERVAL_MS * 1000000};
        nanosleep(&r,NULL);
    }

#ifdef _WIN32
    pdh_close_query(app);
    wmi_deinit(app);
#endif
}

/* --- Implementações de Coleta de Dados --- */

int detect_cpu_count(void){
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1;
#endif
}

#ifndef _WIN32 /* LINUX IMPLEMENTATION */
int read_proc_stat(cpu_sample_t *out, int maxcpu, const char *path) {
    FILE *f = fopen(path, "r"); if(!f) return -1;
    char line[512];
    int count = 0;
    while (count < maxcpu && fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu", 3) != 0) break;
        if (strncmp(line, "cpu ", 4) == 0) continue;

        sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu",
               &out[count].user, &out[count].nice, &out[count].system,
               &out[count].idle, &out[count].iowait, &out[count].irq,
               &out[count].softirq, &out[count].steal);
        count++;
    }
    fclose(f);
    return count;
}

double compute_usage(const cpu_sample_t *a,const cpu_sample_t *b){
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
    if (perc < 0.0) perc = 0.0;
    if (perc > 1.0) perc = 1.0;
    return perc;
}

static void sample_cpu_linux(AppContext *app) {
    int n = app->cpu_count;
    if (n <= 0 || !app->prev_cpu_samples) return;

    cpu_sample_t *current_samples = calloc(n, sizeof(cpu_sample_t));
    if (!current_samples) return;

    if (read_proc_stat(current_samples, n, "/proc/stat") <= 0) {
        free(current_samples);
        return;
    }

    // A primeira amostra apenas preenche o buffer 'prev'. O uso será calculado na próxima.
    // Verificamos o primeiro valor 'user' para ver se o buffer já foi preenchido.
    if (app->prev_cpu_samples[0].user == 0 && app->prev_cpu_samples[0].idle == 0) {
        memcpy(app->prev_cpu_samples, current_samples, n * sizeof(cpu_sample_t));
        free(current_samples);
        return;
    }

    g_mutex_lock(&app->cpu_mutex);
    for (int i = 0; i < n; i++) {
        app->cpu_usage[i] = compute_usage(&app->prev_cpu_samples[i], &current_samples[i]);
    }
    g_mutex_unlock(&app->cpu_mutex);

    // Salva as amostras atuais para a próxima iteração
    memcpy(app->prev_cpu_samples, current_samples, n * sizeof(cpu_sample_t));
    free(current_samples);
}

static void sample_temp_linux(AppContext *app){
    FILE *p = popen("sensors -u 2>/dev/null", "r");
    if (!p){ g_mutex_lock(&app->temp_mutex); app->temp_celsius = TEMP_UNAVAILABLE; g_mutex_unlock(&app->temp_mutex); return; }
    char line[256];
    double found = TEMP_UNAVAILABLE;
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

#else /* WINDOWS IMPLEMENTATION */

static int pdh_init_query(AppContext *app){
    if (PdhOpenQuery(NULL, 0, &app->pdh_query) != ERROR_SUCCESS) return -1;
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
            free(app->pdh_counters); app->pdh_counters = NULL;
            PdhCloseQuery(app->pdh_query); app->pdh_query = NULL;
            return -1;
        }
    }
    return PdhCollectQueryData(app->pdh_query);
}

static void pdh_close_query(AppContext *app) {
    if(!app->pdh_query) return;
    if (app->pdh_counters) {
        for(int i=0; i<app->cpu_count; ++i) PdhRemoveCounter(app->pdh_counters[i]);
        free(app->pdh_counters);
        app->pdh_counters = NULL;
    }
    PdhCloseQuery(app->pdh_query);
    app->pdh_query = NULL;
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

static void wmi_init(AppContext *app) {
    app->pSvc = NULL;
    app->pLoc = NULL;
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) return;

    hres = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if (FAILED(hres)) { CoUninitialize(); return; }

    hres = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID*)&app->pLoc);
    if (FAILED(hres)) { CoUninitialize(); return; }

    hres = app->pLoc->lpVtbl->ConnectServer(app->pLoc, L"ROOT\\WMI", NULL, NULL, NULL, 0, NULL, NULL, &app->pSvc);
    if (FAILED(hres)) { app->pLoc->lpVtbl->Release(app->pLoc); app->pLoc = NULL; CoUninitialize(); return; }

    hres = CoSetProxyBlanket((IUnknown*)app->pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hres)) {
        app->pSvc->lpVtbl->Release(app->pSvc); app->pSvc = NULL;
        app->pLoc->lpVtbl->Release(app->pLoc); app->pLoc = NULL;
        CoUninitialize();
    }
}
static void wmi_deinit(AppContext *app) {
    if(app->pSvc) { app->pSvc->lpVtbl->Release(app->pSvc); app->pSvc = NULL; }
    if(app->pLoc) { app->pLoc->lpVtbl->Release(app->pLoc); app->pLoc = NULL; }
    CoUninitialize();
}
static void sample_temp_windows(AppContext *app) {
    if (!app->pSvc) {
        g_mutex_lock(&app->temp_mutex); app->temp_celsius = TEMP_UNAVAILABLE; g_mutex_unlock(&app->temp_mutex);
        return;
    }
    IEnumWbemClassObject* pEnumerator = NULL;
    HRESULT hres = app->pSvc->lpVtbl->ExecQuery(app->pSvc, L"WQL", L"SELECT * FROM MSAcpi_ThermalZoneTemperature", WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
    double temp = TEMP_UNAVAILABLE;

    if (SUCCEEDED(hres)) {
        IWbemClassObject *pclsObj = NULL;
        ULONG uReturn = 0;
        if (pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn) == WBEM_S_NO_ERROR && uReturn != 0) {
            VARIANT vtProp;
            pclsObj->lpVtbl->Get(pclsObj, L"CurrentTemperature", 0, &vtProp, 0, 0);
            temp = (V_I4(&vtProp) / 10.0) - 273.15;
            VariantClear(&vtProp);
            pclsObj->lpVtbl->Release(pclsObj);
        }
        pEnumerator->lpVtbl->Release(pEnumerator);
    }
    g_mutex_lock(&app->temp_mutex); app->temp_celsius = temp; g_mutex_unlock(&app->temp_mutex);
}

#endif

/* --- CSV LOGGING --- */

static void log_csv_header(AppContext *app) {
    if (!app->csv_log_file) return;
    fprintf(app->csv_log_file, "timestamp");
    for (int c = 0; c < app->cpu_count; c++) fprintf(app->csv_log_file, ",cpu%d_usage", c);
    for (int t = 0; t < app->threads; t++) fprintf(app->csv_log_file, ",thread%d_iters_total", t);
    fprintf(app->csv_log_file, ",temp_celsius\n");
    fflush(app->csv_log_file);
}

static void log_csv_sample(AppContext *app) {
    if (!app->csv_log_file) return;
    
    // Obter o timestamp atual (não está no .h, mas pertence a utils.c, o protótipo seria necessário)
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double timestamp = ts.tv_sec + ts.tv_nsec * 1e-9;
    fprintf(app->csv_log_file, "%.3f", timestamp);

    g_mutex_lock(&app->cpu_mutex);
    for (int c = 0; c < app->cpu_count; c++) fprintf(app->csv_log_file, ",%.6f", app->cpu_usage[c]);
    g_mutex_unlock(&app->cpu_mutex);

    // O histórico em memória guarda o valor total de iterações, que é o que queremos logar
    g_mutex_lock(&app->history_mutex);
    int current_pos = app->history_pos; // A posição que acabamos de preencher
    if(app->thread_history) {
      for (int t = 0; t < app->threads; t++) fprintf(app->csv_log_file, ",%u", app->thread_history[t][current_pos]);
    }
    g_mutex_unlock(&app->history_mutex);
    
    g_mutex_lock(&app->temp_mutex);
    fprintf(app->csv_log_file, ",%.3f\n", app->temp_celsius);
    g_mutex_unlock(&app->temp_mutex);
    
    fflush(app->csv_log_file);
}
