#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "quantum_circuit.hpp"
#include "quantum_mpi.hpp"
#include "utils.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;

struct CliConfig {
    int qpus = 2;
    int qubits = 4;
    int shots = 1024;
    int repetitions = 1;
    std::string family_prefix = "distributed_qft_cpp";
};

struct ExperimentResult {
    double time_taken_mean = 0.0;
    double accuracy_mean = 0.0;
};

struct QubitLocation {
    int rank = 0;
    int local_idx = 0;
};

void print_usage(const char* program_name)
{
    std::cout
        << "Usage: " << program_name << " [options]\n"
        << "Options:\n"
        << "  --qpus <int>          Number of QPUs/ranks to use (default: 2)\n"
        << "  --qubits <int>        Total logical qubits in the global QFT (default: 4)\n"
        << "  --shots <int>         Shots per execution (default: 1024)\n"
        << "  --repetitions <int>   Repetitions for averaging metrics (default: 1)\n"
        << "  --family-prefix <str> Prefix used to build unique CUNQA family names\n"
        << "  --help                Show this help\n";
}

int parse_int_arg(const std::string& name, const std::string& value)
{
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            throw std::invalid_argument("contains trailing characters");
        }
        return parsed;
    } catch (const std::exception& ex) {
        throw std::invalid_argument(
            "Invalid integer for " + name + ": '" + value + "' (" + ex.what() + ").");
    }
}

CliConfig parse_cli(int argc, char** argv)
{
    CliConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }

        if (i + 1 >= argc) {
            throw std::invalid_argument("Missing value for argument: " + arg);
        }

        const std::string value = argv[++i];

        if (arg == "--qpus") {
            cfg.qpus = parse_int_arg("--qpus", value);
        } else if (arg == "--qubits") {
            cfg.qubits = parse_int_arg("--qubits", value);
        } else if (arg == "--shots") {
            cfg.shots = parse_int_arg("--shots", value);
        } else if (arg == "--repetitions") {
            cfg.repetitions = parse_int_arg("--repetitions", value);
        } else if (arg == "--family-prefix") {
            cfg.family_prefix = value;
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }

    if (cfg.qpus <= 0) {
        throw std::invalid_argument("--qpus must be > 0.");
    }
    if (cfg.qubits <= 0) {
        throw std::invalid_argument("--qubits must be > 0.");
    }
    if (cfg.shots <= 0) {
        throw std::invalid_argument("--shots must be > 0.");
    }
    if (cfg.repetitions <= 0) {
        throw std::invalid_argument("--repetitions must be > 0.");
    }
    if (cfg.qpus > cfg.qubits) {
        throw std::invalid_argument(
            "--qpus must be <= --qubits so each rank has at least one qubit.");
    }

    return cfg;
}

std::vector<int> build_qubit_partition(int total_qubits, int qpus)
{
    std::vector<int> partition(static_cast<std::size_t>(qpus), total_qubits / qpus);
    const int remainder = total_qubits % qpus;
    for (int i = 0; i < remainder; ++i) {
        partition[static_cast<std::size_t>(i)] += 1;
    }
    return partition;
}

std::vector<QubitLocation> build_global_mapping(const std::vector<int>& partition)
{
    const int total_qubits =
        std::accumulate(partition.begin(), partition.end(), 0);
    std::vector<QubitLocation> mapping(static_cast<std::size_t>(total_qubits));

    int global_q = 0;
    for (std::size_t rank = 0; rank < partition.size(); ++rank) {
        for (int local_q = 0; local_q < partition[rank]; ++local_q) {
            mapping[static_cast<std::size_t>(global_q)] = QubitLocation{
                static_cast<int>(rank),
                local_q
            };
            ++global_q;
        }
    }

    return mapping;
}

void add_instruction_checked(
    QuantumCircuit& circuit,
    const std::string& name,
    const std::vector<int>& qubits = {},
    const std::vector<int>& clbits = {},
    const std::vector<QuantumCircuit>& related_circuits = {},
    const std::vector<double>& params = {})
{
    const int status = circuit.addInstruction(
        name,
        qubits,
        clbits,
        related_circuits,
        params
    );

    if (status != 0) {
        throw std::runtime_error(
            "addInstruction failed for gate '" + name +
            "' with status " + std::to_string(status) + ".");
    }
}

std::vector<QuantumCircuit> build_distributed_qft_circuits(
    int qpus,
    int total_qubits,
    int shots)
{
    const std::vector<int> partition = build_qubit_partition(total_qubits, qpus);
    const std::vector<QubitLocation> qubit_map = build_global_mapping(partition);

    std::vector<QuantumCircuit> circuits;
    circuits.reserve(static_cast<std::size_t>(qpus));

    for (int rank = 0; rank < qpus; ++rank) {
        QuantumCircuit circuit(
            "rank_" + std::to_string(rank) + "_circuit",
            partition[static_cast<std::size_t>(rank)],
            partition[static_cast<std::size_t>(rank)]
        );
        circuit.setShots(shots);
        circuits.push_back(circuit);
    }

    /*
     * Input basis state with non-trivial phases after QFT.
     * QFT measurement distribution remains uniform for basis states.
     */
    for (int gq = 0; gq < total_qubits; ++gq) {
        if (gq % 2 == 1) {
            const auto loc = qubit_map[static_cast<std::size_t>(gq)];
            add_instruction_checked(
                circuits[static_cast<std::size_t>(loc.rank)],
                "x",
                {loc.local_idx}
            );
        }
    }

    /*
     * QFT (without final swaps):
     * For each qubit j, apply controlled phase rotations from lower-index qubits,
     * then apply H on j.
     */
    for (int j = 0; j < total_qubits; ++j) {
        const auto target_loc = qubit_map[static_cast<std::size_t>(j)];

        for (int k = 0; k < j; ++k) {
            const auto control_loc = qubit_map[static_cast<std::size_t>(k)];
            const int delta = j - k;
            const double theta = kPi / std::pow(2.0, static_cast<double>(delta));

            if (control_loc.rank == target_loc.rank) {
                add_instruction_checked(
                    circuits[static_cast<std::size_t>(target_loc.rank)],
                    "crz",
                    {control_loc.local_idx, target_loc.local_idx},
                    {},
                    {},
                    {theta}
                );
            } else {
                add_instruction_checked(
                    circuits[static_cast<std::size_t>(control_loc.rank)],
                    "expose",
                    {control_loc.local_idx},
                    {},
                    {circuits[static_cast<std::size_t>(target_loc.rank)]}
                );

                add_instruction_checked(
                    circuits[static_cast<std::size_t>(target_loc.rank)],
                    "crz",
                    {target_loc.local_idx},
                    {},
                    {circuits[static_cast<std::size_t>(control_loc.rank)]},
                    {theta}
                );
            }
        }

        add_instruction_checked(
            circuits[static_cast<std::size_t>(target_loc.rank)],
            "h",
            {target_loc.local_idx}
        );
    }

    for (int gq = 0; gq < total_qubits; ++gq) {
        const auto loc = qubit_map[static_cast<std::size_t>(gq)];
        add_instruction_checked(
            circuits[static_cast<std::size_t>(loc.rank)],
            "measure",
            {loc.local_idx},
            {loc.local_idx}
        );
    }

    return circuits;
}

double compute_accuracy_from_result(const nlohmann::json& result_json)
{
    if (!result_json.contains("counts") || !result_json.at("counts").is_object()) {
        return 0.0;
    }

    const auto& counts = result_json.at("counts");
    if (counts.empty()) {
        return 0.0;
    }

    std::size_t n_bits = 0;
    for (const auto& item : counts.items()) {
        n_bits = std::max(n_bits, item.key().size());
    }
    if (n_bits == 0) {
        return 1.0;
    }

    std::vector<double> ones(n_bits, 0.0);
    double total = 0.0;

    for (const auto& item : counts.items()) {
        const std::string bitstring = item.key();
        const double c = item.value().get<double>();
        total += c;
        for (std::size_t i = 0; i < bitstring.size(); ++i) {
            if (bitstring[i] == '1') {
                ones[i] += c;
            }
        }
    }

    if (total <= 0.0) {
        return 0.0;
    }

    double acc_sum = 0.0;
    for (double ones_count : ones) {
        const double p1 = ones_count / total;
        const double bit_acc = std::max(0.0, 1.0 - 2.0 * std::fabs(p1 - 0.5));
        acc_sum += bit_acc;
    }

    return acc_sum / static_cast<double>(n_bits);
}

ExperimentResult run_experiment(
    int qpus,
    int total_qubits,
    int shots,
    int repetitions,
    const std::string& family_prefix)
{
    double total_time = 0.0;
    double total_accuracy = 0.0;

    for (int rep = 0; rep < repetitions; ++rep) {
        auto circuits = build_distributed_qft_circuits(qpus, total_qubits, shots);

        const std::string family_name = build_unique_family_name(
            family_prefix + "_p" + std::to_string(qpus) + "_r" + std::to_string(rep));

        auto api = std::make_shared<CunqaApi>(
            family_name,
            "00:10:00",
            "Aer",
            true,
            true
        );

        QuantumMPI mpi(0, qpus, api, "world_qft_" + family_name);
        mpi.init();
        const CunqaExecutionResult execution = mpi.run_distributed(std::move(circuits));
        mpi.finalize();

        if (execution.status != 0) {
            throw std::runtime_error(
                "distributed_qft execution failed (qpus=" + std::to_string(qpus) +
                ", repetition=" + std::to_string(rep) + "): " + execution.error);
        }

        if (execution.results.empty()) {
            throw std::runtime_error("distributed_qft execution returned no results.");
        }

        double repetition_time = 0.0;
        double repetition_acc = 0.0;
        for (const auto& result_str : execution.results) {
            const nlohmann::json result_json = nlohmann::json::parse(result_str);
            if (result_json.contains("time_taken") &&
                result_json.at("time_taken").is_number()) {
                repetition_time = std::max(
                    repetition_time,
                    result_json.at("time_taken").get<double>());
            }
            repetition_acc += compute_accuracy_from_result(result_json);
        }

        repetition_acc /= static_cast<double>(execution.results.size());

        total_time += repetition_time;
        total_accuracy += repetition_acc;
    }

    return ExperimentResult{
        total_time / static_cast<double>(repetitions),
        total_accuracy / static_cast<double>(repetitions)
    };
}
} // namespace

int main(int argc, char** argv)
{
    try {
        const CliConfig cfg = parse_cli(argc, argv);

        const ExperimentResult baseline = run_experiment(
            1,
            cfg.qubits,
            cfg.shots,
            cfg.repetitions,
            cfg.family_prefix
        );

        const ExperimentResult target = (cfg.qpus == 1) ?
            baseline :
            run_experiment(
                cfg.qpus,
                cfg.qubits,
                cfg.shots,
                cfg.repetitions,
                cfg.family_prefix
            );

        const double speedup = (target.time_taken_mean > 0.0) ?
            baseline.time_taken_mean / target.time_taken_mean :
            0.0;

        nlohmann::json summary = {
            {"algorithm", "distributed_qft_no_swap"},
            {"num_qpus", cfg.qpus},
            {"num_qubits", cfg.qubits},
            {"shots", cfg.shots},
            {"repetitions", cfg.repetitions},
            {"baseline_1qpu", {
                {"time_taken_mean", baseline.time_taken_mean},
                {"accuracy_mean", baseline.accuracy_mean}
            }},
            {"target", {
                {"time_taken_mean", target.time_taken_mean},
                {"accuracy_mean", target.accuracy_mean}
            }},
            {"speedup_vs_1qpu", speedup}
        };

        std::cout << summary.dump(2) << '\n';
        std::cout << "num_qpus,time_taken_mean,accuracy_mean,speedup_vs_1qpu\n";
        std::cout << cfg.qpus << ","
                  << target.time_taken_mean << ","
                  << target.accuracy_mean << ","
                  << speedup << '\n';

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "distributed_qft error: " << ex.what() << '\n';
        return 1;
    }
}
