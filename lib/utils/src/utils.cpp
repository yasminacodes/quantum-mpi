#include "utils.hpp"

#include <chrono>
#include <unistd.h>

std::string trim(const std::string& s)
{
    const auto first = std::find_if_not(s.begin(), s.end(),
        [](unsigned char c) { return std::isspace(c); });
    if (first == s.end()) {
        return "";
    }

    const auto last = std::find_if_not(s.rbegin(), s.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();
    return std::string(first, last);
}

std::string parse_last_number(const std::string& text)
{
    const std::regex number_regex(R"((\d+))");
    std::sregex_iterator begin(text.begin(), text.end(), number_regex);
    std::sregex_iterator end;
    std::string result;

    for (auto it = begin; it != end; ++it) {
        result = (*it)[1].str();
    }

    return result;
}

CommandExecutionResult execute_command_capture(const std::string& command)
{
    std::array<char, 256> buffer{};
    std::string output;
    int exit_code = -1;

    const std::string full_command = command + " 2>&1";
    FILE* pipe = popen(full_command.c_str(), "r");
    if (pipe == nullptr) {
        return {exit_code, output};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (status == -1) {
        exit_code = -1;
    } else if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = status;
    }

    return {exit_code, output};
}

std::string parse_first_non_empty_line(const std::string& text)
{
    std::istringstream iss(text);
    std::string line;

    while (std::getline(iss, line)) {
        const std::string clean_line = trim(line);

        if (!clean_line.empty()) {
            return clean_line;
        }
    }

    return {};
}

bool is_terminal_job_state(const std::string& state)
{
    const std::string clean_state = trim(state);

    if (clean_state.empty()) {
        return false;
    }

    return true;
}

std::string build_unique_family_name(const std::string& prefix)
{
    std::string safe_prefix = prefix;
    if (safe_prefix.empty()) {
        safe_prefix = "quantum_mpi_family";
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    return safe_prefix + "_" + std::to_string(ms) + "_" + std::to_string(getpid());
}
