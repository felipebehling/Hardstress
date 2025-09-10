#include "utils.h"
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#include <string.h>
#endif

// Funções que não dependem do sistema operacional
double now_sec(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec*1e-9;
}

uint64_t splitmix64(uint64_t *x){
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void shuffle32(uint32_t *a, size_t n, uint64_t *seed){
    if (n <= 1) return;
    for (size_t i = n - 1; i > 0; --i){
        size_t j = (size_t)(splitmix64(seed) % (i + 1));
        uint32_t tmp = a[i]; a[i] = a[j]; a[j] = tmp;
    }
}

unsigned long long get_total_system_memory() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullTotalPhys;
    }
    return 0;
#else
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if (meminfo == NULL) {
        return 0;
    }
    char line[256];
    while (fgets(line, sizeof(line), meminfo)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            unsigned long long total_mem_kb;
            sscanf(line + 9, "%llu", &total_mem_kb);
            fclose(meminfo);
            return total_mem_kb * 1024;
        }
    }
    fclose(meminfo);
    return 0;
#endif
}


/* --- Implementação da Abstração de Threads --- */

#ifdef _WIN32
// MODIFICADO: O include foi movido para dentro deste bloco #ifdef
#include <process.h> 

int thread_create(thread_handle_t *t, thread_func_t func, void *arg) {
    *t = (HANDLE)_beginthreadex(NULL, 0, func, arg, 0, NULL);
    return (*t == NULL) ? -1 : 0;
}
int thread_join(thread_handle_t t) {
    if(t) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    return 0;
}
int thread_detach(thread_handle_t t) {
    if(t) CloseHandle(t); 
    return 0;
}
#else
// Código específico para Linux e outros sistemas POSIX (não modificado)
int thread_create(thread_handle_t *t, thread_func_t func, void *arg) {
    return pthread_create(t, NULL, func, arg);
}
int thread_join(thread_handle_t t) {
    return pthread_join(t, NULL);
}
int thread_detach(thread_handle_t t) {
    return pthread_detach(t);
}
#endif
