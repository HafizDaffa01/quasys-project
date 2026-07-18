# QuaSYS

Web-based OS Assistant — full local system control from your browser.

## What it is

A C++17 backend with a vanilla HTML/CSS/JS frontend. Runs a local web server, executes shell commands, monitors system resources, browses files — all from a terminal-style UI on `http://127.0.0.1:8080`.

## Quick start

**Prerequisites:** CMake 3.20+, a C++17 compiler, Git.

```bash
# Clone dependencies (Crow + ASIO)
git clone --depth 1 https://github.com/CrowCpp/Crow.git deps/crow
git clone --depth 1 https://github.com/chriskohlhoff/asio.git deps/asio

# Build
cmake -B build -S .
cmake --build build

# Run
./build/quasys
```

Open `http://127.0.0.1:8080`.

## Features

| View | What it does |
|------|-------------|
| **Terminal** | Execute shell commands, see output with timestamps. Tracks working directory. |
| **Files** | Browse directories, navigate with URL paths (`/files/home/user`). |
| **System** | Live dashboard — CPU, memory, disk, load average, uptime. Refreshes every 3s. |
| **Processes** | Process table sorted by CPU usage (`ps aux`). |
| **Assistant** | AI engine placeholder (planned: llama.cpp + Gemini API). |
| **About** | Project info. |

**UI:** Collapsible sidebar, dark/light theme toggle, SPA routing with clean URLs.

## Architecture

```
src/main.cpp     — Crow server, SPA routes, API endpoints
src/util.cpp/h   — Shell execution, OS utilities (username, hostname, cwd)
web/index.html   — HTML shell
web/style.css    — Terminal-style CSS (slate/blue palette)
web/app.js       — Routing, terminal, files, system dashboard, processes
deps/            — Crow (header-only web framework) + ASIO (async I/O)
```

## API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/info` | GET | Returns `{user, host, home}` |
| `/api/exec` | POST | Executes shell command. Body: `{"cmd": "..."}`. Returns `{output, cwd}` |

## Security

- Binds to `127.0.0.1` only — no external access.
- Commands run under current user privileges.
- No input sanitization yet (MVP).

## Tech

- **Backend:** C++17, Crow v1.2.0, ASIO
- **Frontend:** Vanilla HTML5/CSS/JS — no frameworks, no build step
- **Build:** CMake, source-based local compilation

## Status

MVP phase. Core views functional. AI engine not yet integrated.

## License

Unlicensed — internal project.
