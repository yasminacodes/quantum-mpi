# quantum-mpi
This code belongs to Yasmina Gonzalez Almon (hola@yasminacodes) and it's a work in progress for a master's thesis in HPC and Quantum Computing.

This project builds a generic Quantum MPI library in C++, with a CUNQA runtime module to execute and test it in QMIO/CESGA.

Project structure:
* `lib/quantum_circuit`: build quantum circuits and export raw JSON.
* `lib/quantum_mpi`: generic orchestration layer (launcher, endpoint resolver, transport).
* `modules/cunqa_runtime`: CUNQA-specific module (`include/`, `src/`, `CMakeLists.txt`).
* `src/main.cpp`: example executable.

## Quick start (QMIO + CUNQA)
This is the main workflow in HPC.

### 1) Get the code and submodules
```bash
git submodule update --init --recursive
```

### 2) Compile CUNQA in QMIO
CUNQA is a emulator of distributed quantum computing for HPC environments, and it's the tool this project will use to implement and test a estandar library for quantum MPI.

The documentation for CUNQA can be found (here)[https://cesga-quantum-spain.github.io/cunqa/].

When the files are available, QUNCA can be compilled following the official instructions available (here)[https://github.com/CESGA-Quantum-Spain/cunqa#].

If you are executing this on the CESGA quantum supercomputer QMIO, you can copy and paste the following commands:
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
cmake -B build/ -DCMAKE_PREFIX_INSTALL=. -DAER_GPU=TRUE

cd ..
```

### 3) Compile quantum-mpi with CUNQA module enabled
```bash
cmake -S . -B build-qmio \
  -DQUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON \
  -DQUANTUM_MPI_CUNQA_SOURCE_DIR=$PWD/cunqa \
  -DQUANTUM_MPI_ZMQ_INCLUDE_DIR=$HOME/include \
  -DQUANTUM_MPI_ZMQ_LIBRARY=$HOME/lib/libzmq.so \
  -DQUANTUM_MPI_SPDLOG_INCLUDE_DIR=$PWD/cunqa/build/_deps/spdlog-src/include

cmake --build build-qmio --parallel $(nproc)
```

### 4) Run in QMIO (execution uses qraise)
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
  echo "{}" > "$STORE/.cunqa/qpus.json"
fi

mkdir -p run
cd run
../build-qmio/quantum_mpi_main
```

Important:
* `build-qmio` is only the CMake build directory.
* `quantum_mpi_main` launches `qraise` internally.
* Default command is `qraise -n 4 -t 01:00:00 --co-located`.
* You can override it with environment variable:
  ```bash
  export QUANTUM_MPI_QRAISE_COMMAND="qraise -n 8 -t 00:20:00 --co-located"
  ../build-qmio/quantum_mpi_main
  ```
* You can also override it by passing the command as executable arguments:
  ```bash
  ../build-qmio/quantum_mpi_main qraise -n 8 -t 00:20:00 --co-located
  ```

### 5) Execute qraise directly in QMIO (manual mode)
If you want to launch vQPUs manually first:
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
  echo "{}" > "$STORE/.cunqa/qpus.json"
fi

qraise -n 4 -t 01:00:00 --co-located
```

To clean resources after tests:
```bash
qdrop --all
```

## Alternative: compile quantum-mpi without CUNQA
Use this for generic development/testing of the libraries.

```bash
cmake -S . -B build
cmake --build build --parallel $(nproc)
ctest --test-dir build --output-on-failure
```

This builds:
* `quantum_circuit`
* `quantum_mpi`
* unit tests (`quantum_circuit_json_tests`, `quantum_mpi_tests`)
* `quantum_mpi_main` in generic mode (no CUNQA runtime)

## Regular PC (non-HPC)
This section is for a normal workstation.

### 1) Run quantum-mpi in generic mode
```bash
cmake -S . -B build
cmake --build build --parallel $(nproc)
ctest --test-dir build --output-on-failure
./build/quantum_mpi_main
```

In generic mode, `quantum_mpi_main` exits with a message indicating CUNQA integration is disabled.

### 2) Compile CUNQA locally (optional)
Possible, but you must provide dependencies manually (compiler, MPI/OpenMP stack, Python, pybind11, Boost, Eigen, nlohmann_json, etc.). This method is has not been tested yet.

```bash
cd cunqa
export STORE=/path/to/local/store
cmake -B build -DCMAKE_PREFIX_INSTALL=$PWD
cmake --build build --parallel $(nproc)
cmake --install build
```

After that, you can try:
```bash
cmake -S . -B build-cunqa \
  -DQUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON \
  -DQUANTUM_MPI_CUNQA_SOURCE_DIR=$PWD/cunqa
```

But distributed execution still requires a runtime equivalent to the HPC setup.
