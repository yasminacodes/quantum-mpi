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
    QuantumCircuit circuit("basic_h_measure", 1, 1);
    expect_true(circuit.addInstruction("h", {0}) == 0, "Failed to add h gate");
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
    QuantumCircuit circuit("bell_chsh_a0_b0", 2, 2);
    circuit.setShots(2048);
    
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

void test_classical_communication_json()
{
    QuantumCircuit sender("sender_circuit", 1, 1);
    QuantumCircuit receiver("receiver_circuit", 1, 1);

    expect_true(sender.addInstruction("h", {0}) == 0, "Failed to add h gate");
    expect_true(sender.addInstruction("measure", {0}, {0}) == 0, "Failed to add measure gate");
    expect_true(sender.addInstruction("send", {}, {0}, {receiver}) == 0, "Failed to add classical send");

    expect_true(receiver.addInstruction("recv", {}, {0}, {sender}) == 0, "Failed to add classical recv");

    const std::string sender_json = sender.parseToRawQCJson();
    const std::string receiver_json = receiver.parseToRawQCJson();

    expect_contains(sender_json, "\"id\": \"sender_circuit\"");
    expect_contains(sender_json, "\"num_qubits\": 1");
    expect_contains(sender_json, "\"num_clbits\": 1");
    expect_contains(sender_json, "\"is_dynamic\": true");
    expect_contains(sender_json, "\"sending_to\": [\"receiver_circuit\"]");

    expect_ordered_contains(sender_json, {
        "\"name\": \"h\"",
        "\"qubits\": [0]",
        "\"name\": \"measure\"",
        "\"qubits\": [0]",
        "\"clbits\": [0]",
        "\"name\": \"send\"",
        "\"clbits\": [0]",
        "\"circuits\": [\"receiver_circuit\"]"
    });

    expect_contains(receiver_json, "\"id\": \"receiver_circuit\"");
    expect_contains(receiver_json, "\"num_qubits\": 1");
    expect_contains(receiver_json, "\"num_clbits\": 1");
    expect_contains(receiver_json, "\"is_dynamic\": true");

    expect_ordered_contains(receiver_json, {
        "\"name\": \"recv\"",
        "\"clbits\": [0]",
        "\"circuits\": [\"sender_circuit\"]"
    });
}

void test_quantum_communication_json()
{
    QuantumCircuit qsender("qsender_circuit", 1, 1);
    QuantumCircuit qreceiver("qreceiver_circuit", 1, 1);

    expect_true(qsender.addInstruction("h", {0}) == 0, "Failed to add h gate");
    expect_true(qsender.addInstruction("qsend", {0}, {}, {qreceiver}) == 0, "Failed to add quantum qsend");
    expect_true(qsender.addInstruction("measure", {0}, {0}) == 0, "Failed to add sender measure");

    expect_true(qreceiver.addInstruction("qrecv", {0}, {}, {qsender}) == 0, "Failed to add quantum qrecv");
    expect_true(qreceiver.addInstruction("measure", {0}, {0}) == 0, "Failed to add receiver measure");

    const std::string qsender_json = qsender.parseToRawQCJson();
    const std::string qreceiver_json = qreceiver.parseToRawQCJson();

    expect_contains(qsender_json, "\"id\": \"qsender_circuit\"");
    expect_contains(qsender_json, "\"num_qubits\": 1");
    expect_contains(qsender_json, "\"num_clbits\": 1");
    expect_contains(qsender_json, "\"is_dynamic\": true");

    expect_ordered_contains(qsender_json, {
        "\"name\": \"h\"",
        "\"qubits\": [0]",
        "\"name\": \"qsend\"",
        "\"qubits\": [0]",
        "\"circuits\": [\"qreceiver_circuit\"]",
        "\"name\": \"measure\"",
        "\"qubits\": [0]",
        "\"clbits\": [0]"
    });

    expect_contains(qreceiver_json, "\"id\": \"qreceiver_circuit\"");
    expect_contains(qreceiver_json, "\"num_qubits\": 1");
    expect_contains(qreceiver_json, "\"num_clbits\": 1");
    expect_contains(qreceiver_json, "\"is_dynamic\": true");

    expect_ordered_contains(qreceiver_json, {
        "\"name\": \"qrecv\"",
        "\"qubits\": [0]",
        "\"circuits\": [\"qsender_circuit\"]",
        "\"name\": \"measure\"",
        "\"qubits\": [0]",
        "\"clbits\": [0]"
    });
}

void test_telegate_json()
{
    QuantumCircuit control_circuit("control_circuit", 1, 0);
    QuantumCircuit target_circuit("target_circuit", 1, 0);

    expect_true(
        control_circuit.addInstruction("h", {0}, {}, {}) == 0,
        "Failed to add h gate on control circuit"
    );

    expect_true(
        control_circuit.addInstruction("expose", {0}, {}, {target_circuit}) == 0,
        "Failed to add expose instruction"
    );

    expect_true(
        target_circuit.addInstruction("crz", {0}, {}, {control_circuit}, {1.5707963267948966}) == 0,
        "Failed to add remote controlled crz instruction"
    );

    const std::string control_json = control_circuit.parseToRawQCJson();
    const std::string target_json = target_circuit.parseToRawQCJson();

    std::cout << control_json << '\n';
    std::cout << target_json << '\n';

    expect_contains(control_json, "\"id\": \"control_circuit\"");
    expect_contains(control_json, "\"num_qubits\": 1");
    expect_contains(control_json, "\"num_clbits\": 0");
    expect_contains(control_json, "\"is_dynamic\": true");
    expect_contains(control_json, "\"sending_to\": [\"target_circuit\"]");

    expect_ordered_contains(control_json, {
        "\"name\": \"h\"",
        "\"qubits\": [0]",
        "\"name\": \"expose\"",
        "\"qubits\": [0]",
        "\"circuits\": [\"target_circuit\"]"
    });

    expect_contains(target_json, "\"id\": \"target_circuit\"");
    expect_contains(target_json, "\"num_qubits\": 1");
    expect_contains(target_json, "\"num_clbits\": 0");
    expect_contains(target_json, "\"is_dynamic\": true");

    expect_ordered_contains(target_json, {
        "\"name\": \"crz\"",
        "\"qubits\": [0]",
        "\"circuits\": [\"control_circuit\"]",
        "\"params\": [1.570796]"
    });
}

int main()
{
    try {
        test_basic_h_measure_json();
        test_bell_chsh_json();
        test_classical_communication_json();
        test_quantum_communication_json();
        test_telegate_json();
        std::cout << "All quantum_circuit JSON tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return 1;
    }
}
