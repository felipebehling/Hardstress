# HardStress: Advanced CPU and Memory Stress-Testing Utility with Real-Time Monitoring

## 1\. Abstract

**HardStress** is a high-performance, cross-platform software utility engineered to subject computational systems to intensive, sustained CPU and memory workloads. Developed in C with a graphical user interface (GUI) rendered by the GTK3 toolkit and Cairo graphics library, the application provides sophisticated, real-time monitoring of key performance metrics. Its architecture ensures portability between POSIX-compliant operating systems (e.g., Linux) and Microsoft Windows by leveraging native concurrency abstractions (`pthreads` and the Win32 API, respectively). The primary objective of HardStress is to furnish developers, system analysts, and hardware engineers with an empirical tool for stability validation, thermal performance assessment, and the identification of performance bottlenecks under extreme operational conditions.

## 2\. Core Features

  * **Multi-threaded Stress Kernel:** Launches a user-configurable number of worker threads to achieve maximal utilization of all available processor cores. The computational kernel executes a heterogeneous mix of floating-point, integer, and memory-streaming operations to stress the FPU, ALUs, and system memory bus concurrently.
  * **Configurable Workloads:** Allows precise definition of test parameters, including the number of threads, memory allocation per thread (in MiB), and total test duration. A duration of zero enables indefinite operation until manually terminated.
  * **CPU Core Affinity (Pinning):** Provides an option to bind each worker thread to a specific CPU core, mitigating scheduler-induced thread migration and enabling more consistent, deterministic performance analysis.
  * **Real-time Data Visualization:**
      * **Per-Core CPU Utilization Graph:** Renders a real-time bar chart displaying the utilization percentage for each individual CPU core.
      * **Per-Thread Performance History Graph:** A novel line chart that plots the historical iteration count for each worker thread over a sliding time window. This visualization is critical for identifying performance degradation, cross-core inconsistencies, and the effects of thermal throttling.
  * **System Temperature Monitoring:** Integrates with hardware sensors to display real-time CPU temperature, providing crucial data for thermal analysis (requires `lm-sensors` on Linux or WMI access on Windows).
  * **Performance Data Export:** Features functionality to export all collected time-series data—including timestamps, per-core CPU usage, per-thread iteration counts, and temperature—to a CSV file for advanced offline analysis and reporting.
  * **Integrated Event Logging:** A dedicated panel within the GUI reports key operational events, including test initiation, termination, resource allocation failures, and other diagnostic messages.

## 3\. System Architecture and Implementation

The software is architected around a central `AppContext` structure, which encapsulates the entire application state, thereby avoiding global variables and promoting modularity.

### 3.1. Concurrency Model

HardStress employs a multi-threaded architecture to manage its workload and maintain a responsive user interface.

  * **Controller Thread:** Upon test initiation, a dedicated "controller" thread is spawned in a detached state. This thread orchestrates the entire lifecycle of the stress test: it allocates resources, creates and manages worker threads, monitors the test duration, and performs cleanup. This design prevents the GUI thread from blocking during intensive setup or teardown operations.
  * **Worker Threads:** These threads execute the primary stress-testing logic. Their lifecycle is managed entirely by the controller thread.
  * **Sampler Thread:** A separate thread periodically collects CPU utilization and temperature data. It operates independently and communicates with the GUI thread asynchronously to trigger screen redraws.
  * **Thread Safety:** Data consistency for shared counters (e.g., total iterations, errors) is ensured through the use of atomic types from `<stdatomic.h>`, while access to complex shared data structures (e.g., CPU usage arrays, history buffers) is synchronized using `GMutex`.

### 3.2. Metrics Collection and Storage

  * **CPU Usage:** On Linux, utilization metrics are derived by parsing `/proc/stat`. On Windows, the application is designed to interface with the Performance Data Helper (PDH) library. A differential sampling algorithm is implemented in `compute_usage` to calculate the percentage of non-idle time between two points.
  * **Performance History:** The application maintains a circular buffer for each worker thread, storing the last `HISTORY_SAMPLES` (240) iteration counts. This data structure efficiently provides the time-series data required for rendering the historical performance graphs.
  * **Temperature:** On Linux, temperature is obtained by parsing the output of the `sensors -u` command. On Windows, it is queried via PowerShell, which interfaces with the `MSAcpi_ThermalZoneTemperature` WMI class.

### 3.3. Graphical User Interface

The GUI is constructed using the GTK3 toolkit. All data visualizations are custom-drawn on `GtkDrawingArea` widgets using the Cairo 2D graphics library, which affords complete control over the rendering pipeline and visual aesthetics.

## 4\. Build and Execution

### 4.1. Prerequisites

#### Linux (Debian/Ubuntu)

A C compiler (`build-essential`) and the GTK3 development libraries are required.

```bash
sudo apt update
sudo apt install build-essential libgtk-3-dev
```

For temperature monitoring, `lm-sensors` is recommended:

```bash
sudo apt install lm-sensors
```

#### Windows (with MSYS2)

Install the MSYS2 environment. From the MSYS2 MINGW64 terminal, install the necessary toolchain and libraries:

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 pkg-config
```

### 4.2. Building from Source

The project includes a `Makefile` that automates the compilation process.

1.  **Clone the repository:**

    ```bash
    git clone https://github.com/felipebehling/Hardstress.git
    cd Hardstress
    ```

2.  **Compile:**

      * For a standard debug build:
        ```bash
        make
        ```
      * For a high-performance release build (with `-O3` and native architecture optimizations):
        ```bash
        make release
        ```

3.  **Run:**

    ```bash
    ./HardStress
    ```

### 4.3. Pre-compiled Releases

For convenience, a GitHub Actions workflow automatically builds and packages the application for both Linux and Windows upon every new version tag. These binaries are available for download from the "Releases" section of the GitHub repository.

### 4.4. Important Note for Windows Users

When downloading and attempting to run the pre-compiled version for Windows, it is likely that Windows Defender SmartScreen will display a security alert, preventing immediate execution.

**Why does this happen?**
This alert is a standard Windows security measure for applications from unrecognized developers. As this is an open-source project and the executable is not digitally signed with an expensive certificate, Windows has no way to verify its origin and treats it with caution.

**Is the program safe?**
Yes. The source code is fully available in this repository for auditing. The executable in the Release is compiled directly from this source code through an automated and public process (GitHub Actions).

**How to run the program?**
1.  Click on **"More info"** in the SmartScreen alert.
2.  Then, the **"Run anyway"** button will appear. Click it.

If you prefer not to run the pre-compiled binary, feel free to compile the project directly from the source code by following the instructions in the "Building from Source" section.

## 5\. Usage Guide

1.  **Configure Parameters:**
      * **Threads:** Set the number of worker threads (e.g., the number of logical CPU cores).
      * **Mem (MiB/thread):** Specify the RAM allocation for each thread.
      * **Duration (s):** Define the test length in seconds (0 for indefinite).
      * **Pin threads to CPUs:** Enable CPU affinity.
2.  **Initiate Test:** Click the "Start" button.
3.  **Monitor:** Observe real-time data on the CPU and iteration graphs.
4.  **Terminate Test:** Click "Stop" to end the test manually. The test will also stop automatically if a duration was set.
5.  **Export Data:** After a test run, click "Export CSV" to save the collected metrics.

## 6\. Dependencies

  * **GTK3:** GUI Toolkit.
  * **Cairo:** 2D Graphics Rendering.
  * **pthreads:** (Linux/POSIX) Concurrency support.
  * **PDH Library:** (Windows) System performance counters.

## 7\. License

This project is licensed under the MIT License. See the `LICENSE` file for full details.
