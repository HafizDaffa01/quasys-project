#pragma once

#include <string>

std::string get_username();
std::string get_hostname();
std::string get_exec_dir();
std::string shell_exec(const std::string& cmd);
std::string run_command(const std::string& raw_cmd, std::string& cwd);
