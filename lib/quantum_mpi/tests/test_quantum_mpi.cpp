#include "quantum_mpi.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

void expect_equal_str(
    const std::string& lhs,
    const std::string& rhs,
    const std::string& message)
{
    if (lhs != rhs) {
        throw std::runtime_error(message + " (lhs='" + lhs + "', rhs='" + rhs + "')");
    }
}

void expect_throws(const std::function<void()>& fn, const std::string& message)
{
    bool thrown = false;
    try {
        fn();
    } catch (...) {
        thrown = true;
    }

    if (!thrown) {
        throw std::runtime_error(message);
    }
}

void test_command_launcher_parses_job_id()
{
    CommandJobLauncher launcher("printf 'Submitted batch job 412182\\n'");
    const std::string job_id = launcher.launch_job();
    expect_equal_str(job_id, "412182", "CommandJobLauncher did not parse expected job id");
}

void test_polling_endpoint_resolver_reads_callback()
{
    PollingEndpointResolver resolver(
        [](const std::string& job_id) -> std::string {
            if (job_id == "999999") {
                return "tcp://127.0.0.1:5555";
            }
            return "";
        });

    const std::string endpoint = resolver.wait_endpoint_for_job("999999", 2, 1);
    expect_equal_str(
        endpoint,
        "tcp://127.0.0.1:5555",
        "PollingEndpointResolver did not return expected endpoint");
}

void test_polling_endpoint_resolver_detects_terminal_state()
{
    PollingEndpointResolver resolver(
        [](const std::string&) -> std::string { return ""; },
        [](const std::string&) -> std::string { return "FAILED"; });

    expect_throws(
        [&]() {
            (void)resolver.wait_endpoint_for_job("123", 2, 1);
        },
        "PollingEndpointResolver should throw when terminal state is reported.");
}

void test_quantum_mpi_orchestrates_layers()
{
    const auto launcher = std::make_shared<CommandJobLauncher>("printf '412182\\n'");
    const auto resolver = std::make_shared<PollingEndpointResolver>(
        [](const std::string& job_id) -> std::string {
            if (job_id == "412182") {
                return "tcp://127.0.0.1:6000";
            }
            return "";
        });
    const auto transport = std::make_shared<FunctionTransport>(
        [](const std::string& endpoint, const std::string& payload) -> std::string {
            return endpoint + "|" + payload;
        });

    QuantumMPI mpi(launcher, resolver, transport);
    const std::string job_id = mpi.launchJob();
    expect_equal_str(job_id, "412182", "QuantumMPI::launchJob did not return expected job id.");

    const std::string endpoint = mpi.waitEndpointForJob(job_id, 2, 1);
    expect_equal_str(
        endpoint,
        "tcp://127.0.0.1:6000",
        "QuantumMPI::waitEndpointForJob did not return expected endpoint.");

    const std::string response = mpi.runPayload(endpoint, "{\"hello\":\"world\"}");
    expect_equal_str(
        response,
        "tcp://127.0.0.1:6000|{\"hello\":\"world\"}",
        "QuantumMPI::runPayload did not return expected response.");
}

int main()
{
    try {
        test_command_launcher_parses_job_id();
        test_polling_endpoint_resolver_reads_callback();
        test_polling_endpoint_resolver_detects_terminal_state();
        test_quantum_mpi_orchestrates_layers();
        std::cout << "All quantum_mpi tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return 1;
    }
}
