#include "util.h"

#include <crow.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>

static std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
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

    CROW_LOG_INFO << "QuaSYS running on http://127.0.0.1:8080";
    app.bindaddr("127.0.0.1").port(8080).multithreaded().run();
}
