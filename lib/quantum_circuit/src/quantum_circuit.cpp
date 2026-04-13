#include "quantum_circuit.hpp"

QuantumCircuit::QuantumCircuit(
    std::string id, int num_qubits, int num_clbits,
    int shots, std::string device_name, std::vector<int> target_devices,
    bool is_dynamic, std::vector<std::string> sending_to, std::string method,
    int seed, bool avoid_parallelization
) {
    this->id = id;
    this->config.shots = shots;
    this->config.num_qubits = num_qubits;
    this->config.num_clbits = num_clbits;
    this->config.device_name = device_name;
    this->config.target_devices = target_devices;
    this->other_config.is_dynamic = is_dynamic;
    this->other_config.sending_to = sending_to;
    this->other_config.method = method;
    this->other_config.seed = seed;
    this->other_config.avoid_parallelization = avoid_parallelization;
}

int QuantumCircuit::addInstruction(std::string name, std::vector<int> qubits_idx, std::vector<int> clbits_idx) {
    if (qubits_idx.size() > static_cast<std::size_t>(this->config.num_qubits) ||
        clbits_idx.size() > static_cast<std::size_t>(this->config.num_clbits)) {
        return 1;
    }

    if (!qubits_idx.empty() &&
        *max_element(qubits_idx.begin(), qubits_idx.end()) > this->config.num_qubits - 1) {
        return 2;
    }

    if (!clbits_idx.empty() &&
        *max_element(clbits_idx.begin(), clbits_idx.end()) > this->config.num_clbits - 1) {
        return 3;
    }

    QuantumInstruction instruction;
    instruction.name = name;
    instruction.qubits_idx = qubits_idx;
    instruction.clbits_idx = clbits_idx;

    this->instructions.push_back(instruction);

    return 0;
}

void QuantumCircuit::clearInstructions() {
    this->instructions.clear();
}

std::string QuantumCircuit::parseToRawQCJson() {
    std::string json = "{\n\t\"id\": \"";
    json.append(this->id);

    json.append("\",\n\t\"instructions\": [\n");
    if (this->instructions.size() > 0) {
        for (std::size_t i = 0; i < this->instructions.size(); i++) {
            if (i > 0) {
                json.append(",\n");
            }

            QuantumInstruction inst = this->instructions[i];

            json.append("\t\t{\n\t\t\t\"name\": \"");
            json.append(inst.name);
            json.append("\"");

            if (inst.qubits_idx.size() > 0) {
                json.append(",\n\t\t\t\"qubits\": [");
                for (std::size_t j = 0; j < inst.qubits_idx.size(); j++) {
                    if (j > 0) {
                        json.append(", ");
                    }
                    json.append(std::to_string(inst.qubits_idx[j]));
                }
                json.append("]");
            }

            if (inst.clbits_idx.size() > 0) {
                json.append(",\n\t\t\t\"clbits\": [");
                for (std::size_t j = 0; j < inst.clbits_idx.size(); j++) {
                    if (j > 0) {
                        json.append(", ");
                    }
                    json.append(std::to_string(inst.clbits_idx[j]));
                }
                json.append("]");
            }

            json.append("\n\t\t}");
        }
    }
    json.append("\n\t],\n\t\"config\": {\n");

    if (this->config.shots > 0) {
        json.append("\t\t\"shots\": ");
        json.append(std::to_string(this->config.shots));
        json.append(",\n");
    }

    json.append("\t\t\"num_qubits\": ");
    json.append(std::to_string(this->config.num_qubits));
    json.append(",\n");

    json.append("\t\t\"num_clbits\": ");
    json.append(std::to_string(this->config.num_clbits));
    json.append(",\n");

    json.append("\t\t\"device\": {\n\t\t\t\"device_name\": \"");
    json.append(this->config.device_name);
    json.append("\",\n\t\t\t\"target_devices\": [");
    for (std::size_t j = 0; j < this->config.target_devices.size(); j++) {
        if (j > 0) {
            json.append(", ");
        }
        json.append(std::to_string(this->config.target_devices[j]));
    }
    json.append("]\n\t\t}\n\t},\n");

    json.append("\t\"is_dynamic\": ");
    json.append(this->other_config.is_dynamic ? "true" : "false");

    if (this->other_config.sending_to.size() > 0) {
        json.append(",\n\t\"sending_to\": [");
        for (std::size_t j = 0; j < this->other_config.sending_to.size(); j++) {
            if (j > 0) {
                json.append(", ");
            }
            json.append("\"");
            json.append(this->other_config.sending_to[j]);
            json.append("\"");
        }
        json.append("]");
    }

    if (this->other_config.method != "") {
        json.append(",\n\t\"method\": \"");
        json.append(this->other_config.method);
        json.append("\"");
    }

    if (this->other_config.seed != -1) {
        json.append(",\n\t\"seed\": ");
        json.append(std::to_string(this->other_config.seed));
    }

    if (this->other_config.avoid_parallelization != false) {
        json.append(",\n\t\"avoid_parallelization\": ");
        json.append(this->other_config.avoid_parallelization ? "true" : "false");
    }

    json.append("\n}");
    return json;
}
