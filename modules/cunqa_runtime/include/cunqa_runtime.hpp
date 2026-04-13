#ifndef CUNQA_RUNTIME_HPP
#define CUNQA_RUNTIME_HPP

#include <memory>
#include <string>

#include "quantum_mpi.hpp"

struct CunqaRuntimeConfig {
    std::string qraise_command;
    std::string qraise_command_env_var = "QUANTUM_MPI_QRAISE_COMMAND";
};

std::string resolve_cunqa_qraise_command(
    const CunqaRuntimeConfig& config = CunqaRuntimeConfig());

std::shared_ptr<IJobLauncher> make_cunqa_job_launcher(
    const CunqaRuntimeConfig& config = CunqaRuntimeConfig());

std::shared_ptr<IEndpointResolver> make_cunqa_endpoint_resolver();

std::shared_ptr<IQuantumMPITransport> make_cunqa_transport();

#endif
