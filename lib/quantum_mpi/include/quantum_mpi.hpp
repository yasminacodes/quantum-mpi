#ifndef QUANTUM_MPI_HPP
#define QUANTUM_MPI_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cunqa_api.hpp"

enum class QuantumMPIReduceOp {
    Sum,
    Product,
    Min,
    Max
};

struct QuantumMPIMessage {
    int source_rank;
    int destination_rank;
    int tag;
    std::string payload;
};

class QuantumCircuit;

class QuantumMPI {
public:
    QuantumMPI(
        int rank,
        int size,
        std::shared_ptr<CunqaApi> cunqa_api,
        std::string world_id = "world_default");

    void init();
    void finalize();

    int rank() const;
    int size() const;
    const std::string& job_id() const;
    const std::string& endpoint() const;

    void barrier();

    void send(
        int destination_rank,
        const std::string& payload,
        int tag = 0);

    QuantumMPIMessage recv(
        int source_rank,
        int tag = 0);

    void bcast(
        std::string& payload,
        int root_rank);

    std::string reduce(
        const std::string& local_payload,
        QuantumMPIReduceOp op,
        int root_rank);

    std::string run_circuit(
        QuantumCircuit& circuit,
        int destination_rank = 0,
        int tag = 0);

    CunqaExecutionResult run_distributed(
        std::vector<QuantumCircuit> circuits);

private:
    int rank_;
    int size_;
    std::shared_ptr<CunqaApi> cunqa_api_;
    std::string world_id_;
    bool initialized_;
    std::string job_id_;
    std::string endpoint_;
    std::vector<QuantumMPIMessage> mailbox_;
};

#endif
