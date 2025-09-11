#ifndef UTILS_H
#define UTILS_H

/**
 * @file utils.h
 * @brief Provides utility functions and a cross-platform thread abstraction layer.
 *
 * This file declares various helper functions for timekeeping, random number
 * generation, and system information querying. It also defines a wrapper around
 * pthreads and the Win32 API to provide a consistent interface for thread
 * management across different operating systems.
 */

#include "hardstress.h"

/**
 * @brief Gets the current time as a high-resolution timestamp in seconds.
 *
 * This function uses a monotonic clock to provide a steady, high-precision
 * time measurement that is not affected by system time changes.
 *
 * @return The current time in seconds, with microsecond or better precision.
 */
double now_sec(void);

/**
 * @brief A fast, high-quality 64-bit pseudo-random number generator (PRNG).
 *
 * This implements the `splitmix64` algorithm, which is known for its speed
 * and good statistical properties. It's used as the seeding mechanism for
 * other operations.
 *
 * @param x A pointer to the 64-bit state variable for the PRNG. This state
 *          is updated on each call.
 * @return The next 64-bit pseudo-random number in the sequence.
 */
uint64_t splitmix64(uint64_t *x);

/**
 * @brief Shuffles an array of 32-bit integers using the Fisher-Yates algorithm.
 *
 * This function performs an in-place shuffle of the given array, ensuring
 * a uniform random permutation of its elements.
 *
 * @param a The array of 32-bit integers to shuffle.
 * @param n The number of elements in the array.
 * @param seed A pointer to the 64-bit seed state used by the `splitmix64` PRNG.
 */
void shuffle32(uint32_t *a, size_t n, uint64_t *seed);

/**
 * @brief Retrieves the total amount of physical RAM on the system.
 *
 * This function is cross-platform, using `/proc/meminfo` on Linux and
 * `GlobalMemoryStatusEx` on Windows.
 *
 * @return The total physical memory in bytes. Returns 0 on failure.
 */
unsigned long long get_total_system_memory(void);

/**
 * @brief Creates a new thread (cross-platform).
 *
 * This is a wrapper around `pthread_create` (POSIX) and `_beginthreadex` (Windows).
 *
 * @param t Pointer to a `thread_handle_t` where the new thread's handle will be stored.
 * @param func The function the new thread will execute.
 * @param arg The argument to pass to the thread function.
 * @return 0 on success, non-zero on failure.
 */
int thread_create(thread_handle_t *t, thread_func_t func, void *arg);

/**
 * @brief Waits for a specific thread to terminate and cleans up its resources (cross-platform).
 *
 * This is a wrapper around `pthread_join` (POSIX) and `WaitForSingleObject` / `CloseHandle` (Windows).
 *
 * @param t The handle of the thread to join.
 * @return 0 on success, non-zero on failure.
 */
int thread_join(thread_handle_t t);

/**
 * @brief Detaches a thread, allowing it to run independently and have its resources freed on termination (cross-platform).
 *
 * This is a wrapper around `pthread_detach` (POSIX) and `CloseHandle` (Windows).
 * Note: On Windows, detaching simply closes the handle, allowing the thread to run,
 * but the OS manages resource cleanup.
 *
 * @param t The handle of the thread to detach.
 * @return 0 on success, non-zero on failure.
 */
int thread_detach(thread_handle_t t);

#endif // UTILS_H
