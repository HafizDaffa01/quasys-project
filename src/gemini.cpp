#include "gemini.h"

#include <curl/curl.h>
#include <sstream>

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string json_escape(const std::string& s) {
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

std::string gemini_chat(const std::string& api_key,
                        const std::string& system_instruction,
                        const std::vector<std::pair<std::string, std::string>>& history) {
    if (api_key.empty()) return "[error: GEMINI_API_KEY not set]";

    // Build contents JSON array from history
    std::ostringstream contents;
    contents << "[";
    for (size_t i = 0; i < history.size(); ++i) {
        if (i > 0) contents << ",";
        contents << "{\"role\":\"" << history[i].first
                 << "\",\"parts\":[{\"text\":\"" << json_escape(history[i].second) << "\"}]}";
    }
    contents << "]";

    // Build full request body
    std::ostringstream body;
    body << "{\"contents\":" << contents.str();
    if (!system_instruction.empty()) {
        body << ",\"systemInstruction\":{\"parts\":[{\"text\":\""
             << json_escape(system_instruction) << "\"}]}";
    }
    body << "}";

    std::string payload = body.str();

    // libcurl request
    CURL* curl = curl_easy_init();
    if (!curl) return "[error: curl init failed]";

    std::string response;
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash:generateContent";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "x-goog-api-key: " + api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        return std::string("[error: curl failed: ") + curl_easy_strerror(res) + "]";
    }

    curl_easy_cleanup(curl);

    // Check for error response
    if (response.find("\"error\"") != std::string::npos) {
        // Response format: {"error":{"code":400,"message":"..."}}
        auto err_block = response.find("\"error\"");
        auto msg_pos = response.find("\"message\":", err_block);
        if (msg_pos != std::string::npos) {
            msg_pos += 11; // skip "message":
            while (msg_pos < response.size() && response[msg_pos] == ' ') msg_pos++;
            if (msg_pos < response.size() && response[msg_pos] == '"') {
                msg_pos++; // skip opening quote
                auto msg_end = msg_pos;
                while (msg_end < response.size()) {
                    if (response[msg_end] == '\\') { msg_end += 2; continue; }
                    if (response[msg_end] == '"') break;
                    ++msg_end;
                }
                return "[gemini: " + response.substr(msg_pos, msg_end - msg_pos) + "]";
            }
        }
        return "[gemini: API error]";
    }

    // Extract text from response — find "text" key in candidates[0].content.parts[0]
    auto text_pos = response.find("\"text\"");
    if (text_pos == std::string::npos)
        return "[error: unexpected response format]";

    // find opening quote of the value after "text"
    auto colon = response.find(':', text_pos + 6);
    text_pos = response.find('"', colon + 1) + 1; // skip opening quote
    auto text_end = text_pos;
    while (text_end < response.size()) {
        if (response[text_end] == '\\') {
            text_end += 2; // skip escaped char
            continue;
        }
        if (response[text_end] == '"') break;
        ++text_end;
    }

    // Unescape the JSON string
    std::string result;
    result.reserve(text_end - text_pos);
    for (auto i = text_pos; i < text_end; ++i) {
        if (response[i] == '\\' && i + 1 < text_end) {
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

std::string gemini_title(const std::string& api_key,
                         const std::vector<std::pair<std::string, std::string>>& history) {
    // build title history from user messages only
    std::vector<std::pair<std::string, std::string>> title_history;
    title_history.emplace_back("user", "[generate a short title for this conversation. max 5 words. no quotes, no punctuation, no period.]");
    int count = 0;
    for (auto& [role, content] : history) {
        if (role == "user") {
            title_history.emplace_back(role, content);
            if (++count >= 3) break; // max 3 user messages for context
        }
    }
    return gemini_chat(api_key, "", title_history);
}
