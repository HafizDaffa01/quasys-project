#include "claude.h"

#include <curl/curl.h>
#include <sstream>
#include <uuid/uuid.h>

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string gen_uuid() {
    uuid_t uuid;
    uuid_generate(uuid);
    char buf[37];
    uuid_unparse_lower(uuid, buf);
    return std::string(buf);
}

static std::string form_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\0') continue;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

static std::string build_prompt(const std::vector<std::pair<std::string, std::string>>& history) {
    std::ostringstream prompt;
    for (size_t i = 0; i < history.size(); ++i) {
        if (i > 0) prompt << "\n\n";
        if (history[i].first == "user")
            prompt << "Human: " << history[i].second;
        else
            prompt << "Assistant: " << history[i].second;
    }
    return prompt.str();
}

std::string claude_chat(const std::string& api_key,
                        const std::string& system_instruction,
                        const std::vector<std::pair<std::string, std::string>>& history,
                        const std::string& model) {
    if (api_key.empty()) return "[error: Claude API key not set]";
    if (history.empty()) return "[error: empty history]";

    std::string prompt = build_prompt(history);

    CURL* curl = curl_easy_init();
    if (!curl) return "[error: curl init failed]";

    std::string response;
    std::string url = "https://api.covenant.sbs/api/ai/claude";

    struct curl_slist* headers = nullptr;
    std::string auth_header = "x-api-key: " + api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part;

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "prompt");
    curl_mime_data(part, prompt.c_str(), CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "model");
    curl_mime_data(part, model.c_str(), CURL_ZERO_TERMINATED);

    if (!system_instruction.empty()) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "system");
        curl_mime_data(part, system_instruction.c_str(), CURL_ZERO_TERMINATED);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_mime_free(mime);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        return std::string("[error: curl failed: ") + curl_easy_strerror(res) + "]";
    }

    curl_easy_cleanup(curl);

    if (response.find("\"status\":false") != std::string::npos ||
        response.find("\"code\":4") != std::string::npos ||
        response.find("\"code\":5") != std::string::npos) {
        return "[claude: " + response.substr(0, 200) + "]";
    }

    // try "response" key first (proxy API format), then "reply", "content", "text"
    auto reply_pos = response.find("\"response\"");
    if (reply_pos == std::string::npos)
        reply_pos = response.find("\"reply\"");
    if (reply_pos == std::string::npos)
        reply_pos = response.find("\"content\"");
    if (reply_pos == std::string::npos)
        reply_pos = response.find("\"text\"");
    if (reply_pos == std::string::npos)
        return "[claude: " + response.substr(0, 200) + "]";

    auto key_end = response.find('"', reply_pos + 1);
    auto colon = response.find(':', key_end);
    auto val_start = response.find('"', colon + 1) + 1;
    auto val_end = val_start;
    while (val_end < response.size()) {
        if (response[val_end] == '\\') { val_end += 2; continue; }
        if (response[val_end] == '"') break;
        ++val_end;
    }

    std::string result;
    for (auto i = val_start; i < val_end; ++i) {
        if (response[i] == '\\' && i + 1 < val_end) {
            switch (response[i + 1]) {
                case 'n':  result += '\n'; i++; break;
                case 't':  result += '\t'; i++; break;
                case 'r':  result += '\r'; i++; break;
                case '\\': result += '\\'; i++; break;
                case '"':  result += '"';  i++; break;
                default:   result += response[i + 1]; i++; break;
            }
        } else if (response[i] != '\0') {
            result += response[i];
        }
    }

    return result.empty() ? "[empty response]" : result;
}

std::string claude_title(const std::string& api_key,
                         const std::vector<std::pair<std::string, std::string>>& history,
                         const std::string& model) {
    std::vector<std::pair<std::string, std::string>> title_history;
    title_history.emplace_back("user", "[generate a short title for this conversation. max 5 words. no quotes, no punctuation, no period.]");
    int count = 0;
    for (auto& [role, content] : history) {
        if (role == "user") {
            title_history.emplace_back(role, content);
            if (++count >= 3) break;
        }
    }
    return claude_chat(api_key, "", title_history, model);
}
