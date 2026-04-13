#include "quantum_mpi.hpp"

#include <stdexcept>
#include <utility>

FunctionTransport::FunctionTransport(Sender sender) :
    sender_(std::move(sender))
{
    if (!sender_) {
        throw std::invalid_argument("Transport sender callback cannot be empty.");
    }
}

std::string FunctionTransport::send(
    const std::string& endpoint,
    const std::string& payload) const
{
    return sender_(endpoint, payload);
}
