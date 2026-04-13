#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "quantum_circuit.hpp"
#include "quantum_mpi.hpp"
#if QUANTUM_MPI_WITH_CUNQA
#include "cunqa_runtime.hpp"
#endif

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

int main(int argc, char** argv)
{
#if !QUANTUM_MPI_WITH_CUNQA
    std::cerr
        << "This executable was built without CUNQA integration.\n"
        << "Reconfigure with -DQUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON to run distributed execution.\n";
    return 1;
#else
    try {
        CunqaRuntimeConfig runtime_config;
        const std::string qraise_cmd_from_cli = join_cli_args_as_command(argc, argv);
        if (!qraise_cmd_from_cli.empty()) {
            runtime_config.qraise_command = qraise_cmd_from_cli;
        }

        const std::string qraise_cmd = resolve_cunqa_qraise_command(runtime_config);
        std::cout << "Launching qraise: " << qraise_cmd << '\n';
        QuantumMPI quantum_mpi(
            make_cunqa_job_launcher(runtime_config),
            make_cunqa_endpoint_resolver(),
            make_cunqa_transport());

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
