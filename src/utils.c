#include "utils.h"
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#include <string.h>
#endif

/* --- Platform-Independent Utility Functions --- */

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
    if (a == NULL || n <= 1) return;
    for (size_t i = n - 1; i > 0; --i){
        size_t j = (size_t)(splitmix64(seed) % (i + 1));
        uint32_t tmp = a[i]; a[i] = a[j]; a[j] = tmp;
    }
}

unsigned long long get_total_system_memory() {
#ifdef _WIN32
    // On Windows, use the GlobalMemoryStatusEx function to get memory info.
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullTotalPhys;
    }
    return 0;
#else
    // On Linux, parse the MemTotal field from /proc/meminfo.
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
            return total_mem_kb * 1024; // Convert from KB to bytes
        }
    }
    fclose(meminfo);
    return 0;
#endif
}


/* --- Thread Abstraction Implementation --- */

#ifdef _WIN32
// Windows-specific implementation using the Win32 API
#include <process.h> 

int thread_create(thread_handle_t *t, thread_func_t func, void *arg) {
    // _beginthreadex is the recommended way to create threads on Windows for C runtime compatibility.
    *t = (HANDLE)_beginthreadex(NULL, 0, func, arg, 0, NULL);
    return (*t == NULL) ? -1 : 0;
}
int thread_join(thread_handle_t t) {
    if(t) {
        // Wait for the thread to finish and then close the handle.
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    return 0;
}
int thread_detach(thread_handle_t t) {
    // On Windows, "detaching" is achieved by simply closing the handle.
    // The thread will continue to run, and its resources will be freed by the OS on termination.
    if(t) CloseHandle(t); 
    return 0;
}
#else
// POSIX-specific implementation using pthreads
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
