#include "cunqa_api.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "comm/client.hpp"
#include "utils.hpp"

void quantum_mpi_ensure_cunqa_logger_ready();

static CunqaExecutionResult makeCunqaError(
    int status,
    const std::string& error
)
{
    CunqaExecutionResult result;
    result.status = status;
    result.error = error;
    return result;
}

static std::string getEnvString(const char* name)
{
    const char* value = std::getenv(name);

    if (value == nullptr) {
        return {};
    }

    return std::string(value);
}

static long getEnvPositiveLong(
    const char* name,
    long default_value
)
{
    const std::string value = getEnvString(name);

    if (value.empty()) {
        return default_value;
    }

    try {
        const long parsed = std::stol(value);
        if (parsed > 0) {
            return parsed;
        }
    } catch (const std::exception&) {
    }

    return default_value;
}

static bool containsDuplicateFamilyNameError(const std::string& text)
{
    std::string lower = text;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );

    return lower.find("same family name") != std::string::npos ||
        lower.find("bad family name") != std::string::npos;
}

static bool isRemoteGateName(const std::string& name)
{
    return name == "send" ||
        name == "recv" ||
        name == "qsend" ||
        name == "qrecv" ||
        name == "expose" ||
        name == "rcontrol";
}

static bool isTruthyEnv(const char* name)
{
    const std::string value = getEnvString(name);
    if (value.empty()) {
        return false;
    }

    std::string lower = value;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );

    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

static bool mapCircuitIdsToQpus(
    const nlohmann::json& circuits_field,
    const std::unordered_map<std::string, std::string>& correspondence,
    std::vector<std::string>& qpus,
    std::string& error
)
{
    qpus.clear();
    error.clear();

    if (!circuits_field.is_array()) {
        error = "'circuits' field is not an array.";
        return false;
    }

    for (const auto& item : circuits_field) {
        if (!item.is_string()) {
            error = "'circuits' array contains non-string values.";
            return false;
        }

        const std::string circuit_id = item.get<std::string>();
        const auto it = correspondence.find(circuit_id);
        if (it == correspondence.end()) {
            error = "Circuit id '" + circuit_id + "' has no QPU assignment.";
            return false;
        }

        qpus.push_back(it->second);
    }

    return true;
}

static bool validateRemoteInstructionsHaveQpus(
    const nlohmann::json& instructions,
    std::string& error,
    const std::string& path = "instructions"
)
{
    if (!instructions.is_array()) {
        error = path + " is not an array.";
        return false;
    }

    for (std::size_t i = 0; i < instructions.size(); ++i) {
        const auto& instruction = instructions.at(i);

        if (!instruction.is_object()) {
            continue;
        }

        if (!instruction.contains("name") || !instruction.at("name").is_string()) {
            continue;
        }

        const std::string name = instruction.at("name").get<std::string>();
        const std::string instruction_path =
            path + "[" + std::to_string(i) + "] (" + name + ")";

        if (instruction.contains("circuits")) {
            error =
                "Instruction at " + instruction_path +
                " still contains 'circuits' after transformation.";
            return false;
        }

        if (!isRemoteGateName(name)) {
            continue;
        }

        if (!instruction.contains("qpus") || !instruction.at("qpus").is_array()) {
            error =
                "Remote instruction at " + instruction_path +
                " is missing a valid 'qpus' array.";
            return false;
        }

        if (instruction.at("qpus").empty()) {
            error =
                "Remote instruction at " + instruction_path +
                " has an empty 'qpus' array.";
            return false;
        }

        for (const auto& qpu : instruction.at("qpus")) {
            if (!qpu.is_string()) {
                error =
                    "Remote instruction at " + instruction_path +
                    " has non-string entries in 'qpus'.";
                return false;
            }
        }

        if (name == "rcontrol") {
            if (!instruction.contains("instructions")) {
                error =
                    "Remote instruction at " + instruction_path +
                    " is missing nested 'instructions'.";
                return false;
            }

            if (!validateRemoteInstructionsHaveQpus(
                    instruction.at("instructions"),
                    error,
                    instruction_path + ".instructions")) {
                return false;
            }
        }
    }

    return true;
}

static CunqaExecutionResult transformCircuitForCunqa(
    const std::string& raw_circuit_json,
    const std::unordered_map<std::string, std::string>& correspondence,
    std::string& transformed_circuit_json
)
{
    try {
        nlohmann::json circuit_json = nlohmann::json::parse(raw_circuit_json);

        if (!circuit_json.contains("instructions") ||
            !circuit_json.at("instructions").is_array()) {
            return makeCunqaError(
                9,
                "Circuit JSON does not contain a valid 'instructions' array."
            );
        }

        nlohmann::json transformed_instructions = nlohmann::json::array();

        for (const auto& instruction : circuit_json.at("instructions")) {
            if (!instruction.is_object()) {
                transformed_instructions.push_back(instruction);
                continue;
            }

            if (!instruction.contains("circuits")) {
                transformed_instructions.push_back(instruction);
                continue;
            }

            std::vector<std::string> mapped_qpus;
            std::string mapping_error;
            if (!mapCircuitIdsToQpus(
                    instruction.at("circuits"),
                    correspondence,
                    mapped_qpus,
                    mapping_error)) {
                return makeCunqaError(
                    9,
                    "Could not map communication circuits to QPUs: " + mapping_error
                );
            }

            const std::string name =
                instruction.contains("name") && instruction.at("name").is_string() ?
                    instruction.at("name").get<std::string>() :
                    "";

            if (isRemoteGateName(name)) {
                nlohmann::json remote_instruction = instruction;
                remote_instruction["qpus"] = mapped_qpus;
                remote_instruction.erase("circuits");
                transformed_instructions.push_back(remote_instruction);
                continue;
            }

            /*
             * Compatibility shim:
             * If a non-remote instruction references "circuits",
             * treat it as a remote-controlled block generated from expose().
             */
            nlohmann::json sub_instruction = instruction;
            sub_instruction.erase("circuits");

            if (!sub_instruction.contains("qubits") ||
                !sub_instruction.at("qubits").is_array()) {
                sub_instruction["qubits"] = nlohmann::json::array();
            }

            nlohmann::json remote_qubits = nlohmann::json::array();
            for (std::size_t i = 0; i < mapped_qpus.size(); ++i) {
                remote_qubits.push_back(-static_cast<int>(i + 1));
            }
            for (const auto& q : sub_instruction.at("qubits")) {
                remote_qubits.push_back(q);
            }
            sub_instruction["qubits"] = remote_qubits;

            nlohmann::json rcontrol_instruction;
            rcontrol_instruction["name"] = "rcontrol";
            rcontrol_instruction["qpus"] = mapped_qpus;
            rcontrol_instruction["instructions"] =
                nlohmann::json::array({sub_instruction});

            transformed_instructions.push_back(rcontrol_instruction);
        }

        circuit_json["instructions"] = transformed_instructions;

        if (circuit_json.contains("id") && circuit_json.at("id").is_string()) {
            const std::string original_id = circuit_json.at("id").get<std::string>();
            const auto id_it = correspondence.find(original_id);
            if (id_it != correspondence.end()) {
                circuit_json["id"] = id_it->second;
            }
        }

        if (!circuit_json.contains("config") || !circuit_json.at("config").is_object()) {
            return makeCunqaError(
                9,
                "Circuit JSON does not contain a valid 'config' object."
            );
        }

        /*
         * CUNQA dynamic simulators expect run options inside config.
         * If absent, mirror CUNQA Python defaults.
         */
        nlohmann::json& config_json = circuit_json["config"];
        if (!config_json.contains("method")) {
            if (circuit_json.contains("method") && circuit_json.at("method").is_string()) {
                config_json["method"] = circuit_json.at("method");
            } else {
                config_json["method"] = "automatic";
            }
        }

        if (!config_json.contains("avoid_parallelization")) {
            if (circuit_json.contains("avoid_parallelization") &&
                circuit_json.at("avoid_parallelization").is_boolean()) {
                config_json["avoid_parallelization"] = circuit_json.at("avoid_parallelization");
            } else {
                config_json["avoid_parallelization"] = false;
            }
        }

        if (!config_json.contains("seed") &&
            circuit_json.contains("seed") &&
            circuit_json.at("seed").is_number_integer()) {
            config_json["seed"] = circuit_json.at("seed");
        }

        if (circuit_json.contains("sending_to")) {
            std::vector<std::string> mapped_targets;
            std::string target_mapping_error;
            if (!mapCircuitIdsToQpus(
                    circuit_json.at("sending_to"),
                    correspondence,
                    mapped_targets,
                    target_mapping_error)) {
                return makeCunqaError(
                    9,
                    "Could not map 'sending_to' circuits to QPUs: " +
                        target_mapping_error
                );
            }

            circuit_json["sending_to"] = mapped_targets;
        }

        std::string validation_error;
        if (!validateRemoteInstructionsHaveQpus(
                circuit_json.at("instructions"),
                validation_error)) {
            return makeCunqaError(
                9,
                "Invalid remote instruction mapping for CUNQA: " +
                    validation_error
            );
        }

        transformed_circuit_json = circuit_json.dump();
        return {};
    } catch (const std::exception& ex) {
        return makeCunqaError(
            9,
            "Failed to transform circuit JSON for CUNQA: " +
                std::string(ex.what())
        );
    }
}

static void prependLdLibraryPathIfDir(const std::string& path)
{
    if (path.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        return;
    }

    const std::string current = getEnvString("LD_LIBRARY_PATH");

    if (!current.empty()) {
        const std::string token = ":" + path + ":";
        const std::string current_with_colons = ":" + current + ":";
        if (current_with_colons.find(token) != std::string::npos) {
            return;
        }
    }

    const std::string updated = current.empty() ? path : path + ":" + current;
    (void)setenv("LD_LIBRARY_PATH", updated.c_str(), 1);
}

static bool pathExists(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

static std::string joinCunqaQpusPath(const std::string& root)
{
    if (root.empty()) {
        return {};
    }

    return root + "/.cunqa/qpus.json";
}

static std::string resolveCunqaBuildDir()
{
    const std::string env_build_dir = getEnvString("QUANTUM_MPI_CUNQA_BUILD_DIR");
    if (!env_build_dir.empty()) {
        return env_build_dir;
    }

#ifdef QUANTUM_MPI_CUNQA_BUILD_DIR_COMPILED
    return QUANTUM_MPI_CUNQA_BUILD_DIR_COMPILED;
#else
    return {};
#endif
}

static std::string resolveCompilerRuntimeDir()
{
    const std::string env_runtime_dir = getEnvString("QUANTUM_MPI_CXX_RUNTIME_DIR");
    if (!env_runtime_dir.empty()) {
        return env_runtime_dir;
    }

#ifdef QUANTUM_MPI_CXX_RUNTIME_DIR_COMPILED
    return QUANTUM_MPI_CXX_RUNTIME_DIR_COMPILED;
#else
    return {};
#endif
}

static void ensureCunqaRuntimeEnvironment()
{
    prependLdLibraryPathIfDir(resolveCompilerRuntimeDir());

    const std::string build_dir = resolveCunqaBuildDir();
    if (build_dir.empty()) {
        return;
    }

    prependLdLibraryPathIfDir(build_dir + "/lib");
    prependLdLibraryPathIfDir(build_dir + "/_deps/libzmq-build/lib");
    prependLdLibraryPathIfDir(build_dir + "/_deps/maestro-build");
}

CunqaApi::CunqaApi(
    std::string family_name,
    std::string wall_time,
    std::string simulator,
    bool quantum_comm,
    bool co_located,
    std::string qpus_file_path
)
    : family_name(std::move(family_name)),
      wall_time(std::move(wall_time)),
      simulator(std::move(simulator)),
      quantum_comm(quantum_comm),
      co_located(co_located),
      qpus_file_path(std::move(qpus_file_path))
{
}

std::string CunqaApi::buildQraiseCommand(std::size_t num_qpus) const
{
    std::string command = "qraise";

    command.append(" -n ");
    command.append(std::to_string(num_qpus));

    command.append(" -t ");
    command.append(this->wall_time);

    command.append(" --simulator ");
    command.append(this->simulator);

    if (this->quantum_comm) {
        command.append(" --quantum_comm");
    }

    if (this->co_located) {
        command.append(" --co-located");
    }

    command.append(" --family_name ");
    command.append(this->family_name);

    return command;
}

std::string CunqaApi::buildQdropCommand() const
{
    std::string command = "qdrop";

    command.append(" --family_name ");
    command.append(this->family_name);

    return command;
}

std::string CunqaApi::resolveQpusFilePath() const
{
    if (!this->qpus_file_path.empty()) {
        return this->qpus_file_path;
    }

    const std::string env_qpus_file = getEnvString("CUNQA_QPUS_FILE");

    if (!env_qpus_file.empty()) {
        return env_qpus_file;
    }

    const std::string store = getEnvString("STORE");
    const std::string store_qpus = joinCunqaQpusPath(store);
    if (pathExists(store_qpus)) {
        return store_qpus;
    }

    const std::string project_dir = getEnvString("QUANTUM_MPI_PROJECT_DIR");
    const std::string project_qpus = joinCunqaQpusPath(project_dir);
    if (pathExists(project_qpus)) {
        return project_qpus;
    }

    const std::string cwd_qpus = ".cunqa/qpus.json";
    if (pathExists(cwd_qpus)) {
        return cwd_qpus;
    }

    const std::string home = getEnvString("HOME");

    if (!home.empty()) {
        return home + "/.cunqa/qpus.json";
    }

    return ".cunqa/qpus.json";
}

CunqaExecutionResult CunqaApi::loadQpuEndpoints(
    std::size_t num_qpus,
    std::vector<std::string>& endpoints,
    std::vector<std::string>& qpu_ids
) const
{
    endpoints.clear();
    qpu_ids.clear();

    const std::string qpus_path = this->resolveQpusFilePath();

    std::ifstream qpus_file(qpus_path);

    if (!qpus_file.is_open()) {
        return makeCunqaError(
            4,
            "Could not open CUNQA QPUs file: " + qpus_path +
            "\nSet CUNQA_QPUS_FILE or pass qpus_file_path to CunqaApi."
        );
    }

    nlohmann::json qpus_json;

    try {
        qpus_file >> qpus_json;
    } catch (const std::exception& ex) {
        return makeCunqaError(
            5,
            "Could not parse CUNQA QPUs file '" +
            qpus_path +
            "': " +
            std::string(ex.what())
        );
    }

    std::vector<nlohmann::json> qpu_entries;

    if (qpus_json.is_array()) {
        qpu_entries.reserve(qpus_json.size());
        for (const auto& entry : qpus_json) {
            qpu_entries.push_back(entry);
        }
    } else if (qpus_json.is_object()) {
        qpu_entries.reserve(qpus_json.size());
        for (const auto& item : qpus_json.items()) {
            nlohmann::json entry = item.value();
            if (!entry.contains("name")) {
                entry["__qpu_id"] = item.key();
            }
            qpu_entries.push_back(entry);
        }
    } else {
        return makeCunqaError(
            5,
            "CUNQA QPUs file has unsupported JSON type (expected array/object): " + qpus_path
        );
    }

    std::vector<nlohmann::json> filtered_qpus;
    filtered_qpus.reserve(qpu_entries.size());

    for (const auto& qpu_entry : qpu_entries) {
        if (!qpu_entry.is_object()) {
            continue;
        }

        if (!qpu_entry.contains("family")) {
            filtered_qpus.push_back(qpu_entry);
            continue;
        }

        if (!qpu_entry.at("family").is_string()) {
            continue;
        }

        if (qpu_entry.at("family").get<std::string>() == this->family_name) {
            filtered_qpus.push_back(qpu_entry);
        }
    }

    if (filtered_qpus.size() < num_qpus) {
        return makeCunqaError(
            6,
            "Not enough CUNQA QPUs. Required " +
            std::to_string(num_qpus) +
            ", available " +
            std::to_string(filtered_qpus.size()) +
            ". Family='" + this->family_name +
            "' qpus_file='" + qpus_path + "'" +
            "."
        );
    }

    try {
        endpoints.reserve(num_qpus);
        qpu_ids.reserve(num_qpus);

        for (std::size_t i = 0; i < num_qpus; ++i) {
            const auto& qpu_entry = filtered_qpus.at(i);

            endpoints.push_back(
                qpu_entry.at("net").at("endpoint").get<std::string>()
            );

            if (qpu_entry.contains("name") && qpu_entry.at("name").is_string()) {
                qpu_ids.push_back(qpu_entry.at("name").get<std::string>());
            } else if (qpu_entry.contains("__qpu_id") &&
                       qpu_entry.at("__qpu_id").is_string()) {
                qpu_ids.push_back(qpu_entry.at("__qpu_id").get<std::string>());
            } else {
                qpu_ids.push_back("qpu_" + std::to_string(i));
            }
        }
    } catch (const std::exception& ex) {
        return makeCunqaError(
            5,
            "Invalid CUNQA QPU endpoint format: " + std::string(ex.what())
        );
    }

    return {};
}

CunqaExecutionResult CunqaApi::run(
    std::vector<QuantumCircuit> circuits
) const
{
    const bool verbose = isTruthyEnv("QUANTUM_MPI_VERBOSE");

    quantum_mpi_ensure_cunqa_logger_ready();
    ensureCunqaRuntimeEnvironment();

    CunqaExecutionResult result;

    if (circuits.empty()) {
        return makeCunqaError(1, "Cannot execute an empty circuit list.");
    }

    const std::string qraise_command = this->buildQraiseCommand(circuits.size());
    std::cout << "[INFO] CUNQA request: family='" << this->family_name
              << "' requested_qpus=" << circuits.size() << std::endl;
    if (verbose) {
        std::cout << "[INFO] qraise command: " << qraise_command << std::endl;
    }

    CommandExecutionResult qraise_result =
        execute_command_capture(qraise_command);

    if (qraise_result.exit_code != 0 &&
        containsDuplicateFamilyNameError(qraise_result.output)) {
        const CommandExecutionResult qdrop_before_retry_result =
            execute_command_capture(this->buildQdropCommand());

        (void)qdrop_before_retry_result;

        qraise_result =
            execute_command_capture(qraise_command);
    }

    if (qraise_result.exit_code != 0) {
        return makeCunqaError(
            2,
            "qraise failed with exit code " +
            std::to_string(qraise_result.exit_code) +
            ". Output:\n" +
            qraise_result.output
        );
    }

    if (verbose && !trim(qraise_result.output).empty()) {
        std::cout << "[INFO] qraise output:\n" << qraise_result.output << std::endl;
    }

    std::vector<std::string> endpoints;
    std::vector<std::string> qpu_ids;

    const long endpoint_timeout_seconds =
        getEnvPositiveLong("QUANTUM_MPI_ENDPOINT_TIMEOUT_SECONDS", 300);

    const auto wait_started = std::chrono::steady_clock::now();
    CunqaExecutionResult endpoint_result;

    while (true) {
        endpoint_result = this->loadQpuEndpoints(
            circuits.size(),
            endpoints,
            qpu_ids
        );

        if (endpoint_result.status == 0) {
            break;
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - wait_started
            ).count();

        if (elapsed >= endpoint_timeout_seconds) {
            (void)execute_command_capture(this->buildQdropCommand());

            return makeCunqaError(
                8,
                "Timed out waiting for CUNQA QPU endpoints after " +
                    std::to_string(endpoint_timeout_seconds) +
                    " seconds.\nLast error: " +
                    endpoint_result.error +
                    "\nTip: increase QUANTUM_MPI_ENDPOINT_TIMEOUT_SECONDS."
            );
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (endpoint_result.status != 0) {
        (void)execute_command_capture(this->buildQdropCommand());
        return endpoint_result;
    }

    if (verbose) {
        std::cout << "[INFO] Resolved " << endpoints.size()
                  << " CUNQA endpoint(s) from " << this->resolveQpusFilePath()
                  << ":" << std::endl;
        for (std::size_t i = 0; i < endpoints.size(); ++i) {
            std::cout << "  - qpu_id=" << qpu_ids[i]
                      << " endpoint=" << endpoints[i] << std::endl;
        }
    }

    try {
        std::unordered_map<std::string, std::string> correspondence;
        correspondence.reserve(circuits.size());
        for (std::size_t i = 0; i < circuits.size() && i < qpu_ids.size(); ++i) {
            correspondence[circuits[i].getId()] = qpu_ids[i];
        }

        std::vector<cunqa::comm::Client> clients(circuits.size());

        for (std::size_t i = 0; i < circuits.size(); ++i) {
            clients[i].connect(endpoints[i]);
        }

        for (std::size_t i = 0; i < circuits.size(); ++i) {
            std::string transformed_circuit_json;
            const CunqaExecutionResult transform_result =
                transformCircuitForCunqa(
                    circuits[i].parseToRawQCJson(),
                    correspondence,
                    transformed_circuit_json
                );

            if (transform_result.status != 0) {
                (void)execute_command_capture(this->buildQdropCommand());
                return transform_result;
            }

            if (verbose) {
                std::cout << "[INFO] Sending transformed circuit #" << i
                          << " to endpoint " << endpoints[i] << ":\n"
                          << transformed_circuit_json << std::endl;
            }

            clients[i].send_circuit(transformed_circuit_json);
        }

        result.results.reserve(circuits.size());

        for (std::size_t i = 0; i < circuits.size(); ++i) {
            result.results.push_back(clients[i].recv_results());
        }
    } catch (const std::exception& ex) {
        (void)execute_command_capture(this->buildQdropCommand());

        return makeCunqaError(
            7,
            "CUNQA execution failed: " + std::string(ex.what())
        );
    }

    const CommandExecutionResult qdrop_result =
        execute_command_capture(this->buildQdropCommand());

    if (qdrop_result.exit_code != 0) {
        return makeCunqaError(
            3,
            "qdrop failed with exit code " +
            std::to_string(qdrop_result.exit_code) +
            ". Output:\n" +
            qdrop_result.output
        );
    }

    if (verbose && !trim(qdrop_result.output).empty()) {
        std::cout << "[INFO] qdrop output:\n" << qdrop_result.output << std::endl;
    }

    return result;
}
