# Product Requirement Document (PRD)

## Project Name
**QuaSYS Project (MVP Phase)**

## Document Information
- **Version:** 1.0
- **Status:** Draft Approved
- **Author:** Gemini & QuaSYS Architect
- **Date:** July 2026

---

## 1. Executive Summary & Vision
QuaSYS Project is an advanced, high-performance Web-based OS Assistant / Agentic AI designed to give users deep, full-system control over their local environment. By leveraging **C++** for the backend engine, QuaSYS achieves near-zero latency, absolute memory efficiency, and direct low-level OS kernel interaction. 

Unlike traditional web wrappers, QuaSYS is distributed as a source-based package that compiles locally upon installation, ensuring tailored CPU instruction optimizations for the target machine. The user interacts with this powerful engine via a lightweight, real-time **Web UI (HTML/CSS/JS)** connected through high-throughput **WebSockets**.

---

## 2. Target Audience & Deployment Strategy
- **Audience:** Power users, developers, system administrators, and automation enthusiasts who want an AI agent capable of breaking out of typical sandboxed environments to manage local workflows directly.
- **Cross-Platform Compatibility:** The source code must compile and adapt perfectly to Windows, Linux, and macOS.
- **Installation Model:** Source-based distribution. The installer fetches the C++ codebase and builds it locally using optimized compiler flags (e.g., `-march=native`, `-O3`) to achieve the ultimate hardware-specific execution speed.

---

## 3. Core Functional Requirements (MVP Scope)

### 3.1 Deep OS Integration & Control
The C++ backend must implement low-level wrappers to perform the following operations with absolute authority (bound by user privileges):

- **File & Folder Manipulation:**
  - Full CRUD (Create, Read, Update, Delete) operations on files and directories.
  - Ability to interact seamlessly with the current working directory (`cwd`) where the binary is executed.
- **Shell & Terminal Execution:**
  - Asynchronous execution of local CLI commands (e.g., PowerShell on Windows, Bash on Linux/macOS).
  - Real-time capturing and streaming of standard output (`stdout`) and standard error (`stderr`).
- **System Monitoring:**
  - Real-time polling of vital metrics: CPU usage, RAM utilization, and Disk I/O space.
- **Application & Web Control:**
  - Spawning, monitoring, and force-closing third-party processes.
  - Programmatically launching URLs inside the user's default web browser.
- **Hardware Device Manipulation:**
  - Macro-level programmatic control over user peripherals.
  - Simulating mouse movement, clicks, scrolls, and keystrokes/hotkeys.

### 3.2 Real-Time Communication & Interface Architecture
- **Semi-Static Web UI:** The primary application portal is a streamlined, client-side single-page interface consisting solely of static HTML, CSS, and vanilla JS. This UI is served directly from the embedded server within the C++ binary.
- **WebSocket Protocol:** To ensure zero-lag interactivity, all operational commands, system metrics, terminal streams, and AI responses must bypass HTTP polling and use persistent **WebSockets**.

### 3.3 Agentic AI Engine ("The Brain")
The backend must decouple the LLM inference layer, giving users a toggleable choice between localized or cloud processing:
1. **Local LLM Engine:** Native integration with `llama.cpp` for offline, zero-network, privacy-centric local inference using GGUF models.
2. **Cloud API Engine:** Integration with the Google Gemini API to leverage highly generalized reasoning capabilities when internet access is active.

---

## 4. Technical Stack & Architecture Plan

### 4.1 Backend Engine (C++)
- **Web & WebSocket Framework:** `Crow` or `Drogon`. *Crow* is prioritized for the MVP due to its clean, header-only or lightweight build nature, aligning well with quick local compilation.
- **File System API:** Standard C++17 `<filesystem>` library for platform-independent file discovery.
- **OS Abstraction Layers (Peripheral Manipulation):**
  - **Windows:** `Windows.h` native API (`SendInput`).
  - **Linux:** `libxdo` (xdotool API) or direct `evdev` event injections.
  - **macOS:** `ApplicationServices` / `CoreGraphics` frameworks (`CGEventCreateMouseEvent`).
- **AI Core:** `llama.cpp` compiled locally with vendor-specific acceleration options (CUDA, OpenCL, Vulkan, or Apple Silicon Metal).

### 4.2 Frontend Layer
- Pure HTML5, modern CSS variables, and vanilla JavaScript (ES6+).
- Event-driven WebSocket message broker to map UI controls to C++ routes instantly.

---

## 5. Security & Safety Vault Constraints

> 🛑 **CRITICAL REQUIREMENT:** Giving a web interface or an autonomous AI agent full control over the local OS exposes the machine to extreme systemic risks. 

- **Input Sanitization & Guardrails:** The C++ backend must implement a strict validation matrix for all instructions passed down by the AI engine. Destructive or hazardous shell inputs (e.g., recursive forced deletions like `rm -rf /` or formatting operations) must be parsed, flagged, and blocked before execution.
- **User Confirmation Prompts:** Highly destructive actions must trigger a manual "Break Glass" approval dialog on the Web UI, requiring physical user consent before the C++ system calls are executed.
- **Network Bound Restrictions:** By default, the embedded server must bind exclusively to `localhost` (`127.0.0.1`) to prevent external remote code execution (RCE) vectors from the wider local area network.

