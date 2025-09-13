<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&color=auto&height=240&section=header&text=HardStress&fontSize=80&fontColor=ffffff" alt="HardStress Banner"/>

# HardStress
### A Professional Toolkit for System Stability and Performance Analysis.

<p>
    <a href="https://github.com/felipebehling/Hardstress/actions/workflows/build.yml">
        <img src="https://github.com/felipebehling/Hardstress/actions/workflows/build.yml/badge.svg" alt="Build and Release">
    </a>
    <a href="https://opensource.org/licenses/MIT">
        <img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT">
    </a>
    <a href="https://github.com/felipebehling/Hardstress">
        <img src="https://img.shields.io/badge/platform-linux%20%7C%20windows-blue" alt="Platform">
    </a>
</p>

<p align="center">
  <a href="#-about-the-project">About</a> •
  <a href="#-key-features">Features</a> •
  <a href="#-getting-started">Getting Started</a> •
  <a href="#-usage">Usage</a> •
  <a href="#-development">Development</a> •
  <a href="#-contributing">Contributing</a> •
  <a href="#-license">License</a> •
  <a href="#-acknowledgments">Acknowledgments</a>
</p>
</div>

---

## 📖 About the Project

HardStress provides a sophisticated and reliable method for subjecting computational systems to intense, sustained workloads. It is an essential instrument for system analysts, hardware engineers, and performance enthusiasts who need to validate system stability, analyze thermal performance, and identify performance bottlenecks with precision.

<!-- Placeholder for a high-quality screenshot or GIF of the UI in action -->
<!-- <div align="center">
    <img src="path/to/screenshot.png" alt="HardStress UI" width="700"/>
</div> -->

---

## ✨ Key Features

HardStress is designed around three core principles: Precision, Clarity, and Control.

| Feature   | Description                                                                                                                                                                                                                                 |
| :-------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **🎯 Precision** | Employs a multi-threaded architecture to saturate processor cores with a balanced mix of floating-point, integer, and memory-streaming operations. Offers the ability to pin worker threads to specific CPU cores for maximum test consistency. |
| **📊 Clarity**   | Presents a real-time, high-fidelity view of your machine's performance through a clean and intuitive graphical interface. Provides dynamic visualizations for per-core utilization, per-thread performance history, and critical thermal metrics. |
| **⚙️ Control**    | Provides the necessary controls to configure test parameters to your specific needs, including the number of threads, memory allocation per thread, and test duration. All time-series data can be exported to a CSV file for in-depth analysis. |

---

## 🚀 Getting Started

Pre-compiled binaries for Linux and Windows are available in the [Releases section](https://github.com/felipebehling/Hardstress/releases).

### Prerequisites

<details>
<summary><strong>🐧 Linux (Debian/Ubuntu)</strong></summary>

<br>

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

<br>

Install the [MSYS2](https://www.msys2.org/) environment. From the MSYS2 MINGW64 terminal, install the necessary toolchain and libraries:
```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-libharu pkg-config
```
> **Note for Windows Users:** Windows Defender SmartScreen may flag the pre-compiled executable as it is not digitally signed. The application is safe, and its source code is open for audit. To run it, click "More info" on the SmartScreen prompt, followed by "Run anyway". Additionally, for the performance metrics (like CPU usage) to appear correctly, you may need to run the application with administrative privileges. Right-click `HardStress.exe` and select 'Run as administrator'.
</details>

---

## 👨‍💻 Usage

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

## 🛠️ Development

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

## 🤝 Contributing

Contributions are what make the open source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

If you have a suggestion that would make this better, please fork the repo and create a pull request. You can also simply open an issue with the tag "enhancement".
Don't forget to give the project a star! Thanks again!

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## 📜 License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

A special thanks to the following projects and communities for their inspiration and for the tools that made this project possible:

-   [Shields.io](https://shields.io/) for the dynamic badges.
-   [Capsule Render](https://github.com/kyechan99/capsule-render) for the awesome header banner.
-   The open-source community for providing amazing resources and support.

---

<p align="center">
  <em>A professional toolkit for system stability and performance analysis.</em>
</p>
