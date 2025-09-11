#ifndef CORE_H
#define CORE_H

/**
 * @file core.h
 * @brief Declares the core stress-testing logic.
 *
 * This file contains the declaration for the main controller thread function,
 * which is the entry point for starting and managing a stress test session.
 */

#include "hardstress.h"

/**
 * @brief The main function for the test controller thread.
 *
 * This function orchestrates the entire lifecycle of a stress test. It is
 * launched in a separate thread when the user clicks "Start". Its
 * responsibilities include:
 * - Initializing application state for the test.
 * - Allocating resources (memory for workers, history buffers, etc.).
 * - Creating and launching the worker threads and the metrics sampler thread.
 * - Pinning worker threads to CPU cores if requested.
 * - Monitoring the test duration and stopping the test when complete.
 * - Cleaning up all resources and signaling the UI when the test is finished.
 *
 * @param arg A pointer to the global `AppContext` structure.
 */
void controller_thread_func(void *arg);

#endif // CORE_H
