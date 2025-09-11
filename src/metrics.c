#include "metrics.h"
#include "ui.h" // For gui_log

/* --- Static Function Prototypes --- */
#ifdef _WIN32
static int pdh_init_query(AppContext *app);
static void pdh_close_query(AppContext *app);
static void sample_cpu_windows(AppContext *app);
static void wmi_init(AppContext *app);
static void wmi_deinit(AppContext *app);
static void sample_temp_windows(AppContext *app);
#else

#else
static void sample_cpu_linux(AppContext *app);
static void sample_temp_linux(AppContext *app);
#endif

static void sample_cpu_linux(AppContext *app);
static void sample_temp_linux(AppContext *app);
#endif

static void log_csv_header(AppContext *app);
static void log_csv_sample(AppContext *app);

/* --- Sampler Thread Implementation --- */

void cpu_sampler_thread_func(void *arg){
    AppContext *app = (AppContext*)arg;
    
#ifdef _WIN32
    // On Windows, initialize COM for WMI and the PDH query for CPU usage.
    wmi_init(app);
    if(pdh_init_query(app) != ERROR_SUCCESS) {
        gui_log(app, "[ERROR] Failed to initialize PDH for CPU monitoring.\n");
    }
#endif

    // If real-time CSV logging is enabled, write the header row.
    if(app->csv_realtime_en) {
        log_csv_header(app);
    }

    while (atomic_load(&app->running)){
        // Select the correct sampling functions based on the OS
#ifndef _WIN32
        sample_cpu_linux(app);
        sample_temp_linux(app);
#else
        sample_cpu_windows(app);
        sample_temp_windows(app);
#endif
        // Request the UI thread to redraw the graph widgets
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, app->cpu_drawing);
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, app->iters_drawing);
        
        // If enabled, write the current sample to the CSV log.
        if (app->csv_realtime_en) {
            log_csv_sample(app);
        }

        // Advance the circular buffer for the performance history graph
        g_mutex_lock(&app->history_mutex);
        app->history_pos = (app->history_pos + 1) % app->history_len;
        if (app->thread_history){
            for (int t=0; t<app->threads; t++){
                // Zero out the current position for the next sampling cycle.
                // The value will be filled in by the worker thread.
                app->thread_history[t][app->history_pos] = 0;
            }
        }
        g_mutex_unlock(&app->history_mutex);

        // Wait for the defined sample interval
        struct timespec r = {0, CPU_SAMPLE_INTERVAL_MS * 1000000};
        nanosleep(&r,NULL);
    }

#ifdef _WIN32
    // Clean up Windows-specific handles
    pdh_close_query(app);
    wmi_deinit(app);
#endif
}

/* --- Data Collection Implementations --- */

int detect_cpu_count(void){
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1;
#endif
}

/**
 * @brief Reads CPU time statistics from /proc/stat.
 * @param out An array of cpu_sample_t to store the parsed data.
 * @param maxcpu The maximum number of CPUs to read data for.
 * @return The number of CPUs read, or -1 on failure.
 */
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


/**
 * @brief Calculates CPU usage percentage between two samples.
 * @param a The first (earlier) CPU time sample.
 * @param b The second (later) CPU time sample.
 * @return The CPU usage as a value between 0.0 and 1.0.
 */
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

/**
 * @brief Samples CPU usage on Linux.
 *
 * Takes two snapshots of /proc/stat with a short delay and computes the
 * differential usage for each core.
 */

static void sample_cpu_linux(AppContext *app) {
    int n = app->cpu_count;
    if (n <= 0 || !app->prev_cpu_samples) return;

    cpu_sample_t *current_samples = calloc(n, sizeof(cpu_sample_t));
    if (!current_samples) return;

    if (read_proc_stat(current_samples, n, "/proc/stat") <= 0) {
        free(current_samples);
        return;
    }

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

    memcpy(app->prev_cpu_samples, current_samples, n * sizeof(cpu_sample_t));
    free(current_samples);
}

/**
 * @brief Samples CPU temperature on Linux by running `sensors`.
 *
 * Parses the output of `sensors -u` to find the first available thermal
 * sensor reading. Requires the `lm-sensors` package to be installed.
 */
static void sample_temp_linux(AppContext *app){
    FILE *p = popen("sensors -u 2>/dev/null", "r");
    if (!p){ g_mutex_lock(&app->temp_mutex); app->temp_celsius = TEMP_UNAVAILABLE; g_mutex_unlock(&app->temp_mutex); return; }
    char line[256];
    double found = TEMP_UNAVAILABLE;
    while (fgets(line, sizeof(line), p)){
        // Look for the first line ending in "_input:", which typically indicates a temp sensor.
        char *k = strstr(line, "_input:");
        if (k){
            double v;
            if (sscanf(k+7, "%lf", &v) == 1){
                found = v;
                break; // Use the first one found
            }
        }
    }
    pclose(p);
    g_mutex_lock(&app->temp_mutex); app->temp_celsius = found; g_mutex_unlock(&app->temp_mutex);
}

#else /* --- WINDOWS IMPLEMENTATION --- */

/**
 * @brief Initializes the PDH (Performance Data Helper) query for CPU usage.
 *
 * Sets up a PDH query and adds a counter for "% Processor Time" for each
 * logical processor on the system.
 */
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
            // Cleanup on failure
            for (int j = 0; j < i; j++) PdhRemoveCounter(app->pdh_counters[j]);
            free(app->pdh_counters); app->pdh_counters = NULL;
            PdhCloseQuery(app->pdh_query); app->pdh_query = NULL;
            return -1;
        }
    }
    return PdhCollectQueryData(app->pdh_query); // Initial collection
}

/**
 * @brief Closes the PDH query and frees associated resources.
 */
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

/**
 * @brief Samples CPU usage on Windows using the PDH library.
 */
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

/**
 * @brief Initializes COM and connects to the WMI service for temperature monitoring.
 */
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

/**
 * @brief Deinitializes COM and releases WMI resources.
 */
static void wmi_deinit(AppContext *app) {
    if(app->pSvc) { app->pSvc->lpVtbl->Release(app->pSvc); app->pSvc = NULL; }
    if(app->pLoc) { app->pLoc->lpVtbl->Release(app->pLoc); app->pLoc = NULL; }
    CoUninitialize();
}

/**
 * @brief Samples CPU temperature on Windows by querying WMI.
 *
 * Queries the `MSAcpi_ThermalZoneTemperature` class to get a temperature
 * reading. The value is returned in tenths of a Kelvin and is converted to Celsius.
 */
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
            // The temperature is in tenths of a Kelvin.
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

/**
 * @brief Writes the header row to the real-time CSV log file.
 */
static void log_csv_header(AppContext *app) {
    if (!app->csv_log_file) return;
    fprintf(app->csv_log_file, "timestamp");
    for (int c = 0; c < app->cpu_count; c++) fprintf(app->csv_log_file, ",cpu%d_usage", c);
    for (int t = 0; t < app->threads; t++) fprintf(app->csv_log_file, ",thread%d_iters_total", t);
    fprintf(app->csv_log_file, ",temp_celsius\n");
    fflush(app->csv_log_file);
}

/**
 * @brief Writes a single data sample row to the real-time CSV log file.
 */
static void log_csv_sample(AppContext *app) {
    if (!app->csv_log_file) return;
    
    // Get current timestamp
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double timestamp = ts.tv_sec + ts.tv_nsec * 1e-9;
    fprintf(app->csv_log_file, "%.3f", timestamp);

    // Log CPU usage for each core
    g_mutex_lock(&app->cpu_mutex);
    for (int c = 0; c < app->cpu_count; c++) fprintf(app->csv_log_file, ",%.6f", app->cpu_usage[c]);
    g_mutex_unlock(&app->cpu_mutex);

    // The history buffer stores the total iteration count, which is what we want to log.
    g_mutex_lock(&app->history_mutex);
    int current_pos = app->history_pos; // The position that was just updated
    if(app->thread_history) {
      for (int t = 0; t < app->threads; t++) fprintf(app->csv_log_file, ",%u", app->thread_history[t][current_pos]);
    }
    g_mutex_unlock(&app->history_mutex);
    
    // Log temperature
    g_mutex_lock(&app->temp_mutex);
    fprintf(app->csv_log_file, ",%.3f\n", app->temp_celsius);
    g_mutex_unlock(&app->temp_mutex);
    
    fflush(app->csv_log_file);
}
