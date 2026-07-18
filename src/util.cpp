#include "util.h"

#include <array>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <lmcons.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#include <pwd.h>
#endif

std::string get_username() {
#ifdef _WIN32
    char buf[UNLEN + 1];
    DWORD len = UNLEN + 1;
    return GetUserNameA(buf, &len) ? buf : "unknown";
#else
    const char* user = getenv("USER");
    if (user) return user;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_name : "unknown";
#endif
}

std::string get_hostname() {
#ifdef _WIN32
    char buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
    return GetComputerNameA(buf, &len) ? buf : "unknown";
#else
    char buf[256];
    return gethostname(buf, sizeof(buf)) == 0 ? buf : "unknown";
#endif
}

std::string shell_exec(const std::string& cmd) {
    std::array<char, 4096> buf{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buf.data(), buf.size(), pipe) != nullptr)
        result += buf.data();
    pclose(pipe);
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

std::string run_command(const std::string& raw_cmd, std::string& cwd) {
    std::string cmd = raw_cmd;
    while (!cmd.empty() && cmd.front() == ' ') cmd.erase(cmd.begin());
    if (cmd.empty()) return "";

    const char* shell_env = getenv("SHELL");
    std::string shell = shell_env ? shell_env : "/bin/sh";

    bool is_cd = (cmd == "cd" || cmd.rfind("cd ", 0) == 0);
    std::string target;
    if (is_cd) {
        target = (cmd.size() > 3) ? cmd.substr(3) : "";
        while (!target.empty() && target.front() == ' ') target.erase(target.begin());
        if (target.empty() || target == "~") {
            const char* home = getenv("HOME");
            target = home ? home : "/root";
        } else if (target == "-") {
            target = cwd;
        }
    }

    std::string full;
    if (is_cd) {
        full = "cd \"" + cwd + "\" && cd \"" + target + "\" && pwd 2>&1";
    } else {
        full = "cd \"" + cwd + "\" && " + cmd + " 2>&1";
    }

    std::string output = shell_exec(full);

    if (is_cd) {
        if (!output.empty()) {
            cwd = output;
        }
        return "";
    }

    if (output.empty()) return "(no output)";
    return output;
}

std::string get_exec_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return len ? std::filesystem::path(buf).parent_path().string() : ".";
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) { buf[len] = '\0'; return std::filesystem::path(buf).parent_path().string(); }
    return std::filesystem::current_path().string();
#endif
}
