#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <string>
#include <array>
#include <cctype>
#include <cstdio>
#include <regex>
#include <sstream>
#include <sys/wait.h>

struct CommandExecutionResult {
    int exit_code = -1;
    std::string output;
};

CommandExecutionResult execute_command_capture(const std::string& command);

std::string trim(const std::string& s);
std::string parse_last_number(const std::string& text);
std::string parse_first_non_empty_line(const std::string& text);
bool is_terminal_job_state(const std::string& state);
std::string build_unique_family_name(const std::string& prefix);

#endif
