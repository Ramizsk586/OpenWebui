# OpenWebUI Dashboard

A small native Windows dashboard for preparing, launching, and stopping an Open WebUI server.

This project is written in C with the Win32 API and builds with CMake. The app checks the local machine for the required runtime pieces, shows setup commands when something is missing, and can launch Open WebUI directly once the environment is ready.

## What It Does

- Checks for Python 3.12
- Checks for Miniconda
- Checks for FFmpeg
- Detects whether an HF token is saved
- Shows setup commands for missing dependencies
- Starts Open WebUI on port `8081`
- Stops the running Open WebUI server
- Shows live startup terminal logs inside the app while the server is starting

## Project Layout

```text
.
├── CMakeLists.txt
├── app.rc
├── icon/
│   ├── app.ico
│   └── Local_web.ico
└── src/
    ├── main.c
    └── browser.c
```

Note: the current CMake target builds `src/main.c`. `src/browser.c` is present in the repository but is not wired into the active CMake target right now.

## Build

From the repository root:

```powershell
cmake -S . -B build
cmake --build build
```

The executable is generated in the `build` directory.

## Run

After building, run:

```powershell
.\build\OpenWebUIDashboard.exe
```

## Requirements

- Windows
- CMake 3.15+
- A C compiler supported by CMake on Windows

At runtime, the dashboard expects or helps install:

- Python 3.12
- Miniconda
- FFmpeg
- Open WebUI inside the configured Conda environment

## Notes

- The app stores some preferences under `HKCU\Software\OpenWebUI-Dashboard`.
- The default Open WebUI environment name is `omx-open-webui`.
- The default local server URL is `http://127.0.0.1:8081`.

