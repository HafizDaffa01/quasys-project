# AGENTS.md

## What is QuaSYS

Web-based OS Assistant / Agentic AI with full local system control. Planning docs: `PRD.md`, `ponytail_rules.md`.

## Tech stack

- **Backend:** C++17, Crow (header-only), WebSockets, ASIO (standalone)
- **AI:** llama.cpp (local GGUF models) + Google Gemini API (cloud fallback) — not yet integrated
- **Frontend:** Vanilla HTML5/CSS/JS — no frameworks, no build step
- **Build:** CMake 3.20+, deps cloned in `deps/` (Crow v1.2.0, ASIO 1.30.0)

## Build

```bash
cmake -B build -S .
cmake --build build
./build/quasys
```

Server runs on `http://127.0.0.1:8080`. Binds localhost only.

## Project structure

```
src/main.cpp    — entry point, Crow server + WebSocket endpoint
web/index.html  — minimal terminal-style UI
deps/           — Crow + ASIO (shallow clones, committed or fetched)
CMakeLists.txt  — single target, no external deps beyond deps/
```

## Coding rules — read `ponytail_rules.md`

Key non-obvious constraints:
- YAGNI: don't build it unless asked
- Reuse before writing new code — check codebase, stdlib, platform APIs
- Minimal code; one line if possible
- No unsolicited abstractions, dependencies, or boilerplate
- Deletion over addition; boring over clever
- Fewest files possible

## Crow API notes

Crow v1.2.0 uses `CROW_WEBSOCKET_ROUTE(app, url)` macro (not `crow::websocket::route`). Use `conn.send_text()` / `conn.send_binary()` (not `conn.send()`).
