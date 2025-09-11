#ifndef METRICS_H
#define METRICS_H

#include "hardstress.h"

void cpu_sampler_thread_func(void *arg);
int detect_cpu_count(void);

#ifndef _WIN32
int read_proc_stat(cpu_sample_t *out, int maxcpu, const char *path);
double compute_usage(const cpu_sample_t *a, const cpu_sample_t *b);
#endif

#endif // METRICS_H
