#include "quantum_mpi.hpp"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "quantum_circuit.hpp"

namespace {
std::mutex g_mailbox_mutex;
std::unordered_map<std::string, std::vector<QuantumMPIMessage>> g_world_mailboxes;

bool parse_numeric_payload(const std::string& payload, double& value_out)
{
    try {
        std::size_t consumed = 0;
        const double value = std::stod(payload, &consumed);
        if (consumed != payload.size()) {
            return false;
        }
        value_out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string format_numeric_payload(double value)
{
    std::ostringstream oss;
    oss.precision(17);
    oss << value;
    return oss.str();
}
} // namespace

QuantumMPI::QuantumMPI(
    int rank,
    int size,
    std::shared_ptr<CunqaApi> cunqa_api,
    std::string world_id) :
    rank_(rank),
    size_(size),
    cunqa_api_(std::move(cunqa_api)),
    world_id_(std::move(world_id)),
    initialized_(false)
{
    if (size_ <= 0) {
        throw std::invalid_argument("QuantumMPI size must be positive.");
    }

    if (rank_ < 0 || rank_ >= size_) {
        throw std::invalid_argument("QuantumMPI rank must be in [0, size).");
    }

    if (!cunqa_api_) {
        throw std::invalid_argument("QuantumMPI requires a non-null CunqaApi instance.");
    }

    if (world_id_.empty()) {
        throw std::invalid_argument("QuantumMPI world_id cannot be empty.");
    }
}

void QuantumMPI::init()
{
    if (initialized_) {
        return;
    }

    /*
     * CunqaApi now owns CUNQA execution setup.
     * QuantumMPI no longer launches a CUNQA job during init().
     */
    initialized_ = true;
}

void QuantumMPI::finalize()
{
    if (!initialized_) {
        return;
    }

    initialized_ = false;
    job_id_.clear();
    endpoint_.clear();
    {
        std::lock_guard<std::mutex> lock(g_mailbox_mutex);
        auto mailbox_it = g_world_mailboxes.find(world_id_);
        if (mailbox_it != g_world_mailboxes.end()) {
            auto& mailbox = mailbox_it->second;
            mailbox.erase(
                std::remove_if(
                    mailbox.begin(),
                    mailbox.end(),
                    [&](const QuantumMPIMessage& message) {
                        return message.source_rank == rank_ ||
                            message.destination_rank == rank_;
                    }),
                mailbox.end());
            if (mailbox.empty()) {
                g_world_mailboxes.erase(mailbox_it);
            }
        }
    }
    mailbox_.clear();
}

int QuantumMPI::rank() const
{
    return rank_;
}

int QuantumMPI::size() const
{
    return size_;
}

const std::string& QuantumMPI::job_id() const
{
    return job_id_;
}

const std::string& QuantumMPI::endpoint() const
{
    return endpoint_;
}

void QuantumMPI::barrier()
{
    if (!initialized_) {
        throw std::runtime_error("QuantumMPI::barrier called before init().");
    }
}

void QuantumMPI::send(
    int destination_rank,
    const std::string& payload,
    int tag)
{
    if (!initialized_) {
        throw std::runtime_error("QuantumMPI::send called before init().");
    }

    if (destination_rank < 0 || destination_rank >= size_) {
        throw std::out_of_range("QuantumMPI destination rank out of range.");
    }

    if (payload.empty()) {
        throw std::invalid_argument("QuantumMPI payload cannot be empty.");
    }

    std::lock_guard<std::mutex> lock(g_mailbox_mutex);
    g_world_mailboxes[world_id_].push_back(
        QuantumMPIMessage{rank_, destination_rank, tag, payload});
}

QuantumMPIMessage QuantumMPI::recv(
    int source_rank,
    int tag)
{
    if (!initialized_) {
        throw std::runtime_error("QuantumMPI::recv called before init().");
    }

    if (source_rank < 0 || source_rank >= size_) {
        throw std::out_of_range("QuantumMPI source rank out of range.");
    }

    std::lock_guard<std::mutex> lock(g_mailbox_mutex);
    auto& mailbox = g_world_mailboxes[world_id_];

    const auto it = std::find_if(
        mailbox.begin(),
        mailbox.end(),
        [&](const QuantumMPIMessage& msg) {
            return msg.source_rank == source_rank &&
                msg.destination_rank == rank_ &&
                msg.tag == tag;
        });

    if (it == mailbox.end()) {
        throw std::runtime_error("QuantumMPI::recv could not find a matching message.");
    }

    QuantumMPIMessage message = *it;
    mailbox.erase(it);

    return message;
}

void QuantumMPI::bcast(
    std::string& payload,
    int root_rank)
{
    if (!initialized_) {
        throw std::runtime_error("QuantumMPI::bcast called before init().");
    }

    if (root_rank < 0 || root_rank >= size_) {
        throw std::out_of_range("QuantumMPI root rank out of range.");
    }

    if (size_ == 1) {
        return;
    }

    if (rank_ == root_rank) {
        for (int dst = 0; dst < size_; ++dst) {
            if (dst == root_rank) {
                continue;
            }

            send(dst, payload, 0);
        }

        return;
    }

    const QuantumMPIMessage message = recv(root_rank, 0);
    payload = message.payload;
}

std::string QuantumMPI::reduce(
    const std::string& local_payload,
    QuantumMPIReduceOp op,
    int root_rank)
{
    if (!initialized_) {
        throw std::runtime_error("QuantumMPI::reduce called before init().");
    }

    if (root_rank < 0 || root_rank >= size_) {
        throw std::out_of_range("QuantumMPI root rank out of range.");
    }

    constexpr int reduce_tag = -404;

    if (rank_ != root_rank) {
        send(root_rank, local_payload, reduce_tag);
        return local_payload;
    }

    double local_value = 0.0;
    if (!parse_numeric_payload(local_payload, local_value)) {
        throw std::invalid_argument(
            "QuantumMPI::reduce expects numeric payloads for this v1 implementation.");
    }

    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(size_));
    values.push_back(local_value);

    for (int source = 0; source < size_; ++source) {
        if (source == root_rank) {
            continue;
        }
        const QuantumMPIMessage message = recv(source, reduce_tag);
        double source_value = 0.0;
        if (!parse_numeric_payload(message.payload, source_value)) {
            throw std::invalid_argument(
                "QuantumMPI::reduce found non-numeric payload in mailbox.");
        }
        values.push_back(source_value);
    }

    double reduced = values.front();
    for (std::size_t i = 1; i < values.size(); ++i) {
        switch (op) {
        case QuantumMPIReduceOp::Sum:
            reduced += values[i];
            break;
        case QuantumMPIReduceOp::Product:
            reduced *= values[i];
            break;
        case QuantumMPIReduceOp::Min:
            reduced = std::min(reduced, values[i]);
            break;
        case QuantumMPIReduceOp::Max:
            reduced = std::max(reduced, values[i]);
            break;
        }
    }

    return format_numeric_payload(reduced);
}

std::string QuantumMPI::run_circuit(
    QuantumCircuit& circuit,
    int destination_rank,
    int tag)
{
    if (!initialized_) {
        throw std::runtime_error("QuantumMPI::run_circuit called before init().");
    }

    if (destination_rank < 0 || destination_rank >= size_) {
        throw std::out_of_range("QuantumMPI destination rank out of range.");
    }

    (void)tag;

    const CunqaExecutionResult execution = this->run_distributed({circuit});

    if (execution.status != 0) {
        throw std::runtime_error(
            "QuantumMPI::run_circuit failed: " + execution.error);
    }

    if (execution.results.empty()) {
        throw std::runtime_error(
            "QuantumMPI::run_circuit returned no CUNQA results.");
    }

    return execution.results.front();
}

CunqaExecutionResult QuantumMPI::run_distributed(
    std::vector<QuantumCircuit> circuits)
{
    if (!initialized_) {
        return CunqaExecutionResult{
            1,
            "QuantumMPI::run_distributed called before init().",
            {}
        };
    }

    if (circuits.empty()) {
        return CunqaExecutionResult{
            1,
            "QuantumMPI::run_distributed received an empty circuit list.",
            {}
        };
    }

    if (circuits.size() != static_cast<std::size_t>(size_)) {
        return CunqaExecutionResult{
            1,
            "QuantumMPI::run_distributed requires circuits.size() == size()."
            " circuits.size()=" + std::to_string(circuits.size()) +
                " size()=" + std::to_string(size_),
            {}
        };
    }

    return cunqa_api_->run(std::move(circuits));
}
