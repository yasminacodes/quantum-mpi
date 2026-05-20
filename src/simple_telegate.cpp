#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

#include "cunqa_api.hpp"
#include "quantum_circuit.hpp"
#include "utils.hpp"

int main()
{
    QuantumCircuit controlCircuit("control_circuit", 1, 0);
    QuantumCircuit targetCircuit("target_circuit", 1, 1);

    controlCircuit.setShots(1024);
    targetCircuit.setShots(1024);

    controlCircuit.addInstruction("h", {0});
    controlCircuit.addInstruction("expose", {0}, {}, {targetCircuit});

    targetCircuit.addInstruction("cx", {0}, {}, {controlCircuit});
    targetCircuit.addInstruction("measure", {0}, {0});

    const char* family_override = std::getenv("QUANTUM_MPI_FAMILY_NAME");
    const std::string family_name =
        (family_override != nullptr && std::string(family_override).size() > 0) ?
            std::string(family_override) :
            build_unique_family_name("simple_telegate_cpp");

    CunqaApi cunqa(
        family_name,
        "00:10:00",
        "Aer",
        true,
        true
    );

    const CunqaExecutionResult execution = cunqa.run({
        controlCircuit,
        targetCircuit
    });

    if (execution.status != 0) {
        std::cerr << execution.error << '\n';
        return execution.status;
    }

    for (const std::string& result : execution.results) {
        std::cout << result << '\n';
    }

    return 0;
}
