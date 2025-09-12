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
typedef struct { unsigned long long user,nice,system,idle,iowait,irq,softirq,steal; } cpu_sample_t;
#endif

#include <gtk/gtk.h>
#include <cairo.h>

/** @file hardstress.h
 *  @brief Central header file for the HardStress utility.
 *
 *  This file defines the core data structures, constants, and function prototypes
 *  used throughout the application. It includes platform-specific headers,
 *  defines a cross-platform thread abstraction layer, and declares the main
 *  `AppContext` struct that encapsulates the application's state.
 */

/* --- CONFIGURATION CONSTANTS --- */
#define DEFAULT_MEM_MIB 256             ///< Default memory to allocate per worker thread in MiB.
#define DEFAULT_DURATION_SEC 300        ///< Default stress test duration in seconds (5 minutes).
#define CPU_SAMPLE_INTERVAL_MS 1000     ///< Interval for sampling CPU usage and temperature in milliseconds.
#define HISTORY_SAMPLES 240             ///< Number of historical data points to store for performance graphs.
#define ITER_SCALE 1000.0               ///< Divisor for scaling iteration counts for display.
#define TEMP_UNAVAILABLE -274.0         ///< Sentinel value indicating that temperature data is not available.

/* --- THEME --- */
/** @struct color_t
 *  @brief Represents an RGB color for use in the Cairo-drawn UI.
 */
typedef struct { double r, g, b; } color_t;
extern const color_t COLOR_BG, COLOR_FG, COLOR_WARN, COLOR_ERR, COLOR_TEXT, COLOR_TEMP; ///< Global color constants.

/* --- THREAD ABSTRACTION --- */
#ifdef _WIN32
typedef HANDLE thread_handle_t;         ///< Type definition for a thread handle (Windows).
typedef unsigned (__stdcall *thread_func_t)(void *); ///< Type definition for a thread function (Windows).
#else
typedef pthread_t thread_handle_t;      ///< Type definition for a thread handle (POSIX).
typedef void *(*thread_func_t)(void *); ///< Type definition for a thread function (POSIX).
#endif


/* --- FORWARD DECLARATIONS --- */
typedef struct AppContext AppContext;
typedef struct worker_t worker_t;

/* --- WORKER --- */
/**
 * @enum worker_status_t
 * @brief Represents the status of a worker thread.
 */
typedef enum {
    WORKER_OK = 0,          ///< Worker is operating normally.
    WORKER_ALLOC_FAIL       ///< Worker failed to allocate its memory buffer.
} worker_status_t;

/**
 * @struct worker_t
 * @brief Encapsulates the state and resources for a single stress-testing worker thread.
 */
struct worker_t {
    int tid;                ///< Thread ID (0 to N-1).
    size_t buf_bytes;       ///< Size of the memory buffer to allocate, in bytes.
    uint8_t *buf;           ///< Pointer to the allocated memory buffer for memory access patterns.
    uint32_t *idx;          ///< Array of indices for randomized memory access.
    size_t idx_len;         ///< Number of elements in the `idx` array.
    atomic_int running;     ///< Flag to signal the thread to continue running or terminate.
    atomic_uint iters;      ///< Counter for the number of iterations completed.
    atomic_int status;      ///< The status of the worker (e.g., `WORKER_OK`).
    AppContext *app;        ///< A pointer back to the main application context.
};

/* --- APP CONTEXT --- */
/**
 * @struct AppContext
 * @brief Encapsulates the entire state of the HardStress application.
 *
 * This structure holds all configuration, real-time state, thread management
 * resources, data buffers, and GUI widgets for the application. Passing a pointer
 * to this struct avoids the use of global variables.
 */
struct AppContext {
    /* --- Configuration (set from UI) --- */
    int threads;                    ///< Number of worker threads to spawn.
    size_t mem_mib_per_thread;      ///< Memory to allocate per thread (in MiB).
    int duration_sec;               ///< Total test duration in seconds (0 for indefinite).
    int pin_affinity;               ///< Boolean flag to enable CPU core pinning.
    int kernel_fpu_en;              ///< Boolean flag to enable the FPU stress kernel.
    int kernel_int_en;              ///< Boolean flag to enable the integer stress kernel.
    int kernel_stream_en;           ///< Boolean flag to enable the memory streaming kernel.
    int kernel_ptr_en;              ///< Boolean flag to enable the pointer-chasing kernel.
    int csv_realtime_en;            ///< Boolean flag to enable real-time CSV logging.

    /* --- Runtime State --- */
    atomic_int running;             ///< Flag indicating if a stress test is currently active.
    atomic_int errors;              ///< Counter for errors encountered during the test.
    atomic_uint total_iters;        ///< Aggregated iteration count across all threads.
    double start_time;              ///< Timestamp (from `now_sec`) when the test started.
    FILE *csv_log_file;             ///< File handle for the CSV export file.

    /* --- Workers & Threads --- */
    worker_t *workers;              ///< Array of worker thread contexts.
    thread_handle_t *worker_threads;///< Array of handles for the worker threads.
    thread_handle_t cpu_sampler_thread; ///< Handle for the metrics sampler thread.
    thread_handle_t controller_thread;  ///< Handle for the main test controller thread.

    /* --- CPU Usage Monitoring --- */
    int cpu_count;                  ///< Number of logical CPU cores detected.
    double *cpu_usage;              ///< Array to store the utilization of each CPU core (0.0 to 1.0).
    GMutex cpu_mutex;               ///< Mutex to protect access to the `cpu_usage` array.
#ifdef _WIN32
    /* --- Windows-specific handles for performance monitoring --- */
    PDH_HQUERY pdh_query;           ///< A query handle for the Performance Data Helper (PDH) library.
    PDH_HCOUNTER *pdh_counters;     ///< An array of counter handles for individual CPU cores.
    IWbemServices *pSvc;            ///< A pointer to the WMI services for temperature querying.
    IWbemLocator *pLoc;             ///< A pointer to the WMI locator for connecting to WMI.
#else
    cpu_sample_t *prev_cpu_samples; ///< A buffer to store the previous CPU sample for calculating usage delta.
#endif

    /* --- Per-Thread Performance History --- */
    unsigned **thread_history;      ///< A 2D circular buffer for storing per-thread iteration history.
    int history_pos;                ///< The current write position in the circular buffer.
    int history_len;                ///< The number of valid samples currently in the buffer.
    GMutex history_mutex;           ///< Mutex to protect access to the history buffer.

    /* --- Temperature Monitoring --- */
    double temp_celsius;            ///< The last measured CPU temperature in degrees Celsius.
    GMutex temp_mutex;              ///< Mutex to protect access to `temp_celsius`.

    /* --- GUI Widgets --- */
    GtkWidget *win;                 ///< The main application window.
    GtkWidget *entry_threads, *entry_mem, *entry_dur; ///< Input fields for test parameters.
    GtkWidget *check_pin;           ///< Checkbox for enabling CPU pinning.
    GtkWidget *check_fpu, *check_int, *check_stream, *check_ptr; ///< Checkboxes for stress kernels.
    GtkWidget *check_csv_realtime;  ///< Checkbox for real-time CSV logging.
    GtkWidget *btn_start, *btn_stop, *btn_save_metrics, *btn_defaults, *btn_clear_log; ///< Control buttons.
    GtkTextBuffer *log_buffer;      ///< Text buffer for the event log panel.
    GtkWidget *log_view;            ///< Text view widget for the event log.
    GtkWidget *cpu_drawing;         ///< Drawing area for the per-core CPU utilization graph.
    GtkWidget *iters_drawing;       ///< Drawing area for the per-thread performance history graph.
    GtkWidget *status_label;        ///< Label for displaying the current application status.
    GtkWidget *mem_warning_label;   ///< Label to warn about high memory allocation.
};

/* --- FUNCTION PROTOTYPES --- */

/* --- ui.c --- */
// Prototypes are now in ui.h

/* --- core.c --- */
// Prototypes are now in core.h

/* --- metrics.c --- */
// Prototypes are now in metrics.h

/* --- utils.c --- */
// Prototypes are now in utils.h

#endif // HARDSTRESS_H
