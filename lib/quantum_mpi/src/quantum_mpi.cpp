#include "quantum_mpi.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <sys/wait.h>

namespace {

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

} // namespace

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

QuantumMPI::QuantumMPI(
    std::shared_ptr<IJobLauncher> launcher,
    std::shared_ptr<IEndpointResolver> resolver,
    std::shared_ptr<IQuantumMPITransport> transport) :
    launcher_(std::move(launcher)),
    resolver_(std::move(resolver)),
    transport_(std::move(transport))
{
    if (!launcher_) {
        throw std::invalid_argument("QuantumMPI requires a non-null job launcher.");
    }
    if (!resolver_) {
        throw std::invalid_argument("QuantumMPI requires a non-null endpoint resolver.");
    }
    if (!transport_) {
        throw std::invalid_argument("QuantumMPI requires a non-null transport.");
    }
}

std::string QuantumMPI::launchJob() const
{
    return launcher_->launch_job();
}

std::string QuantumMPI::waitEndpointForJob(
    const std::string& job_id,
    int timeout_seconds,
    int poll_seconds) const
{
    return resolver_->wait_endpoint_for_job(job_id, timeout_seconds, poll_seconds);
}

std::string QuantumMPI::runPayload(
    const std::string& endpoint,
    const std::string& payload) const
{
    return transport_->send(endpoint, payload);
}

std::string QuantumMPI::launchQraise() const
{
    return launchJob();
}

std::string QuantumMPI::runCircuitJson(
    const std::string& endpoint,
    const std::string& circuit_json) const
{
    return runPayload(endpoint, circuit_json);
}

CommandJobLauncher::CommandJobLauncher(std::string launch_command, JobIdParser parser) :
    launch_command_(std::move(launch_command)),
    parser_(std::move(parser))
{
    if (launch_command_.empty()) {
        throw std::invalid_argument("Launch command cannot be empty.");
    }
    if (!parser_) {
        parser_ = &CommandJobLauncher::parse_digits_line_or_last_number;
    }
}

std::string CommandJobLauncher::launch_job() const
{
    const CommandExecutionResult result = execute_command_capture(launch_command_);
    if (result.exit_code != 0) {
        throw std::runtime_error(
            "Launch command failed with code " + std::to_string(result.exit_code) + ".");
    }

    const std::string job_id = parser_(result.output);
    if (job_id.empty()) {
        throw std::runtime_error("Could not parse job id from launcher output.");
    }

    return job_id;
}

std::string CommandJobLauncher::parse_digits_line_or_last_number(const std::string& text)
{
    std::istringstream iss(text);
    std::string line;
    const std::regex digits_only(R"(^\d+$)");
    while (std::getline(iss, line)) {
        line = trim(line);
        if (std::regex_match(line, digits_only)) {
            return line;
        }
    }

    return parse_last_number(text);
}

PollingEndpointResolver::PollingEndpointResolver(
    EndpointLookup endpoint_lookup,
    JobStateLookup job_state_lookup) :
    endpoint_lookup_(std::move(endpoint_lookup)),
    job_state_lookup_(std::move(job_state_lookup))
{
    if (!endpoint_lookup_) {
        throw std::invalid_argument("Endpoint lookup callback cannot be empty.");
    }
}

std::string PollingEndpointResolver::wait_endpoint_for_job(
    const std::string& job_id,
    int timeout_seconds,
    int poll_seconds) const
{
    if (job_id.empty()) {
        throw std::invalid_argument("Job id cannot be empty.");
    }

    const int safe_poll = std::max(1, poll_seconds);
    const int safe_timeout = std::max(1, timeout_seconds);
    const int max_tries = std::max(1, safe_timeout / safe_poll);

    for (int i = 0; i < max_tries; ++i) {
        const std::string endpoint = endpoint_lookup_(job_id);
        if (!endpoint.empty()) {
            return endpoint;
        }

        if (job_state_lookup_) {
            const std::string terminal_state = trim(job_state_lookup_(job_id));
            if (!terminal_state.empty()) {
                throw std::runtime_error(
                    "Job '" + job_id + "' reached terminal state '" + terminal_state +
                    "' before endpoint registration.");
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(safe_poll));
    }

    throw std::runtime_error(
        "No endpoint found for job '" + job_id + "' before timeout (" +
        std::to_string(safe_timeout) + "s).");
}
