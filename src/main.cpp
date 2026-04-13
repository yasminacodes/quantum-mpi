#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "quantum_circuit.hpp"
#include "quantum_mpi.hpp"

#if QUANTUM_MPI_WITH_CUNQA
#include "comm/client.hpp"
#include "logger.hpp"
#include <spdlog/spdlog.h>
std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();
#endif

namespace {

constexpr const char* kDefaultQraiseCommand = "qraise -n 4 -t 01:00:00 --co-located";
constexpr const char* kQraiseCommandEnvVar = "QUANTUM_MPI_QRAISE_COMMAND";

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

void add_instruction_or_throw(
    QuantumCircuit& circuit,
    const std::string& gate_name,
    const std::vector<int>& qubits_idx,
    const std::vector<int>& clbits_idx)
{
    const int rc = circuit.addInstruction(gate_name, qubits_idx, clbits_idx);
    if (rc != 0) {
        throw std::runtime_error(
            "Error adding instruction '" + gate_name + "'. Code: " + std::to_string(rc));
    }
}

QuantumCircuit build_basic_h_measure_circuit()
{
    QuantumCircuit circuit("basic_h_measure", 1, 1, 1024, "CPU");
    add_instruction_or_throw(circuit, "h", {0}, {});
    add_instruction_or_throw(circuit, "measure", {0}, {0});
    return circuit;
}

QuantumCircuit build_bell_pair_measure_circuit()
{
    QuantumCircuit circuit("bell_pair_measure", 2, 2, 2048, "CPU");
    add_instruction_or_throw(circuit, "h", {0}, {});
    add_instruction_or_throw(circuit, "cx", {0, 1}, {});
    add_instruction_or_throw(circuit, "measure", {0}, {0});
    add_instruction_or_throw(circuit, "measure", {1}, {1});
    return circuit;
}

std::string join_cli_args_as_command(int argc, char** argv)
{
    if (argc <= 1 || argv == nullptr) {
        return "";
    }

    std::string command;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr || std::string(argv[i]).empty()) {
            continue;
        }
        if (!command.empty()) {
            command += " ";
        }
        command += argv[i];
    }

    return command;
}

std::string resolve_qraise_command(int argc, char** argv)
{
    const std::string from_cli = join_cli_args_as_command(argc, argv);
    if (!from_cli.empty()) {
        return from_cli;
    }

    const char* env_value = std::getenv(kQraiseCommandEnvVar);
    if (env_value != nullptr && *env_value != '\0') {
        return std::string(env_value);
    }

    return kDefaultQraiseCommand;
}

#if QUANTUM_MPI_WITH_CUNQA

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
        "\"slurm_job_id\"\\s*:\\s*(\"" + escaped_job_id + "\"|" + escaped_job_id + ")");
    const std::regex endpoint_regex("\"endpoint\"\\s*:\\s*\"([^\"]+)\"");

    std::sregex_iterator begin(qpus_content.begin(), qpus_content.end(), job_regex);
    std::sregex_iterator end;

    for (auto it = begin; it != end; ++it) {
        const std::size_t match_pos = static_cast<std::size_t>((*it).position());
        const std::size_t next_job = qpus_content.find("\"slurm_job_id\"", match_pos + 1);
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

#endif

std::string run_circuit_and_recv(
    QuantumMPI& quantum_mpi,
    const std::string& endpoint,
    QuantumCircuit& circuit,
    const std::string& label)
{
    const std::string circuit_json = circuit.parseToRawQCJson();
    std::cout << "\n[" << label << "] Circuit JSON:\n" << circuit_json << '\n';
    const std::string result = quantum_mpi.runCircuitJson(endpoint, circuit_json);
    std::cout << "[" << label << "] CUNQA result:\n" << result << '\n';
    return result;
}

} // namespace

int main(int argc, char** argv)
{
#if !QUANTUM_MPI_WITH_CUNQA
    std::cerr
        << "This executable was built without CUNQA integration.\n"
        << "Reconfigure with -DQUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON to run distributed execution.\n";
    return 1;
#else
    try {
        const std::string qraise_cmd = resolve_qraise_command(argc, argv);
        std::cout << "Launching qraise: " << qraise_cmd << '\n';

        auto launcher = std::make_shared<CommandJobLauncher>(qraise_cmd);
        auto resolver = std::make_shared<PollingEndpointResolver>(
            lookup_cunqa_endpoint_for_job,
            lookup_cunqa_terminal_state);
        auto transport = std::make_shared<FunctionTransport>(
            [](const std::string& endpoint, const std::string& payload) -> std::string {
                cunqa::comm::Client client;
                client.connect(endpoint);
                client.send_circuit(payload);
                return client.recv_results();
            });

        QuantumMPI quantum_mpi(launcher, resolver, transport);
        const std::string qraise_job_id = quantum_mpi.launchJob();
        std::cout << "qraise job id: " << qraise_job_id << '\n';

        const std::string endpoint = quantum_mpi.waitEndpointForJob(qraise_job_id, 120, 2);

        QuantumCircuit basic_circuit = build_basic_h_measure_circuit();
        run_circuit_and_recv(quantum_mpi, endpoint, basic_circuit, "basic_h_measure");

        QuantumCircuit bell_circuit = build_bell_pair_measure_circuit();
        run_circuit_and_recv(quantum_mpi, endpoint, bell_circuit, "bell_pair_measure");
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
#endif
}
