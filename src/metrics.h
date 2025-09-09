#ifndef METRICS_H
#define METRICS_H

#include "hardstress.h"

void cpu_sampler_thread_func(void *arg);
int detect_cpu_count(void);

#endif // METRICS_H
