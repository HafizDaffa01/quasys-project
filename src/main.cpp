#include "util.h"
#include "gemini.h"
#include "claude.h"

#include <ncurses.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <vector>
#include <deque>

static std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': break;
            default:   out += c;
        }
    }
    return out;
}

static std::string build_history_json(const std::vector<std::pair<std::string, std::string>>& history) {
    std::string out = "[";
    for (size_t i = 0; i < history.size(); ++i) {
        if (i) out += ",";
        out += "{\"role\":\"" + json_escape(history[i].first) + "\",\"content\":\"" + json_escape(history[i].second) + "\"}";
    }
    out += "]";
    return out;
}

static std::string gen_uuid() {
    std::ifstream f("/proc/sys/kernel/random/uuid");
    std::string uuid;
    std::getline(f, uuid);
    return uuid;
}

static std::filesystem::path conversations_dir() {
    const char* home = getenv("HOME");
    auto dir = std::filesystem::path(home ? home : ".") / ".quasys" / "conversations";
    std::filesystem::create_directories(dir);
    return dir;
}

static std::filesystem::path settings_file() {
    const char* home = getenv("HOME");
    auto dir = std::filesystem::path(home ? home : ".") / ".quasys";
    std::filesystem::create_directories(dir);
    return dir / "settings.json";
}

// --- TUI colors ---
enum {
    PAIR_DEFAULT = 1,
    PAIR_TITLE,
    PAIR_ACCENT,
    PAIR_USER,
    PAIR_ASSISTANT,
    PAIR_DIM,
    PAIR_ERROR,
    PAIR_MENU,
    PAIR_MENU_SEL,
    PAIR_BORDER,
};

static void init_colors() {
    start_color();
    use_default_colors();
    init_pair(PAIR_DEFAULT,  -1, -1);
    init_pair(PAIR_TITLE,    COLOR_CYAN,   -1);
    init_pair(PAIR_ACCENT,   COLOR_BLUE,   -1);
    PAIR_USER;
    init_pair(PAIR_USER,     COLOR_GREEN,  -1);
    init_pair(PAIR_ASSISTANT,COLOR_YELLOW, -1);
    init_pair(PAIR_DIM,      COLOR_WHITE,  -1);
    init_pair(PAIR_ERROR,    COLOR_RED,    -1);
    init_pair(PAIR_MENU,     COLOR_WHITE,  -1);
    init_pair(PAIR_MENU_SEL, COLOR_BLACK,  COLOR_CYAN);
    init_pair(PAIR_BORDER,   COLOR_CYAN,   -1);
}

// --- settings ---
struct Settings {
    std::string provider = "gemini";
    std::string gemini_key;
    std::string claude_key;
};

static Settings load_settings() {
    Settings s;
    auto data = read_file(settings_file());
    if (data.empty()) return s;
    // simple key-value parse
    auto extract = [&](const std::string& key) -> std::string {
        auto pos = data.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        auto colon = data.find(':', pos);
        auto q1 = data.find('"', colon + 1);
        auto q2 = data.find('"', q1 + 1);
        return data.substr(q1 + 1, q2 - q1 - 1);
    };
    s.provider = extract("provider");
    s.gemini_key = extract("gemini_key");
    s.claude_key = extract("claude_key");
    if (s.provider.empty()) s.provider = "gemini";
    return s;
}

static void save_settings(const Settings& s) {
    std::ostringstream json;
    json << "{\"provider\":\"" << json_escape(s.provider)
         << "\",\"gemini_key\":\"" << json_escape(s.gemini_key)
         << "\",\"claude_key\":\"" << json_escape(s.claude_key) << "\"}";
    std::ofstream out(settings_file());
    out << json.str();
}

// --- conversation ---
struct Conversation {
    std::string uuid;
    std::string title;
    std::vector<std::pair<std::string, std::string>> history;
};

static std::vector<Conversation> list_conversations() {
    std::vector<Conversation> convs;
    auto dir = conversations_dir();
    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        auto data = read_file(entry.path());
        if (data.empty()) continue;
        Conversation c;
        c.uuid = entry.path().stem().string();
        // extract title
        auto tp = data.find("\"title\"");
        if (tp != std::string::npos) {
            auto q1 = data.find('"', tp + 7);
            auto q2 = data.find('"', q1 + 1);
            c.title = data.substr(q1 + 1, q2 - q1 - 1);
        }
        convs.push_back(std::move(c));
    }
    std::sort(convs.begin(), convs.end(), [](const auto& a, const auto& b) {
        return a.uuid > b.uuid;
    });
    return convs;
}

static void save_conversation(const std::string& uuid,
                              const std::string& title,
                              const std::vector<std::pair<std::string, std::string>>& history) {
    auto path = conversations_dir() / (uuid + ".json");
    std::ostringstream json;
    json << "{\"title\":\"" << json_escape(title) << "\",\"history\":"
         << build_history_json(history) << "}";
    std::ofstream out(path);
    out << json.str();
}

static void delete_conversation(const std::string& uuid) {
    auto path = conversations_dir() / (uuid + ".json");
    if (std::filesystem::exists(path))
        std::filesystem::remove(path);
}

// --- UI helpers ---
static void draw_border(WINDOW* win) {
    int h, w;
    getmaxyx(win, h, w);
    wattron(win, COLOR_PAIR(PAIR_BORDER));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(PAIR_BORDER));
}

static void draw_title_bar(WINDOW* win, const std::string& title) {
    int w = getmaxx(win);
    wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
    mvwprintw(win, 0, 2, " %s ", title.c_str());
    wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
    // separator line
    wattron(win, COLOR_PAIR(PAIR_DIM));
    for (int i = 0; i < w; ++i) mvwaddch(win, 1, i, ACS_HLINE);
    wattroff(win, COLOR_PAIR(PAIR_DIM));
}

static void draw_status_bar(WINDOW* win, const std::string& left, const std::string& right) {
    int h, w;
    getmaxyx(win, h, w);
    wattron(win, COLOR_PAIR(PAIR_ACCENT) | A_REVERSE);
    mvwhline(win, 0, 0, ' ', w);
    mvwprintw(win, 0, 1, " %s", left.c_str());
    if (!right.empty()) mvwprintw(win, 0, w - (int)right.size() - 2, "%s ", right.c_str());
    wattroff(win, COLOR_PAIR(PAIR_ACCENT) | A_REVERSE);
}

static std::string input_line(WINDOW* win, int y, int x, int max_w, const std::string& prompt) {
    echo();
    curs_set(1);
    wattron(win, COLOR_PAIR(PAIR_USER) | A_BOLD);
    mvwprintw(win, y, x, "%s", prompt.c_str());
    wattroff(win, COLOR_PAIR(PAIR_USER) | A_BOLD);
    int input_x = x + (int)prompt.size();
    int buf_size = max_w - (int)prompt.size();
    if (buf_size < 10) buf_size = 10;
    char* buf = new char[buf_size + 1];
    mvwgetnstr(win, y, input_x, buf, buf_size);
    noecho();
    curs_set(0);
    std::string result(buf);
    delete[] buf;
    return result;
}

// --- views ---

static void view_system(WINDOW* main_win, const std::string& cwd) {
    int h, w;
    getmaxyx(main_win, h, w);
    werase(main_win);
    draw_border(main_win);
    draw_title_bar(main_win, "system info");

    std::string username = get_username();
    std::string hostname = get_hostname();

    int row = 3;
    auto put = [&](const std::string& label, const std::string& value) {
        if (row >= h - 1) return;
        wattron(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        mvwprintw(main_win, row, 3, "%-14s", label.c_str());
        wattroff(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        wattron(main_win, COLOR_PAIR(PAIR_DEFAULT));
        mvwprintw(main_win, row, 19, "%s", value.c_str());
        wattroff(main_win, COLOR_PAIR(PAIR_DEFAULT));
        row++;
    };

    // gather system info
    put("user", username + "@" + hostname);
    put("cwd", cwd);

    // memory
    std::string mem = run_command("free -h | awk '/^Mem:/{print $3\"/\"$2}'", const_cast<std::string&>(cwd));
    put("memory", mem);

    // disk
    std::string disk = run_command("df -h / | awk 'NR==2{print $3\"/\"$2}'", const_cast<std::string&>(cwd));
    put("disk /", disk);

    // load
    std::string load = run_command("cat /proc/loadavg | awk '{print $1, $2, $3}'", const_cast<std::string&>(cwd));
    put("load avg", load);

    // uptime
    std::string uptime = run_command("uptime -p", const_cast<std::string&>(cwd));
    put("uptime", uptime);

    // cpu
    std::string cpu = run_command("nproc", const_cast<std::string&>(cwd));
    put("cpu cores", cpu);

    row += 2;
    if (row < h - 1) {
        wattron(main_win, COLOR_PAIR(PAIR_DIM));
        mvwprintw(main_win, row, 3, "[press any key]");
        wattroff(main_win, COLOR_PAIR(PAIR_DIM));
    }

    wrefresh(main_win);
    wgetch(main_win);
}

static void view_processes(WINDOW* main_win, const std::string& cwd) {
    int h, w;
    getmaxyx(main_win, h, w);

    auto run_cmd = [&](const std::string& cmd) -> std::string {
        std::string c = cwd;
        return run_command(cmd, c);
    };

    std::string ps_out = run_cmd("ps aux --sort=-%cpu | head -n " + std::to_string(h - 4));

    werase(main_win);
    draw_border(main_win);
    draw_title_bar(main_win, "processes (by cpu)");

    // header
    wattron(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
    mvwprintw(main_win, 3, 2, "%-8s %-6s %-5s %-5s %-10s %s", "USER", "PID", "%CPU", "%MEM", "COMMAND", "");
    wattroff(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);

    int row = 4;
    std::istringstream stream(ps_out);
    std::string line;
    std::getline(stream, line); // skip header
    while (std::getline(stream, line) && row < h - 2) {
        if (row >= h - 2) break;
        mvwprintw(main_win, row, 2, "%s", line.substr(0, w - 5).c_str());
        row++;
    }

    row++;
    if (row < h - 1) {
        wattron(main_win, COLOR_PAIR(PAIR_DIM));
        mvwprintw(main_win, row, 3, "[press any key]");
        wattroff(main_win, COLOR_PAIR(PAIR_DIM));
    }

    wrefresh(main_win);
    wgetch(main_win);
}

static void view_files(WINDOW* main_win, const std::string& cwd) {
    int h, w;
    getmaxyx(main_win, h, w);

    std::string current_dir = cwd;

    while (true) {
        werase(main_win);
        draw_border(main_win);
        draw_title_bar(main_win, "files: " + current_dir);

        auto run_cmd = [&](const std::string& cmd) -> std::string {
            std::string c = current_dir;
            return run_command(cmd, c);
        };

        std::string ls_out = run_cmd("ls -1F 2>/dev/null");

        int row = 3;
        wattron(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        mvwprintw(main_win, row, 2, ".. (go up)");
        wattroff(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        row++;

        std::vector<std::string> entries;
        std::istringstream stream(ls_out);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) entries.push_back(line);
        }

        for (auto& entry : entries) {
            if (row >= h - 3) break;
            bool is_dir = entry.back() == '/';
            if (is_dir) {
                wattron(main_win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
            } else {
                wattron(main_win, COLOR_PAIR(PAIR_DEFAULT));
            }
            mvwprintw(main_win, row, 4, "%s", entry.substr(0, w - 7).c_str());
            wattroff(main_win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
            wattroff(main_win, COLOR_PAIR(PAIR_DEFAULT));
            row++;
        }

        row++;
        if (row < h - 1) {
            wattron(main_win, COLOR_PAIR(PAIR_DIM));
            mvwprintw(main_win, row, 3, "[enter name, .. to go up, q to quit]");
            wattroff(main_win, COLOR_PAIR(PAIR_DIM));
        }

        wrefresh(main_win);
        noecho();
        curs_set(0);
        int ch = wgetch(main_win);
        if (ch == 'q' || ch == 'Q') break;

        // input
        echo();
        curs_set(1);
        int input_y = h - 2;
        wattron(main_win, COLOR_PAIR(PAIR_USER));
        mvwprintw(main_win, input_y, 3, "> ");
        wattroff(main_win, COLOR_PAIR(PAIR_USER));
        char buf[256];
        mvwgetnstr(main_win, input_y, 5, buf, 250);
        noecho();
        curs_set(0);
        std::string input(buf);

        if (input.empty()) continue;
        if (input == "..") {
            auto pos = current_dir.rfind('/');
            if (pos > 0) current_dir = current_dir.substr(0, pos);
            else current_dir = "/";
            continue;
        }
        // enter dir or open file
        std::string target = current_dir;
        if (target.back() != '/') target += '/';
        target += input;
        if (input.back() == '/') input.pop_back();
        std::string check = run_cmd("test -d \"" + target + "\" && echo dir || echo file");
        if (check == "dir") {
            current_dir = target;
        } else {
            // cat file
            werase(main_win);
            draw_border(main_win);
            draw_title_bar(main_win, input);
            std::string content = run_cmd("head -n " + std::to_string(h - 5) + " \"" + target + "\"");
            int r = 3;
            std::istringstream cstream(content);
            std::string cline;
            while (std::getline(cstream, cline) && r < h - 2) {
                mvwprintw(main_win, r, 2, "%s", cline.substr(0, w - 5).c_str());
                r++;
            }
            r++;
            if (r < h - 1) {
                wattron(main_win, COLOR_PAIR(PAIR_DIM));
                mvwprintw(main_win, r, 3, "[press any key]");
                wattroff(main_win, COLOR_PAIR(PAIR_DIM));
            }
            wrefresh(main_win);
            wgetch(main_win);
        }
    }
}

static void view_terminal(WINDOW* main_win, std::string cwd) {
    int h, w;
    getmaxyx(main_win, h, w);

    std::deque<std::string> output;
    output.push_back("QuaSYS terminal. type 'exit' to return.");
    output.push_back("");

    while (true) {
        werase(main_win);
        draw_border(main_win);
        draw_title_bar(main_win, "terminal");

        int row = 3;
        for (auto& line : output) {
            if (row >= h - 3) break;
            // truncate long lines
            std::string display = line.substr(0, w - 6);
            if (!output.empty() && output.back().substr(0, 1) == "$") {
                // command line in accent
                wattron(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            } else {
                wattron(main_win, COLOR_PAIR(PAIR_DEFAULT));
            }
            mvwprintw(main_win, row, 2, "%s", display.c_str());
            wattroff(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            wattroff(main_win, COLOR_PAIR(PAIR_DEFAULT));
            row++;
        }

        // status
        if (row < h - 2) {
            wattron(main_win, COLOR_PAIR(PAIR_DIM));
            std::string status = "cwd: " + cwd;
            mvwprintw(main_win, row, 2, "%s", status.substr(0, w - 5).c_str());
            wattroff(main_win, COLOR_PAIR(PAIR_DIM));
            row++;
        }

        // input
        echo();
        curs_set(1);
        int input_y = h - 2;
        wattron(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
        mvwprintw(main_win, input_y, 2, "$ ");
        wattroff(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
        char buf[1024];
        mvwgetnstr(main_win, input_y, 4, buf, 1000);
        noecho();
        curs_set(0);
        std::string cmd(buf);

        if (cmd.empty()) continue;
        if (cmd == "exit" || cmd == "quit") break;

        output.push_back("$ " + cmd);

        std::string result = run_command(cmd, cwd);
        if (!result.empty()) {
            std::istringstream stream(result);
            std::string line;
            while (std::getline(stream, line)) {
                output.push_back(line);
            }
        }

        // keep output manageable
        while ((int)output.size() > h * 2) output.pop_front();
    }
}

static void view_assistant(WINDOW* main_win, const Settings& settings) {
    int h, w;
    getmaxyx(main_win, h, w);

    std::string username = get_username();
    std::string hostname = get_hostname();
    std::string cwd = std::filesystem::current_path().string();

    std::string sys = "You are QuaSYS AI, a helpful system assistant. "
                      "User: " + username + "@" + hostname + ". "
                      "OS: Linux. CWD: " + cwd + ". "
                      "You help with system administration, file management, coding, and general tasks. "
                      "Be concise and helpful. Use markdown when appropriate.";

    std::vector<std::pair<std::string, std::string>> history;
    std::deque<std::string> messages;
    messages.push_back("[QuaSYS AI ready. provider: " + settings.provider + "]");
    messages.push_back("");

    while (true) {
        werase(main_win);
        draw_border(main_win);

        std::string title = "assistant [" + settings.provider + "]";
        draw_title_bar(main_win, title);

        int row = 3;
        for (auto& msg : messages) {
            if (row >= h - 3) break;
            std::string display = msg.substr(0, w - 6);
            if (display.find("[user]") == 0) {
                wattron(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            } else if (display.find("[ai]") == 0 || display.find("[thinking]") == 0) {
                wattron(main_win, COLOR_PAIR(PAIR_ASSISTANT) | A_BOLD);
            } else if (display.find("[error") == 0) {
                wattron(main_win, COLOR_PAIR(PAIR_ERROR) | A_BOLD);
            } else {
                wattron(main_win, COLOR_PAIR(PAIR_DEFAULT));
            }
            mvwprintw(main_win, row, 2, "%s", display.c_str());
            wattroff(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            wattroff(main_win, COLOR_PAIR(PAIR_ASSISTANT) | A_BOLD);
            wattroff(main_win, COLOR_PAIR(PAIR_ERROR) | A_BOLD);
            wattroff(main_win, COLOR_PAIR(PAIR_DEFAULT));
            row++;
        }

        if (row < h - 2) {
            wattron(main_win, COLOR_PAIR(PAIR_DIM));
            mvwprintw(main_win, row, 2, "[type your message, /clear to reset, /quit to exit]");
            wattroff(main_win, COLOR_PAIR(PAIR_DIM));
        }

        // input
        echo();
        curs_set(1);
        int input_y = h - 2;
        wattron(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
        mvwprintw(main_win, input_y, 2, "> ");
        wattroff(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
        char buf[1024];
        mvwgetnstr(main_win, input_y, 4, buf, 1000);
        noecho();
        curs_set(0);
        std::string input(buf);

        if (input.empty()) continue;
        if (input == "/quit" || input == "/q") break;
        if (input == "/clear") {
            history.clear();
            messages.clear();
            messages.push_back("[conversation cleared]");
            messages.push_back("");
            continue;
        }

        messages.push_back("[user] " + input);
        history.emplace_back("user", input);

        messages.push_back("[thinking...]");
        wrefresh(main_win);

        std::string reply;
        if (settings.provider == "claude") {
            if (settings.claude_key.empty()) {
                reply = "[error: no Claude API key — run: quasys --setup]";
            } else {
                reply = claude_chat(settings.claude_key, sys, history);
            }
        } else {
            std::string key = settings.gemini_key;
            if (key.empty()) {
                const char* env = getenv("GEMINI_API_KEY");
                if (env) key = env;
            }
            if (key.empty()) {
                reply = "[error: no Gemini API key — run: quasys --setup]";
            } else {
                reply = gemini_chat(key, sys, history);
            }
        }

        // remove [thinking...]
        if (!messages.empty()) messages.pop_back();

        messages.push_back("[ai] " + reply);
        history.emplace_back("model", reply);
    }
}

static void view_conversations(WINDOW* main_win) {
    int h, w;
    getmaxyx(main_win, h, w);

    int sel = 0;

    while (true) {
        auto convs = list_conversations();

        werase(main_win);
        draw_border(main_win);
        draw_title_bar(main_win, "conversations");

        if (convs.empty()) {
            wattron(main_win, COLOR_PAIR(PAIR_DIM));
            mvwprintw(main_win, 4, 4, "no conversations yet");
            wattroff(main_win, COLOR_PAIR(PAIR_DIM));
        } else {
            int row = 3;
            for (size_t i = 0; i < convs.size() && row < h - 4; ++i) {
                if ((int)i == sel) {
                    wattron(main_win, COLOR_PAIR(PAIR_MENU_SEL));
                    mvwprintw(main_win, row, 2, " %s ", convs[i].title.substr(0, w - 8).c_str());
                    wattroff(main_win, COLOR_PAIR(PAIR_MENU_SEL));
                } else {
                    wattron(main_win, COLOR_PAIR(PAIR_DEFAULT));
                    mvwprintw(main_win, row, 4, "%s", convs[i].title.substr(0, w - 8).c_str());
                    wattroff(main_win, COLOR_PAIR(PAIR_DEFAULT));
                }
                row++;
            }
        }

        int info_row = h - 3;
        wattron(main_win, COLOR_PAIR(PAIR_DIM));
        mvwprintw(main_win, info_row, 3, "[j/k: navigate  d: delete  q: back]");
        wattroff(main_win, COLOR_PAIR(PAIR_DIM));

        wrefresh(main_win);
        int ch = wgetch(main_win);

        if (ch == 'q' || ch == 'Q') break;
        if (ch == 'j' || ch == KEY_DOWN) {
            if (!convs.empty() && sel < (int)convs.size() - 1) sel++;
        }
        if (ch == 'k' || ch == KEY_UP) {
            if (sel > 0) sel--;
        }
        if ((ch == 'd' || ch == 'D') && !convs.empty()) {
            delete_conversation(convs[sel].uuid);
            if (sel >= (int)convs.size() - 1 && sel > 0) sel--;
        }
    }
}

static void view_settings(WINDOW* main_win, Settings& settings) {
    int h, w;
    getmaxyx(main_win, h, w);

    werase(main_win);
    draw_border(main_win);
    draw_title_bar(main_win, "settings");

    int row = 3;
    auto put_field = [&](const std::string& label, const std::string& value) {
        if (row >= h - 4) return;
        wattron(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        mvwprintw(main_win, row, 3, "%-14s", label.c_str());
        wattroff(main_win, COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        wattron(main_win, COLOR_PAIR(PAIR_DEFAULT));
        std::string display = value.empty() ? "(empty)" : value.substr(0, 40);
        if (value.size() > 40) display += "...";
        mvwprintw(main_win, row, 19, "%s", display.c_str());
        wattroff(main_win, COLOR_PAIR(PAIR_DEFAULT));
        row++;
    };

    put_field("provider", settings.provider);
    put_field("gemini key", settings.gemini_key.empty() ? "(not set)" : std::string(settings.gemini_key.substr(0, 8)) + "...");
    put_field("claude key", settings.claude_key.empty() ? "(not set)" : std::string(settings.claude_key.substr(0, 8)) + "...");

    row += 2;
    wattron(main_win, COLOR_PAIR(PAIR_DIM));
    mvwprintw(main_win, row, 3, "[p: change provider]");
    row++;
    mvwprintw(main_win, row, 3, "[g: set gemini key]");
    row++;
    mvwprintw(main_win, row, 3, "[c: set claude key]");
    row++;
    mvwprintw(main_win, row, 3, "[q: back]");
    wattroff(main_win, COLOR_PAIR(PAIR_DIM));

    wrefresh(main_win);

    while (true) {
        int ch = wgetch(main_win);
        if (ch == 'q' || ch == 'Q') break;

        if (ch == 'p') {
            settings.provider = (settings.provider == "gemini") ? "claude" : "gemini";
            save_settings(settings);
            view_settings(main_win, settings);
            return;
        }
        if (ch == 'g') {
            echo();
            curs_set(1);
            wattron(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            mvwprintw(main_win, row - 2, 3, "> gemini key: ");
            wattroff(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            char buf[256];
            mvwgetnstr(main_win, row - 2, 17, buf, 200);
            noecho();
            curs_set(0);
            settings.gemini_key = buf;
            save_settings(settings);
            view_settings(main_win, settings);
            return;
        }
        if (ch == 'c') {
            echo();
            curs_set(1);
            wattron(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            mvwprintw(main_win, row - 1, 3, "> claude key: ");
            wattroff(main_win, COLOR_PAIR(PAIR_USER) | A_BOLD);
            char buf[256];
            mvwgetnstr(main_win, row - 1, 17, buf, 200);
            noecho();
            curs_set(0);
            settings.claude_key = buf;
            save_settings(settings);
            view_settings(main_win, settings);
            return;
        }
    }
}

// --- main menu ---
static void run_tui() {
    Settings settings = load_settings();
    std::string cwd = std::filesystem::current_path().string();

    int menu_sel = 0;
    const char* menu_items[] = {
        "terminal",
        "files",
        "system",
        "processes",
        "assistant",
        "conversations",
        "settings",
        "quit"
    };
    const int menu_count = sizeof(menu_items) / sizeof(menu_items[0]);

    // main window
    WINDOW* main_win = newwin(0, 0, 0, 0);

    while (true) {
        int h, w;
        getmaxyx(stdscr, h, w);

        werase(stdscr);

        // title
        wattron(stdscr, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
        mvwprintw(stdscr, 1, 2, "QuaSYS");
        wattroff(stdscr, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
        wattron(stdscr, COLOR_PAIR(PAIR_DIM));
        mvwprintw(stdscr, 1, 10, "system assistant");
        wattroff(stdscr, COLOR_PAIR(PAIR_DIM));

        // separator
        wattron(stdscr, COLOR_PAIR(PAIR_BORDER));
        for (int i = 0; i < w; ++i) mvwaddch(stdscr, 2, i, ACS_HLINE);
        wattroff(stdscr, COLOR_PAIR(PAIR_BORDER));

        // menu
        int start_y = 4;
        for (int i = 0; i < menu_count; ++i) {
            if (i == menu_sel) {
                wattron(stdscr, COLOR_PAIR(PAIR_MENU_SEL));
                mvwprintw(stdscr, start_y + i, 2, " > %-16s", menu_items[i]);
                wattroff(stdscr, COLOR_PAIR(PAIR_MENU_SEL));
            } else {
                wattron(stdscr, COLOR_PAIR(PAIR_MENU));
                mvwprintw(stdscr, start_y + i, 4, "%s", menu_items[i]);
                wattroff(stdscr, COLOR_PAIR(PAIR_MENU));
            }
        }

        // status
        draw_status_bar(stdscr, "provider: " + settings.provider, "q: quit");

        wrefresh(stdscr);

        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        if (ch == 'j' || ch == KEY_DOWN) {
            menu_sel = (menu_sel + 1) % menu_count;
        }
        if (ch == 'k' || ch == KEY_UP) {
            menu_sel = (menu_sel - 1 + menu_count) % menu_count;
        }
        if (ch == '\n' || ch == KEY_ENTER) {
            switch (menu_sel) {
                case 0: view_terminal(main_win, cwd); break;
                case 1: view_files(main_win, cwd); break;
                case 2: view_system(main_win, cwd); break;
                case 3: view_processes(main_win, cwd); break;
                case 4: view_assistant(main_win, settings); break;
                case 5: view_conversations(main_win); break;
                case 6: view_settings(main_win, settings); break;
                case 7: goto done;
            }
            // reload settings after returning from views
            settings = load_settings();
        }
    }
done:
    delwin(main_win);
}

// --- setup mode (non-interactive) ---
static void run_setup() {
    Settings settings = load_settings();

    std::cout << "=== QuaSYS Setup ===" << std::endl;
    std::cout << "Current provider: " << settings.provider << std::endl;
    std::cout << std::endl;

    std::cout << "Provider [gemini/claude] (current: " << settings.provider << "): ";
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty()) settings.provider = input;

    std::cout << "Gemini API key (current: " << (settings.gemini_key.empty() ? "not set" : settings.gemini_key.substr(0, 8) + "...") << "): ";
    std::getline(std::cin, input);
    if (!input.empty()) settings.gemini_key = input;

    std::cout << "Claude API key (current: " << (settings.claude_key.empty() ? "not set" : settings.claude_key.substr(0, 8) + "...") << "): ";
    std::getline(std::cin, input);
    if (!input.empty()) settings.claude_key = input;

    save_settings(settings);
    std::cout << "Settings saved." << std::endl;
}

int main(int argc, char* argv[]) {
    // check args
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--setup" || arg == "-s") {
            run_setup();
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "QuaSYS - TUI system assistant" << std::endl;
            std::cout << std::endl;
            std::cout << "Usage: quasys [options]" << std::endl;
            std::cout << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --setup, -s   Configure API keys and provider" << std::endl;
            std::cout << "  --help, -h    Show this help" << std::endl;
            std::cout << std::endl;
            std::cout << "Environment:" << std::endl;
            std::cout << "  GEMINI_API_KEY   Gemini API key (fallback)" << std::endl;
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "QuaSYS v0.1.0" << std::endl;
            return 0;
        }
    }

    // init ncurses
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    init_colors();

    run_tui();

    endwin();
    return 0;
}
