# HardStress: Advanced Utility for Stress Testing and System Monitoring

## 1. Overview

**HardStress** is a high-performance software utility designed to subject computing systems to intensive CPU and memory workloads. Developed in C, with a graphical user interface (GUI) implemented using the GTK3 toolkit, the tool provides real-time system performance monitoring through graphs rendered with the Cairo library. Its cross-platform architecture ensures compatibility with POSIX-based operating systems (such as Linux) and Windows, utilizing native concurrency abstractions (`pthreads` and Windows API, respectively). The primary objective of HardStress is to provide developers, system analysts, and hardware enthusiasts with a robust tool for stability validation, thermal performance analysis, and the identification of performance bottlenecks under sustained stress.

## 2. Key Features

* **Multi-threaded CPU Stress Testing:** Ability to launch a configurable number of worker threads to fully utilize all available processing cores.
* **Intensive Memory Allocation:** Each worker thread allocates and operates on a user-defined memory buffer, stressing the memory subsystem and memory controller.
* **CPU Affinity (Thread Pinning):** Option to bind each worker thread to a specific CPU core, enabling more consistent and controlled performance tests by eliminating scheduler-induced thread migration overhead.
* **Real-time Data Visualization:**

  * **CPU Utilization Graph:** Displays aggregated CPU usage over time, allowing immediate observation of test impact.
  * **Thread Iteration Graph:** Monitors and plots the number of operations (iterations) completed by each thread individually, helping to detect performance inconsistencies across cores or issues like thermal throttling.
* **Temperature Monitoring:** Integration with system hardware sensors to display real-time CPU temperature (requires `lm-sensors` on Linux or WMI access on Windows).
* **Configurable Parameters:** Users can easily define the number of threads, memory per thread (in MiB), and total test duration in seconds.
* **Data Export:** Functionality to export collected performance data to CSV format, facilitating further analysis with external tools.
* **Log Panel:** An integrated log panel within the GUI reports key events, such as test initiation and termination, resource allocation, and possible errors.

## 3. Architecture and Implementation Details

The software is structured around a central `AppContext`, which manages the global application state, including test configurations, thread handles, data structures for graphing, and GUI components.

### 3.1. Concurrency Model

The workload is distributed across multiple "worker" threads. On POSIX systems, the program uses the `pthread` library for thread creation and management. On Windows, it interfaces directly with the Win32 API. The use of `_Atomic` types (`stdatomic.h`) for global counters (such as total iterations and errors) ensures data consistency and thread-safety for simple operations without requiring heavy mutexes.

### 3.2. Stress Kernel

Each worker thread executes an intensive computational loop involving floating-point operations, integer manipulations, and sequential memory accesses within the allocated buffer. This design ensures simultaneous stress on multiple CPU components (FPU, ALU) and the memory bus.

### 3.3. Metrics Collection

* **CPU Usage:** On Linux, CPU usage is calculated by reading and processing data from `/proc/stat`. On Windows, the Performance Data Helper library (`pdh.lib`) is used to query system performance counters. The `compute_usage` function implements differential sampling between two time points to determine the percentage of non-idle time.
* **Performance History:** The application maintains a circular buffer (`thread_iters_history`) that stores iteration counts for each thread over the last `HISTORY_SAMPLES` (240) samples. This buffer is used by Cairo's drawing routines to render historical performance graphs.

### 3.4. Graphical User Interface

The interface is entirely built with GTK3 and designed to be intuitive. Graphs are rendered directly onto drawing areas (`GtkDrawingArea`) using the Cairo graphics library, without relying on third-party widgets. This approach provides full control over the appearance and performance of data visualizations.

## 4. Prerequisites

### Linux (Debian/Ubuntu)

A C compiler and the GTK3 development libraries are required:

```bash
sudo apt update
sudo apt install build-essential libgtk-3-dev
```

Optionally, for temperature monitoring, install `lm-sensors`:

```bash
sudo apt install lm-sensors
```

### Windows (using MSYS2)

Install the MSYS2 environment and, from its terminal, install the MinGW-w64 toolchain and GTK3 libraries:

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3
```

## 5. Building and Running

The project includes a `Makefile` to simplify the build process on both platforms.

1. Clone the repository:

   ```bash
   git clone https://github.com/felipebehling/Hardstress.git
   cd HardStress
   ```

2. Compile the program:

   ```bash
   make
   ```

3. Run the application:

   ```bash
   ./hardstress
   ```

## 6. Usage Guide

1. **Configure Parameters:**

   * **Threads:** Set the number of stress threads. A good starting point is the number of logical CPU cores.
   * **Mem/Thread (MiB):** Specify the amount of RAM, in Mebibytes, to be allocated by each thread.
   * **Duration (s):** Define the duration of the test in seconds. Set to 0 for an indefinite test (until manually stopped).
   * **Pin Threads to Cores:** Enable this option to bind threads to specific CPU cores.

2. **Start the Test:** Click the "Start" button. Graphs and counters will begin updating in real-time.

3. **Stop the Test:** Click the "Stop" button at any time to interrupt the test. If a duration was set, the test will stop automatically.

4. **Export Results:** After the test ends, click "Export CSV" to save the performance data.

## 7. Dependencies

* **GTK3:** For the graphical user interface.
* **Cairo:** For 2D graph rendering.
* **pthreads:** (Linux/POSIX) For multi-threading support.
* **PDH Library:** (Windows) For CPU performance monitoring.
