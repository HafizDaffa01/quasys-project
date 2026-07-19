#pragma once

#include <string>
#include <vector>
#include <utility>

std::string gemini_chat(const std::string& api_key,
                        const std::string& system_instruction,
                        const std::vector<std::pair<std::string, std::string>>& history);

std::string gemini_title(const std::string& api_key,
                         const std::vector<std::pair<std::string, std::string>>& history);
