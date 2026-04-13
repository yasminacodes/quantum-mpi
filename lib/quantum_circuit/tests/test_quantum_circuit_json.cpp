#include <iostream>
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

void expect_contains(const std::string& text, const std::string& needle)
{
    expect_true(
        text.find(needle) != std::string::npos,
        "Missing JSON fragment: " + needle);
}

void expect_ordered_contains(const std::string& text, const std::vector<std::string>& needles)
{
    std::size_t pos = 0;
    for (const auto& needle : needles) {
        const std::size_t found = text.find(needle, pos);
        expect_true(found != std::string::npos, "Missing JSON fragment in order: " + needle);
        pos = found + needle.size();
    }
}

void test_basic_h_measure_json()
{
    QuantumCircuit circuit("basic_h_measure", 1, 1, 1024, "CPU");
    expect_true(circuit.addInstruction("h", {0}, {}) == 0, "Failed to add h gate");
    expect_true(circuit.addInstruction("measure", {0}, {0}) == 0, "Failed to add measure gate");

    const std::string json = circuit.parseToRawQCJson();
    expect_contains(json, "\"id\": \"basic_h_measure\"");
    expect_contains(json, "\"shots\": 1024");
    expect_contains(json, "\"num_qubits\": 1");
    expect_contains(json, "\"num_clbits\": 1");
    expect_contains(json, "\"device_name\": \"CPU\"");
    expect_contains(json, "\"target_devices\": []");
    expect_contains(json, "\"is_dynamic\": false");

    expect_ordered_contains(json, {
        "\"name\": \"h\"",
        "\"qubits\": [0]",
        "\"name\": \"measure\"",
        "\"qubits\": [0]",
        "\"clbits\": [0]"
    });
}

void test_bell_chsh_json()
{
    QuantumCircuit circuit("bell_chsh_a0_b0", 2, 2, 2048, "CPU");
    expect_true(circuit.addInstruction("h", {0}, {}) == 0, "Failed to add h gate");
    expect_true(circuit.addInstruction("cx", {0, 1}, {}) == 0, "Failed to add cx gate");
    expect_true(circuit.addInstruction("s", {1}, {}) == 0, "Failed to add s gate");
    expect_true(circuit.addInstruction("h", {1}, {}) == 0, "Failed to add h gate on q1");
    expect_true(circuit.addInstruction("tdg", {1}, {}) == 0, "Failed to add tdg gate");
    expect_true(circuit.addInstruction("h", {1}, {}) == 0, "Failed to add second h gate on q1");
    expect_true(circuit.addInstruction("sdg", {1}, {}) == 0, "Failed to add sdg gate");
    expect_true(circuit.addInstruction("measure", {0}, {0}) == 0, "Failed to add measure q0");
    expect_true(circuit.addInstruction("measure", {1}, {1}) == 0, "Failed to add measure q1");

    const std::string json = circuit.parseToRawQCJson();
    expect_contains(json, "\"id\": \"bell_chsh_a0_b0\"");
    expect_contains(json, "\"shots\": 2048");
    expect_contains(json, "\"num_qubits\": 2");
    expect_contains(json, "\"num_clbits\": 2");
    expect_contains(json, "\"device_name\": \"CPU\"");
    expect_contains(json, "\"target_devices\": []");
    expect_contains(json, "\"is_dynamic\": false");

    expect_ordered_contains(json, {
        "\"name\": \"h\",\n\t\t\t\"qubits\": [0]",
        "\"name\": \"cx\",\n\t\t\t\"qubits\": [0, 1]",
        "\"name\": \"s\",\n\t\t\t\"qubits\": [1]",
        "\"name\": \"h\",\n\t\t\t\"qubits\": [1]",
        "\"name\": \"tdg\",\n\t\t\t\"qubits\": [1]",
        "\"name\": \"h\",\n\t\t\t\"qubits\": [1]",
        "\"name\": \"sdg\",\n\t\t\t\"qubits\": [1]",
        "\"name\": \"measure\",\n\t\t\t\"qubits\": [0],\n\t\t\t\"clbits\": [0]",
        "\"name\": \"measure\",\n\t\t\t\"qubits\": [1],\n\t\t\t\"clbits\": [1]"
    });
}

int main()
{
    try {
        test_basic_h_measure_json();
        test_bell_chsh_json();
        std::cout << "All quantum_circuit JSON tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return 1;
    }
}
