#ifndef CUNQA_API_HPP
#define CUNQA_API_HPP

#include <string>
#include <vector>

#include "quantum_circuit.hpp"

struct CunqaExecutionResult {
    int status = 0;
    std::string error;
    std::vector<std::string> results;
};

class CunqaApi {
public:
    explicit CunqaApi(
        std::string family_name = "simple_cunqa_cpp",
        std::string wall_time = "00:10:00",
        std::string simulator = "Aer",
        bool quantum_comm = true,
        bool co_located = true,
        std::string qpus_file_path = ""
    );

    CunqaExecutionResult run(
        std::vector<QuantumCircuit> circuits
    ) const;

private:
    std::string family_name;
    std::string wall_time;
    std::string simulator;
    bool quantum_comm;
    bool co_located;
    std::string qpus_file_path;

    std::string buildQraiseCommand(std::size_t num_qpus) const;
    std::string buildQdropCommand() const;
    std::string resolveQpusFilePath() const;

    CunqaExecutionResult loadQpuEndpoints(
        std::size_t num_qpus,
        std::vector<std::string>& endpoints,
        std::vector<std::string>& qpu_ids
    ) const;
};

#endif
