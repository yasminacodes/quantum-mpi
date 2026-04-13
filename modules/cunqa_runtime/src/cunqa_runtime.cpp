#include "cunqa_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "comm/client.hpp"

namespace {

constexpr const char* kDefaultQraiseCommand = "qraise -n 4 -t 01:00:00 --co-located";

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

std::string first_non_empty_line(const std::string& text)
{
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (!line.empty()) {
            const std::size_t sep = line.find('|');
            if (sep != std::string::npos) {
                return trim(line.substr(0, sep));
            }
            return line;
        }
    }

    return "";
}

std::string read_text_file(const std::string& filename)
{
    std::ifstream in(filename);
    if (!in.is_open()) {
        throw std::runtime_error("Error opening file: " + filename);
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::vector<std::string> build_qpus_filepaths()
{
    std::vector<std::string> qpus_filepaths;

    const char* store_env = std::getenv("STORE");
    if (store_env != nullptr) {
        qpus_filepaths.push_back(std::string(store_env) + "/.cunqa/qpus.json");
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        const std::string home_path = home;
        qpus_filepaths.push_back(home_path + "/.cunqa/qpus.json");
        qpus_filepaths.push_back("/mnt/netapp1/Store_CESGA/" + home_path + "/.cunqa/qpus.json");
    }

    std::vector<std::string> unique_qpus_filepaths;
    std::set<std::string> seen_paths;
    for (const auto& path : qpus_filepaths) {
        if (seen_paths.insert(path).second) {
            unique_qpus_filepaths.push_back(path);
        }
    }

    return unique_qpus_filepaths;
}

std::string escape_regex_literal(const std::string& value)
{
    static const std::regex special_chars(R"([-[\]{}()*+?.,\^$|#\s])");
    return std::regex_replace(value, special_chars, R"(\$&)");
}

std::string find_endpoint_for_job_in_qpus_content(
    const std::string& qpus_content,
    const std::string& job_id)
{
    if (qpus_content.empty() || job_id.empty()) {
        return "";
    }

    const std::string escaped_job_id = escape_regex_literal(job_id);
    const std::regex job_regex(
        "\"slurm_job_id\"\\s*:\\s*(\""+ escaped_job_id +"\"|" + escaped_job_id + ")");
    const std::regex endpoint_regex("\"endpoint\"\\s*:\\s*\"([^\"]+)\"");

    std::sregex_iterator begin(qpus_content.begin(), qpus_content.end(), job_regex);
    std::sregex_iterator end;

    for (auto it = begin; it != end; ++it) {
        const std::size_t match_pos = static_cast<std::size_t>((*it).position());
        const std::size_t next_job =
            qpus_content.find("\"slurm_job_id\"", match_pos + 1);
        const std::size_t window_end =
            (next_job == std::string::npos) ? qpus_content.size() : next_job;
        const std::string window = qpus_content.substr(match_pos, window_end - match_pos);

        std::smatch endpoint_match;
        if (std::regex_search(window, endpoint_match, endpoint_regex) &&
            endpoint_match.size() > 1) {
            return endpoint_match[1].str();
        }
    }

    return "";
}

std::string lookup_cunqa_endpoint_for_job(const std::string& job_id)
{
    const std::vector<std::string> qpus_filepaths = build_qpus_filepaths();
    for (const auto& qpus_filepath : qpus_filepaths) {
        std::string qpus_content;
        try {
            qpus_content = read_text_file(qpus_filepath);
        } catch (const std::exception&) {
            continue;
        }

        const std::string endpoint = find_endpoint_for_job_in_qpus_content(qpus_content, job_id);
        if (!endpoint.empty()) {
            return endpoint;
        }
    }

    return "";
}

bool is_terminal_failure_state(const std::string& state)
{
    static const std::vector<std::string> terminal_states = {
        "FAILED", "CANCELLED", "TIMEOUT", "OUT_OF_MEMORY",
        "NODE_FAIL", "PREEMPTED", "BOOT_FAIL", "DEADLINE"
    };

    for (const auto& terminal_state : terminal_states) {
        if (state.find(terminal_state) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string lookup_cunqa_terminal_state(const std::string& job_id)
{
    const CommandExecutionResult squeue_result =
        execute_command_capture("squeue -h -j " + job_id + " -o %T");

    if (squeue_result.exit_code != 0) {
        return "";
    }

    const std::string squeue_state = trim(squeue_result.output);
    if (squeue_state.empty()) {
        const CommandExecutionResult sacct_result =
            execute_command_capture("sacct -j " + job_id + " --format=State --noheader --parsable2");

        if (sacct_result.exit_code != 0) {
            return "UNKNOWN";
        }
        const std::string sacct_state = first_non_empty_line(sacct_result.output);
        return sacct_state.empty() ? "UNKNOWN" : sacct_state;
    }

    if (is_terminal_failure_state(squeue_state)) {
        return squeue_state;
    }

    return "";
}

} // namespace

std::string resolve_cunqa_qraise_command(const CunqaRuntimeConfig& config)
{
    if (!config.qraise_command.empty()) {
        return config.qraise_command;
    }

    if (!config.qraise_command_env_var.empty()) {
        const char* env_value = std::getenv(config.qraise_command_env_var.c_str());
        if (env_value != nullptr && *env_value != '\0') {
            return std::string(env_value);
        }
    }

    return kDefaultQraiseCommand;
}

std::shared_ptr<IJobLauncher> make_cunqa_job_launcher(const CunqaRuntimeConfig& config)
{
    return std::make_shared<CommandJobLauncher>(resolve_cunqa_qraise_command(config));
}

std::shared_ptr<IEndpointResolver> make_cunqa_endpoint_resolver()
{
    return std::make_shared<PollingEndpointResolver>(
        lookup_cunqa_endpoint_for_job,
        lookup_cunqa_terminal_state);
}

std::shared_ptr<IQuantumMPITransport> make_cunqa_transport()
{
    return std::make_shared<FunctionTransport>(
        [](const std::string& endpoint, const std::string& payload) -> std::string {
            cunqa::comm::Client client;
            client.connect(endpoint);
            client.send_circuit(payload);
            return client.recv_results();
        });
}
