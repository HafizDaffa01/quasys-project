#include "util.h"
#include "gemini.h"
#include "claude.h"

#include <crow.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <thread>

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

static std::string build_history_json(const crow::json::rvalue& history_arr) {
    std::string out = "[";
    for (size_t i = 0; i < history_arr.size(); ++i) {
        auto entry = history_arr[i];
        if (entry.has("role") && entry.has("content")) {
            if (i) out += ",";
            out += "{\"role\":\"" + json_escape(entry["role"].s()) + "\",\"content\":\"" + json_escape(entry["content"].s()) + "\"}";
        }
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

int main() {
    std::cout << "+------------------+" << std::endl;
    std::cout << "|  QuaSYS Project  |" << std::endl;
    std::cout << "+------------------+" << std::endl;

    crow::SimpleApp app;

    std::string username = get_username();
    std::string hostname = get_hostname();
    std::string cwd = std::filesystem::current_path().string();
    std::string exec_dir = get_exec_dir();

    std::filesystem::path web_dir = std::filesystem::path(exec_dir) / "web";
    if (!std::filesystem::exists(web_dir))
        web_dir = std::filesystem::current_path() / "web";

    // --- static file server ---
    auto serve_index = [&web_dir] {
        std::string body = read_file(web_dir / "index.html");
        if (body.empty())
            return crow::response(404, "text/plain", "index.html not found");
        return crow::response(200, "text/html", body);
    };

    auto serve_static = [&web_dir](const std::string& file, const std::string& content_type) {
        return [&web_dir, file, content_type] {
            std::string body = read_file(web_dir / file);
            if (body.empty())
                return crow::response(404, "text/plain", file + " not found");
            return crow::response(200, content_type, body);
        };
    };

    // --- SPA routes ---
    CROW_ROUTE(app, "/") (serve_index);
    CROW_ROUTE(app, "/terminal") (serve_index);
    CROW_ROUTE(app, "/files") (serve_index);
    CROW_ROUTE(app, "/system") (serve_index);
    CROW_ROUTE(app, "/processes") (serve_index);
    CROW_ROUTE(app, "/assistant") (serve_index);
    CROW_ROUTE(app, "/about") (serve_index);
    CROW_ROUTE(app, "/settings") (serve_index);

    CROW_ROUTE(app, "/assistant/<str>")
    ([&serve_index](const std::string&) {
        return serve_index();
    });

    CROW_ROUTE(app, "/files/<path>")
    ([&serve_index](const std::string&) {
        return serve_index();
    });

    // --- static assets ---
    CROW_ROUTE(app, "/style.css") (serve_static("style.css", "text/css"));
    CROW_ROUTE(app, "/app.js") (serve_static("app.js", "application/javascript"));

    // --- api routes ---
    CROW_ROUTE(app, "/api/info")
    ([&username, &hostname] {
        crow::json::wvalue res;
        res["user"] = username;
        res["host"] = hostname;
        const char* home = getenv("HOME");
        res["home"] = home ? home : "";
        return crow::response(res);
    });

    // --- settings ---
    CROW_ROUTE(app, "/api/settings")
    ([] {
        auto path = settings_file();
        std::string data = read_file(path);
        // return as-is, or defaults
        if (data.empty())
            return crow::response(200, "application/json",
                "{\"provider\":\"gemini\",\"gemini_key\":\"\",\"claude_key\":\"\"}");
        return crow::response(200, "application/json", data);
    });

    CROW_ROUTE(app, "/api/settings")
    .methods("POST"_method)
    ([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, "application/json", "{\"error\":\"invalid json\"}");

        std::string provider = body.has("provider") ? std::string(body["provider"].s()) : "gemini";
        std::string gemini_key = body.has("gemini_key") ? std::string(body["gemini_key"].s()) : "";
        std::string claude_key = body.has("claude_key") ? std::string(body["claude_key"].s()) : "";

        auto escape = [](const std::string& s) {
            std::ostringstream out;
            for (char c : s) {
                if (c == '"') out << "\\\"";
                else if (c == '\\') out << "\\\\";
                else if (c != '\0') out << c;
            }
            return out.str();
        };

        std::ostringstream json;
        json << "{\"provider\":\"" << escape(provider)
             << "\",\"gemini_key\":\"" << escape(gemini_key)
             << "\",\"claude_key\":\"" << escape(claude_key) << "\"}";

        std::ofstream out(settings_file());
        out << json.str();
        out.close();

        return crow::response(200, "application/json", "{\"ok\":true}");
    });

    CROW_ROUTE(app, "/api/exec")
    .methods("POST"_method)
    ([&cwd](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("cmd"))
            return crow::response(400, "application/json", "{\"error\":\"missing cmd\"}");
        std::string cmd = body["cmd"].s();
        std::string output = run_command(cmd, cwd);
        crow::json::wvalue res;
        res["output"] = output;
        res["cwd"] = cwd;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/chat")
    .methods("POST"_method)
    ([&username, &hostname, &cwd](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("history"))
            return crow::response(400, "application/json", "{\"error\":\"missing history\"}");

        // read settings for provider + keys
        std::string provider = "gemini";
        std::string gemini_key, claude_key;
        auto sdata = read_file(settings_file());
        if (!sdata.empty()) {
            auto sj = crow::json::load(sdata);
            if (sj) {
                if (sj.has("provider")) provider = sj["provider"].s();
                if (sj.has("gemini_key")) gemini_key = sj["gemini_key"].s();
                if (sj.has("claude_key")) claude_key = sj["claude_key"].s();
            }
        }
        // request can override key
        if (body.has("api_key") && std::string(body["api_key"].s()).size() > 0) {
            if (provider == "claude") claude_key = body["api_key"].s();
            else gemini_key = body["api_key"].s();
        }
        // env var fallback
        if (gemini_key.empty()) { const char* k = getenv("GEMINI_API_KEY"); if (k) gemini_key = k; }

        auto history_arr = body["history"];
        std::vector<std::pair<std::string, std::string>> history;
        for (size_t i = 0; i < history_arr.size(); ++i) {
            auto entry = history_arr[i];
            if (entry.has("role") && entry.has("content"))
                history.emplace_back(entry["role"].s(), entry["content"].s());
        }
        if (history.empty())
            return crow::response(400, "application/json", "{\"error\":\"empty history\"}");

        std::string sys = "You are QuaSYS AI, a helpful system assistant. "
                          "User: " + username + "@" + hostname + ". "
                          "OS: Linux. CWD: " + cwd + ". "
                          "You help with system administration, file management, coding, and general tasks. "
                          "Be concise and helpful. Use markdown when appropriate.";

        std::string reply;
        if (provider == "claude") {
            if (claude_key.empty())
                return crow::response(503, "application/json", "{\"error\":\"no Claude API key — set in Settings\"}");
            reply = claude_chat(claude_key, sys, history);
        } else {
            if (gemini_key.empty())
                return crow::response(503, "application/json", "{\"error\":\"no Gemini API key — set in Settings or GEMINI_API_KEY env\"}");
            reply = gemini_chat(gemini_key, sys, history);
        }

        crow::json::wvalue res;
        res["reply"] = reply;
        return crow::response(res);
    });

    // --- conversation storage ---
    CROW_ROUTE(app, "/api/chat/list")
    ([] {
        auto dir = conversations_dir();
        std::vector<std::pair<std::string, std::string>> entries; // uuid, title
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() != ".json") continue;
            auto data = read_file(entry.path());
            auto j = crow::json::load(data);
            if (!j) continue;
            std::string uuid = entry.path().stem().string();
            std::string title = j.has("title") ? std::string(j["title"].s()) : "untitled";
            entries.emplace_back(uuid, title);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        crow::json::wvalue::list items;
        for (auto& [uuid, title] : entries) {
            crow::json::wvalue item;
            item["uuid"] = uuid;
            item["title"] = title;
            items.push_back(std::move(item));
        }
        crow::json::wvalue res;
        res["conversations"] = std::move(items);
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/chat/load/<str>")
    ([](const std::string& uuid) {
        auto path = conversations_dir() / (uuid + ".json");
        if (!std::filesystem::exists(path))
            return crow::response(404, "application/json", "{\"error\":\"not found\"}");
        return crow::response(200, "application/json", read_file(path));
    });

    CROW_ROUTE(app, "/api/chat/save")
    .methods("POST"_method)
    ([&username, &hostname, &cwd](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("history"))
            return crow::response(400, "application/json", "{\"error\":\"missing history\"}");

        std::string uuid;
        if (body.has("uuid") && std::string(body["uuid"].s()).size() > 0)
            uuid = std::string(body["uuid"].s());
        else
            uuid = gen_uuid();

        // build history
        std::vector<std::pair<std::string, std::string>> history;
        auto history_arr = body["history"];
        for (size_t i = 0; i < history_arr.size(); ++i) {
            auto entry = history_arr[i];
            if (entry.has("role") && entry.has("content"))
                history.emplace_back(entry["role"].s(), entry["content"].s());
        }

        // fallback title: first user message
        std::string title = "untitled";
        for (auto& [role, content] : history) {
            if (role == "user") { title = content; break; }
        }
        { std::string clean; for (char c : title) { if (c != '\n' && c != '\r') clean += c; } title = clean.substr(0, 50); }

        // save file immediately
        auto path = conversations_dir() / (uuid + ".json");
        {
            std::ostringstream json;
            json << "{\"title\":\"";
            for (char c : title) { if (c == '"') json << "\\\""; else if (c == '\\') json << "\\\\"; else if (c != '\0') json << c; }
            json << "\",\"history\":";
            json << build_history_json(body["history"]);
            json << "}";
            std::ofstream out(path); out << json.str(); out.close();
        }

        // async title generation — read provider from settings
        std::string provider = "gemini";
        std::string gemini_key, claude_key;
        {
            auto sd = read_file(settings_file());
            if (!sd.empty()) {
                auto sj = crow::json::load(sd);
                if (sj) {
                    if (sj.has("provider")) provider = sj["provider"].s();
                    if (sj.has("gemini_key")) gemini_key = sj["gemini_key"].s();
                    if (sj.has("claude_key")) claude_key = sj["claude_key"].s();
                }
            }
            if (gemini_key.empty()) { const char* k = getenv("GEMINI_API_KEY"); if (k) gemini_key = k; }
        }
        if (history.size() >= 2) {
            std::thread([provider, gemini_key, claude_key, history, path, uuid]() mutable {
                std::string new_title;
                if (provider == "claude" && !claude_key.empty())
                    new_title = claude_title(claude_key, history);
                else if (!gemini_key.empty())
                    new_title = gemini_title(gemini_key, history);

                if (!new_title.empty() && new_title[0] != '[' && new_title.size() >= 2) {
                    auto data = crow::json::load(read_file(path));
                    if (data) {
                        std::ostringstream json;
                        json << "{\"title\":\"";
                        for (char c : new_title) { if (c == '"') json << "\\\""; else if (c == '\\') json << "\\\\"; else if (c != '\0') json << c; }
                        json << "\",\"history\":";
                        json << build_history_json(data["history"]);
                        json << "}";
                        std::ofstream out(path); out << json.str(); out.close();
                    }
                }
            }).detach();
        }

        crow::json::wvalue res;
        res["uuid"] = uuid;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/chat/delete/<str>")
    .methods("DELETE"_method)
    ([](const std::string& uuid) {
        auto path = conversations_dir() / (uuid + ".json");
        if (std::filesystem::exists(path))
            std::filesystem::remove(path);
        return crow::response(200, "{\"ok\":true}");
    });

    CROW_ROUTE(app, "/api/chat/rename/<str>")
    .methods("PUT"_method)
    ([](const crow::request& req, const std::string& uuid) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("title"))
            return crow::response(400, "application/json", "{\"error\":\"missing title\"}");

        auto path = conversations_dir() / (uuid + ".json");
        if (!std::filesystem::exists(path))
            return crow::response(404, "application/json", "{\"error\":\"not found\"}");

        auto data = crow::json::load(read_file(path));
        if (!data)
            return crow::response(500, "application/json", "{\"error\":\"corrupt json\"}");

        std::string new_title = body["title"].s();

        std::ostringstream json;
        json << "{\"title\":\"";
        for (char c : new_title) {
            if (c == '"') json << "\\\"";
            else if (c == '\\') json << "\\\\";
            else if (c != '\0') json << c;
        }
        json << "\",\"history\":";
        json << data["history"];
        json << "}";

        std::ofstream out(path);
        out << json.str();
        out.close();

        return crow::response(200, "application/json", "{\"ok\":true}");
    });

    CROW_LOG_INFO << "QuaSYS running on http://127.0.0.1:8080";
    app.bindaddr("127.0.0.1").port(8080).multithreaded().run();
}
