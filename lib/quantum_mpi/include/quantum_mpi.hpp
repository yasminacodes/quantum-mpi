#ifndef QUANTUM_MPI_HPP
#define QUANTUM_MPI_HPP

#include <functional>
#include <memory>
#include <string>

struct CommandExecutionResult {
    int exit_code = -1;
    std::string output;
};

class IJobLauncher {
public:
    virtual ~IJobLauncher() = default;
    virtual std::string launch_job() const = 0;
};

class IEndpointResolver {
public:
    virtual ~IEndpointResolver() = default;
    virtual std::string wait_endpoint_for_job(
        const std::string& job_id,
        int timeout_seconds,
        int poll_seconds) const = 0;
};

class IQuantumMPITransport {
public:
    virtual ~IQuantumMPITransport() = default;
    virtual std::string send(
        const std::string& endpoint,
        const std::string& payload) const = 0;
};

class QuantumMPI {
private:
    std::shared_ptr<IJobLauncher> launcher_;
    std::shared_ptr<IEndpointResolver> resolver_;
    std::shared_ptr<IQuantumMPITransport> transport_;

public:
    QuantumMPI(
        std::shared_ptr<IJobLauncher> launcher,
        std::shared_ptr<IEndpointResolver> resolver,
        std::shared_ptr<IQuantumMPITransport> transport);

    std::string launchJob() const;
    std::string waitEndpointForJob(
        const std::string& job_id,
        int timeout_seconds = 120,
        int poll_seconds = 2) const;
    std::string runPayload(
        const std::string& endpoint,
        const std::string& payload) const;

    // Backward-compatible aliases with previous API naming.
    std::string launchQraise() const;
    std::string runCircuitJson(
        const std::string& endpoint,
        const std::string& circuit_json) const;
};

class CommandJobLauncher : public IJobLauncher {
public:
    using JobIdParser = std::function<std::string(const std::string&)>;

    explicit CommandJobLauncher(
        std::string launch_command,
        JobIdParser parser = JobIdParser());

    std::string launch_job() const override;

    static std::string parse_digits_line_or_last_number(const std::string& text);

private:
    std::string launch_command_;
    JobIdParser parser_;
};

class PollingEndpointResolver : public IEndpointResolver {
public:
    // Returns endpoint when available, or an empty string while it is still unavailable.
    using EndpointLookup = std::function<std::string(const std::string& job_id)>;
    // Returns empty string while active/unknown. Non-empty string means terminal state reached.
    using JobStateLookup = std::function<std::string(const std::string& job_id)>;

    explicit PollingEndpointResolver(
        EndpointLookup endpoint_lookup,
        JobStateLookup job_state_lookup = JobStateLookup());

    std::string wait_endpoint_for_job(
        const std::string& job_id,
        int timeout_seconds,
        int poll_seconds) const override;

private:
    EndpointLookup endpoint_lookup_;
    JobStateLookup job_state_lookup_;
};

class FunctionTransport : public IQuantumMPITransport {
public:
    using Sender = std::function<std::string(const std::string&, const std::string&)>;

    explicit FunctionTransport(Sender sender);
    std::string send(const std::string& endpoint, const std::string& payload) const override;

private:
    Sender sender_;
};

CommandExecutionResult execute_command_capture(const std::string& command);

#endif
