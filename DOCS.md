# Documentación del Proyecto quantum-mpi

## Índice

1. [Visión General](#visión-general)
2. [Arquitectura del Proyecto](#arquitectura-del-proyecto)
3. [Componentes Principales](#componentes-principales)
4. [Patrones de Diseño](#patrones-de-diseño)
5. [Flujo de Ejecución](#flujo-de-ejecución)
6. [Simuladores Disponibles](#simuladores-disponibles)
7. [Comunicación entre QPUs](#comunicación-entre-qpus)
8. [CLI - Herramientas de Gestión](#cli---herramientas-de-gestión)
9. [Estructura de Directorios](#estructura-de-directorios)

---

## Visión General

Este proyecto implementa una biblioteca genérica de **Quantum MPI** en C++, diseñada para ejecutar programas cuánticos distribuidos utilizando **CUNQA** (Distributed Quantum Computing emulator for HPC) en el supercomputador cuántico **QMIO** de CESGA.

El proyecto permite:
- Construir circuitos cuánticos y exportarlos a JSON
- Orquestar ejecuciones distribuidas en múltiples QPUs virtuales (vQPUs)
- Simular comunicaciones clásicas y cuánticas entre QPUs
- Soportar múltiples backends de simulación (Qulacs, Qiskit Aer, MQT-DDSIM, etc.)

---

## Arquitectura del Proyecto

### Diagrama de Componentes de Alto Nivel

```mermaid
graph TB
    subgraph "Capa de Aplicación"
        CLI[CLI Tools<br/>qraise, qdrop, qinfo]
        MAIN[quantum_mpi_main]
    end

    subgraph "Capa de Bibliotecas"
        QC[lib/quantum_circuit<br/>Constructor de Circuitos]
        MPI[lib/quantum_mpi<br/>Orquestación MPI]
    end

    subgraph "CUNQA Framework"
        QPU[CUNQA QPU<br/>Backend + Server]
        CC[Classical Channel<br/>MPI / ZMQ]
        SIM[Simulator Engines]
    end

    subgraph "Backends de Simulación"
        AER[AER Simulator]
        QULACS[Qulacs]
        QSIM[QSim]
        MAESTRO[Maestro]
        MUNICH[Munich]
        CUNQA[CUNQA Native]
    end

    subgraph "Infraestructura HPC"
        QMIO[QMIO Supercomputer]
        SLURM[Slurm Scheduler]
    end

    CLI --> QC
    MAIN --> QC
    MAIN --> MPI
    MPI --> QPU
    CLI --> QPU
    QPU --> CC
    QPU --> SIM
    SIM --> AER
    SIM --> QULACS
    SIM --> QSIM
    SIM --> MAESTRO
    SIM --> MUNICH
    SIM --> CUNQA
    QPU --> SLURM
    SLURM --> QMIO
```

---

## Componentes Principales

### 1. Bibliotecas Base

#### lib/quantum_circuit

Biblioteca para construir circuitos cuánticos y exportarlos a JSON.

```mermaid
classDiagram
    class QuantumCircuit {
        -string id
        -vector~QuantumInstruction~ instructions
        -QuantumCircuitConfig config
        -QuantumCircuitOtherConfig other_config
        +QuantumCircuit()
        +addInstruction(name, qubits, clbits) int
        +clearInstructions()
        +parseToRawQCJson() string
    }

    class QuantumCircuitConfig {
        +int shots
        +int num_qubits
        +int num_clbits
        +string device_name
        +vector~int~ target_devices
    }

    class QuantumInstruction {
        +string name
        +vector~int~ qubits_idx
        +vector~int~ clbits_idx
    }

    QuantumCircuit *-- QuantumCircuitConfig
    QuantumCircuit *-- QuantumInstruction
```

#### lib/quantum_mpi

Capa de orquestación que abstrae la ejecución MPI cuántica.

```mermaid
classDiagram
    class QuantumMPI {
        -shared_ptr~IJobLauncher~ launcher_
        -shared_ptr~IEndpointResolver~ resolver_
        -shared_ptr~IQuantumMPITransport~ transport_
        +launchJob() string
        +waitEndpointForJob(job_id, timeout, poll) string
        +runPayload(endpoint, payload) string
    }

    class IJobLauncher {
        <<interface>>
        +launch_job() string
    }

    class IEndpointResolver {
        <<interface>>
        +wait_endpoint_for_job(job_id, timeout, poll) string
    }

    class IQuantumMPITransport {
        <<interface>>
        +send(endpoint, payload) string
    }

    class CommandJobLauncher {
        -string launch_command_
        -JobIdParser parser_
        +launch_job() string
    }

    class PollingEndpointResolver {
        -EndpointLookup endpoint_lookup_
        -JobStateLookup job_state_lookup_
        +wait_endpoint_for_job(job_id, timeout, poll) string
    }

    class FunctionTransport {
        -Sender sender_
        +send(endpoint, payload) string
    }

    QuantumMPI *-- IJobLauncher
    QuantumMPI *-- IEndpointResolver
    QuantumMPI *-- IQuantumMPITransport
    CommandJobLauncher ..|> IJobLauncher
    PollingEndpointResolver ..|> IEndpointResolver
    FunctionTransport ..|> IQuantumMPITransport
```

### 2. CUNQA Framework

#### QPU (Quantum Processing Unit)

```mermaid
classDiagram
    class QPU {
        -unique_ptr~Backend~ backend
        -unique_ptr~Server~ server
        -queue~string~ message_queue_
        -condition_variable queue_condition_
        -mutex queue_mutex_
        -string family_
        -string name_
        -string comm_
        +QPU(backend, mode, name, family, comm)
        +turn_ON()
        -compute_result_()
        -recv_data_()
    }

    class Backend {
        <<interface>>
        +JSON config
        +execute(quantum_task) JSON
        +to_json() JSON
    }

    class QCBackend {
        -unique_ptr~SimulatorStrategy~ simulator_
        +execute(quantum_task) JSON
    }

    class CCBackend {
        -unique_ptr~SimulatorStrategy~ simulator_
        +execute(quantum_task) JSON
    }

    class SimpleBackend {
        -unique_ptr~SimulatorStrategy~ simulator_
        +execute(quantum_task) JSON
    }

    QPU *-- Backend
    QPU *-- Server
    Backend <|-- QCBackend
    Backend <|-- CCBackend
    Backend <|-- SimpleBackend
```

#### Backends de Simulación (Strategy Pattern)

```mermaid
classDiagram
    class SimulatorStrategy~T~ {
        <<template>>
        <<interface>>
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class QulacsSimpleSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class QulacsCCSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class QulacsQCSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class AERSimpleSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class QsimSimpleSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class MaestroSimpleSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class MunichSimpleSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    class CunqaSimpleSimulator {
        +get_name() string
        +execute(backend, circuit) JSON
    }

    SimulatorStrategy <|.. QulacsSimpleSimulator
    SimulatorStrategy <|.. AERSimpleSimulator
    SimulatorStrategy <|.. QsimSimpleSimulator
    SimulatorStrategy <|.. MaestroSimpleSimulator
    SimulatorStrategy <|.. MunichSimpleSimulator
    SimulatorStrategy <|.. CunqaSimpleSimulator
```

---

## Patrones de Diseño

### 1. Strategy Pattern (Simuladores)

Los simuladores implementan el patrón Strategy para permitir diferentes motores de simulación:

```mermaid
classDiagram
    class Backend {
        <<context>>
        -simulator_ : unique_ptr~SimulatorStrategy~
        +execute(task) JSON
    }

    class SimulatorStrategy~T~ {
        <<strategy interface>>
        +execute(backend, task) JSON
    }

    class AERAdapter {
        +execute(backend, task) JSON
    }

    class QulacsAdapter {
        +execute(backend, task) JSON
    }

    Backend o-- SimulatorStrategy
    SimulatorStrategy <|.. AERAdapter
    SimulatorStrategy <|.. QulacsAdapter
```

### 2. Factory Pattern (Configuraciones)

```mermaid
classDiagram
    class SimpleConfig {
        +string name
        +int n_qubits
        +vector~string~ basis_gates
        +JSON noise_model
    }

    class CCConfig {
        +string name
        +int n_qubits
        +vector~string~ basis_gates
    }

    class QCConfig {
        +string name
        +int n_qubits
        +vector~vector~int~~ coupling_map
        +vector~string~ basis_gates
    }
```

### 3. Pimpl (Pointer to Implementation)

Para ocultar la implementación y reducir dependencias:

```mermaid
classDiagram
    class ClassicalChannel {
        -unique_ptr~Impl~ pimpl_
        +publish()
        +connect(target)
        +send_info(data, target)
        +recv_info(origin)
    }

    class Client {
        -unique_ptr~Impl~ pimpl_
        +connect(endpoint)
        +send_circuit(circuit)
        +recv_results()
    }

    class Server {
        -unique_ptr~Impl~ pimpl_
        +accept()
        +recv_data()
        +send_result(result)
    }
```

---

## Flujo de Ejecución

### Ejecución en QMIO

```mermaid
sequenceDiagram
    participant User
    participant Launcher as launch_cunqa_program.sh
    participant QRAISE as qraise CLI
    participant SLURM as Slurm
    participant QMIO as QMIO Cluster
    participant QPU as CUNQA QPU
    participant MAIN as quantum_mpi_main

    User->>Launcher: bash launch_cunqa_program.sh --build
    Launcher->>Launcher: cmake + make (build-qmio/)
    Launcher->>QRAISE: qraise -n 4 -t 01:00:00
    QRAISE->>QRAISE: Genera sbatch script
    QRAISE->>SLURM: sbatch qraise_sbatch_tmp.sbatch
    SLURM->>QMIO: Allocates nodes
    QMIO->>QPU: Starts QPU processes
    QPU->>QPU: Backend initializes
    QPU->>QPU: Server starts listening

    loop Until job endpoint available
        MAIN->>SLURM: squeue -j <job_id>
    end

    MAIN->>QPU: Client sends circuit JSON
    QPU->>QPU: Simulator executes
    QPU-->>MAIN: Returns result
```

### Flujo de Tareas Cuánticas

```mermaid
flowchart LR
    subgraph Input
        QT[QuantumTask]
    end

    subgraph Processing
        QT -->|Parse| JSON
        JSON -->|Execute| SIM[Simulator]
        SIM -->|Result| RES[JSON Result]
    end

    subgraph Types
        SIM -->|QC| QCS[Quantum Communication]
        SIM -->|CC| CCS[Classical Communication]
        SIM -->|Simple| SS[Simple Execution]
    end

    Input --> Processing
    RES --> Output
```

---

## Simuladores Disponibles

| Simulador | Descripción | Soporte QC | Soporte CC |
|-----------|-------------|-------------|------------|
| **Aer** | Qiskit Aer (con GPU) | Sí | Sí |
| **Qulacs** | Simulador de alto rendimiento | Sí | Sí |
| **QSim** | MQT-DDSIM | Sí | Sí |
| **Maestro** | Simulador especializado | Sí | Sí |
| **Munich** | Simulador Múnich | Sí | Sí |
| **CUNQA** | Nativo de CUNQA | Sí | Sí |

### Arquitectura de Adaptadores

```mermaid
graph TB
    subgraph "Adapters"
        AER_A[AER Adapter]
        QULACS_A[Qulacs Adapter]
        QSIM_A[QSim Adapter]
        MAESTRO_A[Maestro Adapter]
        MUNICH_A[Munich Adapter]
        CUNQA_A[CUNQA Adapter]
    end

    subgraph "Simulators"
        AER[Qiskit Aer]
        QULACS[Qulacs Lib]
        QSIM[MQT-DDSIM]
        MAESTRO[Maestro Lib]
        MUNICH[Munich Lib]
        CUNQA[CUNQA Core]
    end

    AER_A --> AER
    QULACS_A --> QULACS
    QSIM_A --> QSIM
    MAESTRO_A --> MAESTRO
    MUNICH_A --> MUNICH
    CUNQA_A --> CUNQA
```

---

## Comunicación entre QPUs

### Canales de Comunicación

```mermaid
classDiagram
    class ClassicalChannel {
        -unique_ptr~Impl~ pimpl_
        -JSON communications
        -string qpu_id
        +publish()
        +connect(qpu_id)
        +send_info(data, target)
        +recv_info(origin)
        +send_measure(measurement, target)
        +recv_measure(origin)
    }

    class MPIChannel {
        +send(data)
        +recv()
        +broadcast()
    }

    class ZMQChannel {
        +send(data)
        +recv()
        +publish()
    }

    ClassicalChannel <|-- MPIChannel
    ClassicalChannel <|-- ZMQChannel
```

### Topologías de Comunicación

```mermaid
graph LR
    subgraph "Classical Communication (CC)"
        QPU1[QPU 1]
        QPU2[QPU 2]
        QPU3[QPU 3]
        MPI[(MPI Bus)]
        QPU1 <--> MPI
        QPU2 <--> MPI
        QPU3 <--> MPI
    end

    subgraph "Quantum Communication (QC)"
        QPU1a[QPU 1]
        QPU2a[QPU 2]
        QPU3a[QPU 3]
        LINK1[Quantum Link]
        LINK2[Quantum Link]
        QPU1a --->|Teleport| LINK1
        LINK1 --->|Teleport| QPU2a
        QPU2a --->|Teleport| LINK2
        LINK2 --->|Teleport| QPU3a
    end
```

---

## CLI - Herramientas de Gestión

### Comandos Disponibles

| Comando | Descripción |
|---------|-------------|
| `qraise` | Despliega vQPUs en el cluster |
| `qdrop` | Libera recursos de vQPUs |
| `qinfo` | Muestra información de QPUs |
| `erase_key` | Elimina claves de configuración |

### Diagrama de Opciones de qraise

```mermaid
flowchart TB
    QRAISE[qraise] --> OPTS

    OPTS --> NUM[-n num_qpus]
    OPTS --> TIME[-t time]
    OPTS --> SIM[--simulator]
    OPTS --> CC[--classical_comm]
    OPTS --> QC[--quantum_comm]
    OPTS --> QMIO[--qmio]
    OPTS --> CO[--co-located]

    SIM --> SIM_A[AER / Qulacs / Qsim / ...]
    CC --> CC_B[Simple Backend]
    QC --> QC_B[QC Backend]

    subgraph Backends
        SIM_A
        CC_B
        QC_B
    end
```

---

## Estructura de Directorios

```
quantum-mpi/
├── CMakeLists.txt           # Build principal
├── README.md
├── launch_cunqa_program.sh  # Script de lanzamiento
│
├── lib/
│   ├── quantum_circuit/     # Biblioteca de circuitos
│   │   ├── include/
│   │   │   └── quantum_circuit.hpp
│   │   ├── src/
│   │   │   └── quantum_circuit.cpp
│   │   └── tests/
│   │
│   └── quantum_mpi/          # Biblioteca MPI cuántica
│       ├── include/
│       │   └── quantum_mpi.hpp
│       ├── src/
│       │   ├── quantum_mpi.cpp
│       │   └── quantum_mpi_comm.cpp
│       └── tests/
│
├── src/
│   └── main.cpp              # Ejecutable principal
│
└── cunqa/                    # Framework CUNQA (submódulo)
    ├── CMakeLists.txt
    ├── README.md
    │
    ├── cunqa/                # Bindings Python
    │   ├── bindings.cpp
    │   ├── circuit/
    │   ├── qiskit_deps/
    │   ├── real_qpus/
    │   └── utils/
    │
    └── src/
        ├── backends/         # Backends de simulación
        │   ├── backend.hpp
        │   ├── qc_backend.hpp
        │   ├── cc_backend.hpp
        │   ├── simple_backend.hpp
        │   └── simulators/
        │       ├── simulator_strategy.hpp
        │       ├── AER/
        │       ├── Qulacs/
        │       ├── Qsim/
        │       ├── Maestro/
        │       ├── Munich/
        │       └── CUNQA/
        │
        ├── cli/              # Herramientas CLI
        │   ├── CMakeLists.txt
        │   ├── qraise.cpp
        │   ├── qdrop.cpp
        │   ├── qinfo.cpp
        │   ├── erase_key.cpp
        │   └── qraise/
        │       ├── args_qraise.hpp
        │       ├── simple_conf_qraise.hpp
        │       ├── cc_conf_qraise.hpp
        │       ├── qc_conf_qraise.hpp
        │       ├── noise_model_conf_qraise.hpp
        │       ├── qmio_conf_qraise.hpp
        │       ├── infrastructure_conf_qraise.hpp
        │       └── utils_qraise.hpp
        │
        ├── comm/             # Comunicación
        │   ├── client.hpp
        │   ├── server.hpp
        │   └── comm_impl/
        │       ├── asio/
        │       ├── crow/
        │       └── zmq/
        │
        ├── classical_channel/  # Canal clásico
        │   ├── classical_channel.hpp
        │   └── classical_channel_impl/
        │       ├── mpi/
        │       └── zmq/
        │
        ├── utils/            # Utilidades
        │   ├── json.hpp
        │   ├── logger/
        │   ├── probabilities/
        │   └── helpers/
        │
        ├── quantum_task.hpp
        └── qpu.hpp
```

### Diagrama de Dependencias de Módulos

```mermaid
graph TB
    subgraph "quantum_circuit"
        QC_H[quantum_circuit.hpp]
        QC_C[quantum_circuit.cpp]
    end

    subgraph "quantum_mpi"
        MPI_H[quantum_mpi.hpp]
        MPI_C[quantum_mpi.cpp]
        MPI_COMM[quantum_mpi_comm.cpp]
    end

    subgraph "cunqa/src"
        QPU[QPU]
        BACKENDS[Backends]
        CLI[CLI]
        COMM[Comm]
        CHANNEL[Classical Channel]
    end

    QC_C --> QC_H
    MPI_C --> QC_H
    MPI_C --> MPI_H
    MPI_COMM --> MPI_H
    BACKENDS --> QC_H
    QPU --> BACKENDS
    QPU --> COMM
    CHANNEL --> COMM
    CLI --> QPU
    CLI --> BACKENDS
```

---

## Configuración y Compilación

### Compilación Local (Desarrollo)

```bash
cmake -S . -B build
cmake --build build --parallel $(nproc)
ctest --test-dir build --output-on-failure
```

### Compilación con CUNQA (QMIO)

```bash
cd cunqa
cmake -B build/ -DCMAKE_PREFIX_INSTALL=.
cmake --build build/ --parallel $(nproc)
```

### Compilación con GPU

```bash
cmake -B build/ -DCMAKE_PREFIX_INSTALL=. -DAER_GPU=TRUE
```

---

## Ejemplo de Uso

```python
# Desplegar vQPUs
from cunqa.qpu import qraise, get_QPUs

family = qraise(2, "00:10:00", simulator="Aer", co_located=True)
qpus = get_QPUs(co_located=True)

# Crear y ejecutar circuito
from cunqa.circuit import CunqaCircuit

qc = CunqaCircuit(num_qubits=2)
qc.h(0)
qc.cx(0, 1)
qc.measure_all()

# Ejecutar
from cunqa.qpu import run
from cunqa.qjob import gather

qcs = [qc] * 2
qjobs = run(qcs, qpus, shots=1000)
results = gather(qjobs)

# Liberar recursos
from cunqa.qpu import qdrop
qdrop(family)
```
