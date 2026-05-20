#include "quantum_mpi.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "quantum_circuit.hpp"

void expect_true(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_equal(const std::string& lhs, const std::string& rhs, const std::string& message)
{
    if (lhs != rhs) {
        throw std::runtime_error(message + " (lhs='" + lhs + "', rhs='" + rhs + "')");
    }
}

void expect_equal_int(int lhs, int rhs, const std::string& message)
{
    if (lhs != rhs) {
        throw std::runtime_error(
            message + " (lhs=" + std::to_string(lhs) + ", rhs=" + std::to_string(rhs) + ")");
    }
}

void expect_near(double lhs, double rhs, double tolerance, const std::string& message)
{
    if (std::fabs(lhs - rhs) > tolerance) {
        throw std::runtime_error(
            message + " (lhs=" + std::to_string(lhs) + ", rhs=" + std::to_string(rhs) +
            ", tol=" + std::to_string(tolerance) + ")");
    }
}

std::shared_ptr<CunqaApi> build_test_api(const std::string& family)
{
    return std::make_shared<CunqaApi>(
        family,
        "00:01:00",
        "Aer",
        true,
        true
    );
}

void test_constructor_validation()
{
    auto api = build_test_api("qmpi_ctor");
    bool thrown = false;

    thrown = false;
    try {
        QuantumMPI mpi(0, 0, api);
        (void)mpi;
    } catch (...) {
        thrown = true;
    }
    expect_true(thrown, "QuantumMPI should reject non-positive size.");

    thrown = false;
    try {
        QuantumMPI mpi(-1, 1, api);
        (void)mpi;
    } catch (...) {
        thrown = true;
    }
    expect_true(thrown, "QuantumMPI should reject negative rank.");

    thrown = false;
    try {
        QuantumMPI mpi(2, 2, api);
        (void)mpi;
    } catch (...) {
        thrown = true;
    }
    expect_true(thrown, "QuantumMPI should reject rank >= size.");

    thrown = false;
    try {
        std::shared_ptr<CunqaApi> null_api;
        QuantumMPI mpi(0, 1, null_api);
        (void)mpi;
    } catch (...) {
        thrown = true;
    }
    expect_true(thrown, "QuantumMPI should reject null CunqaApi.");

    thrown = false;
    try {
        QuantumMPI mpi(0, 1, api, "");
        (void)mpi;
    } catch (...) {
        thrown = true;
    }
    expect_true(thrown, "QuantumMPI should reject empty world_id.");
}

void test_init_finalize_rank_size()
{
    auto api = build_test_api("qmpi_init");
    QuantumMPI mpi(0, 1, api, "world_init");

    expect_equal_int(mpi.rank(), 0, "rank() mismatch.");
    expect_equal_int(mpi.size(), 1, "size() mismatch.");

    mpi.init();
    mpi.init();
    expect_equal(mpi.job_id(), "", "job_id() should be empty in this v1.");
    expect_equal(mpi.endpoint(), "", "endpoint() should be empty in this v1.");

    mpi.finalize();
    expect_equal(mpi.job_id(), "", "job_id() should be empty after finalize.");
    expect_equal(mpi.endpoint(), "", "endpoint() should be empty after finalize.");
    mpi.finalize();
}

void test_send_recv_roundtrip()
{
    auto api = build_test_api("qmpi_sendrecv");
    QuantumMPI sender(0, 2, api, "world_sendrecv");
    QuantumMPI receiver(1, 2, api, "world_sendrecv");
    sender.init();
    receiver.init();

    sender.send(1, "payload", 7);
    const QuantumMPIMessage msg = receiver.recv(0, 7);

    expect_true(
        msg.payload == "payload",
        "Unexpected recv payload.");
    expect_equal_int(msg.source_rank, 0, "Unexpected source rank.");
    expect_equal_int(msg.destination_rank, 1, "Unexpected destination rank.");
    expect_equal_int(msg.tag, 7, "Unexpected tag.");
}

void test_recv_missing_message_throws()
{
    auto api = build_test_api("qmpi_recv_missing");
    QuantumMPI mpi(0, 1, api, "world_recv_missing");
    mpi.init();

    bool thrown = false;
    try {
        (void)mpi.recv(0, 123);
    } catch (...) {
        thrown = true;
    }
    expect_true(thrown, "recv() should throw when message is not available.");
}

void test_bcast_to_two_receivers()
{
    auto api = build_test_api("qmpi_bcast");
    QuantumMPI root(0, 3, api, "world_bcast");
    QuantumMPI rank1(1, 3, api, "world_bcast");
    QuantumMPI rank2(2, 3, api, "world_bcast");
    root.init();
    rank1.init();
    rank2.init();

    std::string payload_root = "hello-bcast";
    root.bcast(payload_root, 0);

    std::string payload_1;
    std::string payload_2;
    rank1.bcast(payload_1, 0);
    rank2.bcast(payload_2, 0);

    expect_equal(payload_1, "hello-bcast", "rank1 should receive bcast payload.");
    expect_equal(payload_2, "hello-bcast", "rank2 should receive bcast payload.");
}

void test_reduce_sum_three_ranks()
{
    auto api = build_test_api("qmpi_reduce_sum");
    QuantumMPI root(0, 3, api, "world_reduce_sum");
    QuantumMPI rank1(1, 3, api, "world_reduce_sum");
    QuantumMPI rank2(2, 3, api, "world_reduce_sum");
    root.init();
    rank1.init();
    rank2.init();

    (void)rank1.reduce("2.0", QuantumMPIReduceOp::Sum, 0);
    (void)rank2.reduce("3.0", QuantumMPIReduceOp::Sum, 0);
    const std::string reduced = root.reduce("1.5", QuantumMPIReduceOp::Sum, 0);

    const double value = std::stod(reduced);
    expect_near(value, 6.5, 1e-9, "reduce(sum) produced unexpected value.");
}

void test_run_distributed_preconditions()
{
    auto api = build_test_api("qmpi_run_distributed");
    QuantumMPI mpi(0, 2, api, "world_run_distributed");

    QuantumCircuit c0("c0", 1, 1);
    QuantumCircuit c1("c1", 1, 1);

    const CunqaExecutionResult before_init = mpi.run_distributed({c0, c1});
    expect_true(
        before_init.status != 0,
        "run_distributed should fail before init().");

    mpi.init();
    const CunqaExecutionResult wrong_size = mpi.run_distributed({c0});
    expect_true(
        wrong_size.status != 0,
        "run_distributed should fail when circuits.size() != size().");
}

int main()
{
    try {
        test_constructor_validation();
        test_init_finalize_rank_size();
        test_send_recv_roundtrip();
        test_recv_missing_message_throws();
        test_bcast_to_two_receivers();
        test_reduce_sum_three_ranks();
        test_run_distributed_preconditions();
        std::cout << "All quantum_mpi tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return 1;
    }
}
