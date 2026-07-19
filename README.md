# QuaSYS

TUI-based system assistant — full local system control from your terminal.

## What it is

A C++17 terminal UI application with ncurses. Executes shell commands, monitors system resources, browses files, and chats with AI — all from a terminal interface.

## Quick start

**Prerequisites:** CMake 3.20+, a C++17 compiler, ncurses, libcurl, libuuid.

```bash
# Build
cmake -B build -S .
cmake --build build

# Setup (configure API keys)
./build/quasys --setup

# Run
./build/quasys
```

## Features

| View | What it does |
|------|-------------|
| **Terminal** | Execute shell commands, tracks working directory |
| **Files** | Browse directories, navigate into folders, view files |
| **System** | CPU, memory, disk, load average, uptime |
| **Processes** | Process table sorted by CPU usage |
| **Assistant** | AI chat with Gemini or Claude |
| **Conversations** | Browse and manage saved chats |
| **Settings** | Configure provider, API keys |

## Architecture

```
src/main.cpp      — TUI (ncurses), views, menu
src/util.cpp/h    — Shell execution, OS utilities
src/gemini.cpp/h  — Gemini API provider (libcurl)
src/claude.cpp/h  — Claude API provider (libcurl)
```

## CLI

```
quasys             Launch TUI
quasys --setup     Configure API keys
quasys --help      Show help
quasys --version   Show version
```

## Environment

- `GEMINI_API_KEY` — Gemini API key (fallback if not set in settings)

## Storage

- Settings: `~/.quasys/settings.json`
- Conversations: `~/.quasys/conversations/*.json`

## Build

- **C++17**, CMake 3.20+
- **Dependencies:** ncurses, libcurl, libuuid
- **Optimizations:** `-O3 -march=native`

## License

Unlicensed — internal project.
