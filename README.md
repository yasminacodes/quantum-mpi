# quantum-mpi
This code belongs to Yasmina Gonzalez Almon (hola@yasminacodes) and it is a work in progress for a master's thesis in HPC and Quantum Computing.

This project builds a Quantum MPI library in C++, and can execute programs in QMIO.CESGA using CUNQA.

Layered flow used by the project:
`examples/src -> quantum_mpi (optional) -> quantum_circuit -> cunqa_api -> CUNQA`

Project structure:
* `lib/utils`: shared helper functions (command execution, parsing, string helpers).
* `lib/cunqa_api`: thin adapter to compiled CUNQA (qraise/qdrop, endpoint resolution, payload send/recv).
* `lib/quantum_circuit`: quantum circuit builder and raw JSON export.
* `lib/quantum_mpi`: MPI-like orchestration layer built on top of `quantum_circuit` + `cunqa_api`.
* `src/simple_telegate.cpp`: basic telegate example.
* `src/distributed_qft.cpp`: distributed QFT benchmark example.
* `configure.sh`: helper script to configure+compile with CUNQA in QMIO.
* `run.sh`: helper script to execute a compiled binary with QMIO runtime setup.

## Quick start (QMIO + CUNQA)
This is the main HPC workflow.

### 1) Get code and submodules
```bash
git submodule update --init --recursive
```

### 2) Compile CUNQA in QMIO
CUNQA is an emulator of distributed quantum computing for HPC environments.

Official documentation:
* https://cesga-quantum-spain.github.io/cunqa/
* https://github.com/CESGA-Quantum-Spain/cunqa

QMIO setup example:
```bash
module purge
module load qmio/hpc
module load gcc/12.3.0
module load cmake/3.27.6
module load hpcx-ompi
module load boost/1.85.0
module load eigen/5.0.0
module load nlohmann_json/3.12.0
module load python/3.11.9
module load pybind11/2.13.6-python-3.11.9
module load qiskit/1.2.4-python-3.11.9

cd cunqa
rm -rf build

git config --global --unset-all url."https://github.com/".insteadOf 2>/dev/null || true
git config --global --add url."https://github.com/".insteadOf git@github.com:
git config --global --add url."https://github.com/".insteadOf ssh://git@github.com/

cmake -B build/ -DCMAKE_PREFIX_INSTALL=.
cmake --build build/ --parallel $(nproc)
# Optional GPU build:
cmake -B build/ -DCMAKE_PREFIX_INSTALL=. -DAER_GPU=TRUE

cd ..
```

### 3) Build and run with CUNQA (recommended)
Compile and run tests:
```bash
./configure.sh
```

Run a basic telegate program:
```bash
./run.sh simple_telegate
```

Run distributed QFT benchmark:
```bash
./run.sh distributed_qft --qpus 2 --qubits 4 --shots 1024 --repetitions 3
```

Notes:
* In CUNQA mode, circuit transport is handled by `lib/cunqa_api` (using compiled CUNQA client/ZMQ libs) and results are returned by the simulator backend.
* `run.sh` launches in background by default. Use foreground mode for live output:
```bash
QUANTUM_MPI_DETACH=0 QUANTUM_MPI_VERBOSE=1 ./run.sh simple_telegate
```
* Program stdout/stderr is always written to `run/<execution>/launcher.log`.
* If Slurm keeps jobs in `PENDING`, endpoint resolution can timeout. You can increase wait time:
```bash
QUANTUM_MPI_ENDPOINT_TIMEOUT_SECONDS=600 ./run.sh
```

Run another binary inside `build-qmio/`:
```bash
./run.sh my_program
```

Run by full path:
```bash
./run.sh /path/to/my_binary
```

Pass runtime args to a program:
```bash
./run.sh distributed_qft --qpus 4 --qubits 8 --shots 2048 --repetitions 5
```

### 4) Manual qraise mode in QMIO (optional)
```bash
module purge
module load qmio/hpc
module load gcc/12.3.0
module load hpcx-ompi
module load python/3.11.9
module load imkl/2024.2
module load boost/1.85.0

export PATH="$HOME/bin:$PATH"
export STORE="${STORE:-/mnt/netapp1/Store_CESGA/${HOME#/}}"
mkdir -p "$STORE/.cunqa/logs"
if [ ! -f "$STORE/.cunqa/qpus.json" ]; then
  echo "[]" > "$STORE/.cunqa/qpus.json"
fi

qraise -n 4 -t 01:00:00 --co-located
```

Cleanup:
```bash
qdrop --all
```

## Development and testing without CUNQA (local/non-HPC)
Use this mode for regular development and unit tests.

Warning: this method has not been tested.

### Configure, build and test
```bash
cmake -S . -B build
cmake --build build --parallel $(nproc)
ctest --test-dir build --output-on-failure
```

Generated targets include:
* libraries: `utils`, `cunqa_api`, `quantum_circuit`, `quantum_mpi`
* tests: `quantum_circuit_json_tests`, `quantum_mpi_tests`
* executables: `simple_telegate`, `distributed_qft`

### Run developed code in generic mode
```bash
./build/simple_telegate
```

In generic mode (without CUNQA runtime available), examples may compile but they will not launch real vQPUs. For real distributed execution, build with `-DQUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON` and use the QMIO/CUNQA workflow.

## Compile CUNQA locally (optional)
Possible, but dependencies must be provided manually (compiler, MPI/OpenMP stack, Python, pybind11, Boost, Eigen, nlohmann_json, etc.).

```bash
cd cunqa
export STORE=/path/to/local/store
cmake -B build -DCMAKE_PREFIX_INSTALL=$PWD
cmake --build build --parallel $(nproc)
cmake --install build
```
