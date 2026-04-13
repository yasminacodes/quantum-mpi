#ifndef QUANTUM_CIRCUIT_HPP
#define QUANTUM_CIRCUIT_HPP

#include <vector>
#include <string>
#include <bits/stdc++.h>

struct QuantumCircuitConfig {
    int shots = -1;
    int num_qubits = 0;
    int num_clbits = 0;
    std::string device_name = "";
    std::vector<int> target_devices = {};
};

struct QuantumCircuitOtherConfig {
    std::vector<std::string> sending_to = {};
    bool is_dynamic = false;
    std::string method = "";
    int seed = -1;
    bool avoid_parallelization = false;
};

struct QuantumInstruction {
    std::string name;
    std::vector<int> qubits_idx;
    std::vector<int> clbits_idx;
};

class QuantumCircuit {
    private:
        std::string id;
        std::vector<QuantumInstruction> instructions;
        QuantumCircuitConfig config;
        QuantumCircuitOtherConfig other_config;

    public:
    QuantumCircuit(
        std::string id, int num_qubits = 0, int num_clbits = 0,
        int shots = -1, std::string device_name = "", std::vector<int> target_devices = {},
        bool is_dynamic = false, std::vector<std::string> sending_to = {}, std::string method = "",
        int seed = -1, bool avoid_parallelization = false
    );
    int addInstruction(std::string name, std::vector<int> qubits_idx, std::vector<int> clbits_idx);
    void clearInstructions();
    std::string parseToRawQCJson();
};

#endif
