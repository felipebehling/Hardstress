#ifndef METRICS_H
#define METRICS_H

/**
 * @file metrics.h
 * @brief Declares functions for collecting system performance metrics.
 *
 * This module is responsible for gathering real-time data such as CPU
 * utilization per core, system temperature, and detecting the number of
 * available CPU cores. It provides cross-platform implementations for
 * Linux and Windows.
 */

#include "hardstress.h"

/**
 * @brief The main function for the metrics sampler thread.
 *
 * This thread runs in the background during a stress test, periodically
 * sampling CPU utilization and temperature. After collecting the data, it
 * triggers a redraw of the relevant GUI graphs to provide real-time feedback.
 * It is also responsible for advancing the history buffer position.
 *
 * @param arg A pointer to the global `AppContext` structure.
 */
void cpu_sampler_thread_func(void *arg);

/**
 * @brief Detects the number of logical CPU cores on the system.
 *
 * This function provides a cross-platform way to determine the number of
 * processors, using `sysconf` on POSIX systems and `GetSystemInfo` on Windows.
 *
 * @return The number of logical CPU cores available.
 */
int detect_cpu_count(void);

#ifndef _WIN32
int read_proc_stat(cpu_sample_t *out, int maxcpu, const char *path);
double compute_usage(const cpu_sample_t *a, const cpu_sample_t *b);
#endif

#endif // METRICS_H
