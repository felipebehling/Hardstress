HardStress: Advanced System Stability & Performance Diagnostics



HardStress is an elite, cross-platform diagnostic utility engineered for subjecting computational systems to extreme, sustained workloads. Meticulously crafted in C, it provides an empirical toolkit for system analysts, hardware engineers, and performance enthusiasts to validate system stability, analyze thermal dynamics, and uncover performance bottlenecks with precision.
Its sophisticated architecture leverages native concurrency models—pthreads on POSIX systems and the Win32 API on Windows—to achieve maximum hardware saturation. The intuitive graphical interface, rendered by GTK3 and Cairo, offers a real-time, high-fidelity view into the heart of your machine's performance.
Core Capabilities
HardStress integrates a suite of advanced features designed to provide deep insights into system behavior under duress.
Sophisticated Stress Engine
 * Multi-Threaded Kernel: Deploys a user-defined number of worker threads to fully utilize every available processor core.
 * Heterogeneous Workloads: Executes a carefully balanced mix of floating-point, integer, and memory-streaming operations to concurrently stress the FPU, ALUs, and the system memory bus.
 * CPU Core Affinity: An optional feature to "pin" each worker thread to a specific CPU core. This mitigates OS scheduler migrations, ensuring consistent and reproducible performance analysis.
Advanced Real-Time Monitoring
 * Per-Core Utilization: A dynamic bar chart visualizes the real-time utilization percentage of each individual CPU core.
 * Per-Thread Performance History: A groundbreaking line chart plots the operational throughput (iterations) of each thread over time. This view is indispensable for identifying performance degradation, cross-core inconsistencies, and the subtle effects of thermal throttling. 📈
 * Thermal Monitoring: Integrates with system hardware sensors to display real-time CPU temperature, a critical metric for any thermal performance evaluation.
Data & Diagnostics
 * Performance Data Export: All collected time-series data—timestamps, per-core usage, per-thread throughput, and temperature—can be seamlessly exported to a CSV file for in-depth offline analysis and reporting. 📊
 * Integrated Event Logging: A dedicated console within the UI reports key operational events, such as test initiation, resource allocation, and diagnostic messages.
System Architecture & Design
The application is built upon a modular and robust architecture centered around a single AppContext structure, eliminating global variables and promoting maintainability.
Concurrency Model
The architecture employs a sophisticated multi-threaded model to ensure maximum performance and a flawlessly responsive user interface.
 * Controller Thread: Orchestrates the entire test lifecycle—from resource allocation to worker thread management and cleanup. By running in a detached state, it ensures the GUI remains fluid and responsive at all times.
 * Worker Threads: The core of the stress test. These threads execute the intensive computational and memory workloads.
 * Sampler Thread: A high-frequency, independent thread that periodically collects CPU utilization and temperature metrics, asynchronously feeding data to the GUI for real-time visualization.
 * Thread Safety: Concurrency integrity is paramount. Data consistency is guaranteed through modern atomic types from <stdatomic.h> for simple counters and GMutex synchronization for complex shared data structures.
GUI & Data Visualization
The user interface is constructed with the versatile GTK3 toolkit. All charts and data visualizations are custom-drawn on GtkDrawingArea widgets using the powerful Cairo 2D graphics library, providing complete control over rendering performance and visual aesthetics for a polished, professional finish. ✨
Getting Started
Prerequisites
🐧 Linux (Debian/Ubuntu)
A C compiler and the GTK3 development libraries are required.
sudo apt update
sudo apt install build-essential libgtk-3-dev libhpdf-dev

For thermal monitoring, lm-sensors is highly recommended:
sudo apt install lm-sensors

🪟 Windows (with MSYS2)
Install the MSYS2 environment. From the MSYS2 MINGW64 terminal, install the necessary toolchain and libraries:
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-libharu pkg-config

Building from Source
The included Makefile automates the entire compilation process.
 * Clone the repository:
   git clone https://github.com/felipebehling/Hardstress.git
cd Hardstress

 * Compile the application:
   * For a standard debug build:
     make

   * For a high-performance release build with -O3 optimizations:
     make release

 * Execute:
   ./HardStress

Pre-Compiled Releases
For your convenience, a GitHub Actions workflow automatically builds and packages the application for both Linux and Windows. These ready-to-run binaries are available in the Releases section.
> A Note for Windows Users 🛡️
> Windows Defender SmartScreen may flag the pre-compiled executable because it is not digitally signed. The application is completely safe, and its source code is open for audit.
> To run it, simply click "More info" on the SmartScreen prompt, followed by "Run anyway".
> 
Usage Guide
Operating HardStress is straightforward:
 * Configure Test Parameters:
   * Threads: Set the number of worker threads (ideally, matching your CPU's logical core count).
   * Mem (MiB/thread): Specify the amount of RAM to be allocated by each thread.
   * Duration (s): Define the test duration in seconds. Use 0 for an indefinite run.
   * Pin threads to CPUs: Check this box to enable CPU affinity for maximum test consistency.
 * Initiate Test: Click the Start button.
 * Monitor Performance: Observe the real-time data visualizations.
 * Conclude Test: Click Stop to terminate the test manually.
 * Export Results: After the test completes, click Export CSV to save the performance data.
Project Validation
The project includes a comprehensive suite of unit tests to ensure the correctness and reliability of core utility and metrics functions.
To build and execute the test suite, run the following command:
make test

This is a critical validation step after making any modifications to the src/utils.c or src/metrics.c source files.
License
This project is licensed under the MIT License. See the LICENSE file for full details.
