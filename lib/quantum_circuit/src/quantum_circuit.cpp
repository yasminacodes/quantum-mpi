#include "quantum_circuit.hpp"

QuantumCircuit::QuantumCircuit(
    std::string id, 
    int num_qubits, 
    int num_clbits
) {
    this->id = id;
    this->num_qubits = num_qubits;
    this->num_clbits = num_clbits;
}

int QuantumCircuit::addInstruction(
    std::string name, 
    std::vector<int> qubits_idx, 
    std::vector<int> clbits_idx, 
    std::vector<QuantumCircuit> circuits,
    std::vector<double> params) 
{
    if (qubits_idx.size() > static_cast<std::size_t>(this->num_qubits) ||
        clbits_idx.size() > static_cast<std::size_t>(this->num_clbits)) {
        return 1;
    }

    if (!qubits_idx.empty() &&
        *max_element(qubits_idx.begin(), qubits_idx.end()) > this->num_qubits - 1) {
        return 2;
    }

    if (!clbits_idx.empty() &&
        *max_element(clbits_idx.begin(), clbits_idx.end()) > this->num_clbits - 1) {
        return 3;
    }

    std::vector<std::string> circuit_ids;
    if(!circuits.empty()) {
        for(QuantumCircuit& circuit : circuits) {
            std::string circuit_id = circuit.getId();
            if (circuit_id == "") {
                return 4;
            } else {
                if(name == "send" || name == "qsend" || name == "expose") {
                    this->sending_to.push_back(circuit_id);
                }
                circuit_ids.push_back(circuit_id);
            }
        }
    }

    QuantumInstruction instruction;
    instruction.name = name;
    instruction.qubits_idx = qubits_idx;
    instruction.clbits_idx = clbits_idx;
    instruction.circuits = circuit_ids;
    instruction.params = params;

    this->instructions.push_back(instruction);

    return 0;
}

std::string QuantumCircuit::parseToRawQCJson() {
    this->checkCommAndSetDynamic();

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

            if (inst.circuits.size() > 0) {
                json.append(",\n\t\t\t\"circuits\": [");
                for (std::size_t j = 0; j < inst.circuits.size(); j++) {
                    if (j > 0) {
                        json.append(", ");
                    }
                    json.append("\"");
                    json.append(inst.circuits[j]);
                    json.append("\"");
                }
                json.append("]");
            }

            if (inst.params.size() > 0) {
                json.append(",\n\t\t\t\"params\": [");
                for (std::size_t j = 0; j < inst.params.size(); j++) {
                    if (j > 0) {
                        json.append(", ");
                    }
                    json.append(std::to_string(inst.params[j]));
                }
                json.append("]");
            }

            json.append("\n\t\t}");
        }
    }
    json.append("\n\t],\n\t\"config\": {\n");

    if (this->shots > 0) {
        json.append("\t\t\"shots\": ");
        json.append(std::to_string(this->shots));
        json.append(",\n");
    }

    json.append("\t\t\"num_qubits\": ");
    json.append(std::to_string(this->num_qubits));
    json.append(",\n");

    json.append("\t\t\"num_clbits\": ");
    json.append(std::to_string(this->num_clbits));
    json.append(",\n");

    json.append("\t\t\"device\": {\n\t\t\t\"device_name\": \"");
    json.append(this->device_name);
    json.append("\",\n\t\t\t\"target_devices\": [");
    for (std::size_t j = 0; j < this->target_devices.size(); j++) {
        if (j > 0) {
            json.append(", ");
        }
        json.append(std::to_string(this->target_devices[j]));
    }
    json.append("]\n\t\t}\n\t},\n");

    json.append("\t\"is_dynamic\": ");
    json.append(this->is_dynamic ? "true" : "false");

    if (this->sending_to.size() > 0) {
        json.append(",\n\t\"sending_to\": [");
        for (std::size_t j = 0; j < this->sending_to.size(); j++) {
            if (j > 0) {
                json.append(", ");
            }
            json.append("\"");
            json.append(this->sending_to[j]);
            json.append("\"");
        }
        json.append("]");
    }

    if (this->method != "") {
        json.append(",\n\t\"method\": \"");
        json.append(this->method);
        json.append("\"");
    }

    if (this->seed != -1) {
        json.append(",\n\t\"seed\": ");
        json.append(std::to_string(this->seed));
    }

    if (this->avoid_parallelization != false) {
        json.append(",\n\t\"avoid_parallelization\": ");
        json.append(this->avoid_parallelization ? "true" : "false");
    }

    json.append("\n}");
    return json;
}


std::string QuantumCircuit::getId() {
    return this->id;
}

void QuantumCircuit::setShots(int shots) {
    this->shots = shots;
}

int QuantumCircuit::getShots() {
    return this->shots;
}

void QuantumCircuit::setDynamic(bool dynamic) {
    this->is_dynamic = dynamic;
}

bool QuantumCircuit::getDynamic() {
    return this->is_dynamic;
}

void QuantumCircuit::checkCommAndSetDynamic() {
    for (const QuantumInstruction& inst : this->instructions) {
        if (!inst.circuits.empty() ||
            inst.name == "send" ||
            inst.name == "recv" ||
            inst.name == "qsend" ||
            inst.name == "qrecv" ||
            inst.name == "expose" ||
            inst.name == "rcontrol" ||
            inst.name == "crz") {
            this->is_dynamic = true;
            return;
        }
    }
}
