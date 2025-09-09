#include "utils.h"
#include <process.h>

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


/* --- Thread Abstraction Implementation --- */
#ifdef _WIN32
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
    // Detaching é implícito com _beginthreadex, apenas fechamos o handle para evitar leaks.
    if(t) CloseHandle(t); 
    return 0;
}
#else
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
