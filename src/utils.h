#ifndef UTILS_H
#define UTILS_H

#include "hardstress.h"

double now_sec(void);
uint64_t splitmix64(uint64_t *x);
void shuffle32(uint32_t *a, size_t n, uint64_t *seed);
unsigned long long get_total_system_memory(void);

int thread_create(thread_handle_t *t, thread_func_t func, void *arg);
int thread_join(thread_handle_t t);
int thread_detach(thread_handle_t t);

#endif // UTILS_H
