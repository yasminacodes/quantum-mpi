#ifndef QUANTUM_CIRCUIT_HPP
#define QUANTUM_CIRCUIT_HPP

#include <vector>
#include <string>
#include <bits/stdc++.h>

struct QuantumInstruction {
    std::string name;
    std::vector<int> qubits_idx;
    std::vector<int> clbits_idx;
    std::vector<std::string> circuits;
    std::vector<double> params;
};

class QuantumCircuit {
    private:
        std::string id;
        int num_qubits = 0;
        int num_clbits = 0;

        std::vector<QuantumInstruction> instructions;

        int shots = 1024;
        std::string device_name = "CPU";
        std::vector<int> target_devices = {};
        std::vector<std::string> sending_to = {};
        bool is_dynamic = false;
        std::string method = "";
        int seed = -1;
        bool avoid_parallelization = false;

        void checkCommAndSetDynamic();

    public:
        QuantumCircuit(
            std::string id, 
            int num_qubits = 0, 
            int num_clbits = 0
        );

        std::string getId();
        void setShots(int shots);
        int getShots();
        void setDynamic (bool dynamic);
        bool getDynamic();

        int addInstruction(
            std::string name, 
            std::vector<int> qubits_idx = {}, 
            std::vector<int> clbits_idx = {}, 
            std::vector<QuantumCircuit> circuits = {},
            std::vector<double> params = {});        

        std::string parseToRawQCJson();
};

#endif
