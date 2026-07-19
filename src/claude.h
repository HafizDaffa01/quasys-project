#pragma once

#include <string>
#include <vector>
#include <utility>

std::string claude_chat(const std::string& api_key,
                        const std::string& system_instruction,
                        const std::vector<std::pair<std::string, std::string>>& history,
                        const std::string& model = "sonnet");

std::string claude_title(const std::string& api_key,
                         const std::vector<std::pair<std::string, std::string>>& history,
                         const std::string& model = "sonnet");
