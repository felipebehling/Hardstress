# HardStress

**A Professional Toolkit for System Stability and Performance Analysis.**

[![Build and Release](https://github.com/felipebehling/Hardstress/actions/workflows/build.yml/badge.svg)](https://github.com/felipebehling/Hardstress/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-linux%20%7C%20windows-blue)](https://github.com/felipebehling/Hardstress)

HardStress provides a sophisticated and reliable method for subjecting computational systems to intense, sustained workloads. It is an essential instrument for system analysts, hardware engineers, and performance enthusiasts who need to validate system stability, analyze thermal performance, and identify performance bottlenecks with precision.

<!-- Placeholder for a high-quality screenshot or GIF of the UI in action -->
<!-- ![HardStress UI](path/to/screenshot.png) -->

---

## Core Philosophy

HardStress is designed around three core principles: Precision, Clarity, and Control.

### Precision
The foundation of a reliable diagnostic tool is the quality of its stress engine. HardStress employs a multi-threaded architecture to saturate processor cores, executing a balanced mix of floating-point, integer, and memory-streaming operations. For maximum test consistency, it offers the ability to pin worker threads to specific CPU cores, mitigating OS scheduler migrations and ensuring reproducible performance analysis.

### Clarity
Understanding a system's response to stress is paramount. HardStress presents a real-time, high-fidelity view of your machine's performance through a clean and intuitive graphical interface. It provides dynamic visualizations for per-core utilization, per-thread performance history, and critical thermal metrics, allowing for the immediate identification of performance degradation or thermal throttling.

### Control
Every system is unique. HardStress provides the necessary controls to configure the test parameters to your specific needs, including the number of threads, memory allocation per thread, and test duration. When the analysis is complete, all time-series data can be exported to a CSV file for in-depth offline analysis and reporting.

---

## Getting Started

Pre-compiled binaries for Linux and Windows are available in the [Releases section](https://github.com/felipebehling/Hardstress/releases).

### Prerequisites

<details>
<summary><strong>🐧 Linux (Debian/Ubuntu)</strong></summary>

A C compiler and the GTK3 development libraries are required.
```bash
sudo apt update
sudo apt install build-essential libgtk-3-dev libhpdf-dev
```
For thermal monitoring, `lm-sensors` is highly recommended:
```bash
sudo apt install lm-sensors
```
</details>

<details>
<summary><strong>🪟 Windows (MSYS2)</strong></summary>

Install the [MSYS2](https://www.msys2.org/) environment. From the MSYS2 MINGW64 terminal, install the necessary toolchain and libraries:
```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-libharu pkg-config
```
> **Note for Windows Users:** Windows Defender SmartScreen may flag the pre-compiled executable as it is not digitally signed. The application is safe, and its source code is open for audit. To run it, click "More info" on the SmartScreen prompt, followed by "Run anyway".
</details>

---

## Development

To build the project from source, clone the repository and use the included Makefile.

```bash
git clone https://github.com/felipebehling/Hardstress.git
cd Hardstress
```

**Build the application:**
-   For a standard debug build: `make`
-   For a high-performance release build: `make release`

**Run the test suite:**
-   `make test`

This command builds and executes a suite of unit tests to validate the core utility and metrics functions.

---

## Usage

1.  **Configure Test Parameters:**
    -   **Threads:** Set the number of worker threads.
    -   **Mem (MiB/thread):** Specify the amount of RAM to be allocated by each thread.
    -   **Duration (s):** Define the test duration. Use `0` for an indefinite run.
    -   **Pin threads to CPUs:** Enable CPU affinity for maximum test consistency.
2.  **Initiate Test:** Click `Start`.
3.  **Monitor Performance:** Observe the real-time data visualizations.
4.  **Conclude Test:** Click `Stop` to terminate the test manually.
5.  **Export Results:** After the test completes, click `Export CSV` to save the performance data.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
