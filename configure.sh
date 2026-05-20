#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
BUILD_DIR="${QUANTUM_MPI_BUILD_DIR:-${PROJECT_DIR}/build-qmio}"
CUNQA_ROOT_DIR="${QUANTUM_MPI_CUNQA_ROOT_DIR:-${QUANTUM_MPI_CUNQA_SOURCE_DIR:-${PROJECT_DIR}/cunqa}}"
CUNQA_BUILD_DIR="${QUANTUM_MPI_CUNQA_BUILD_DIR:-${CUNQA_ROOT_DIR}/build}"
CUNQA_INCLUDE_DIR="${QUANTUM_MPI_CUNQA_INCLUDE_DIR:-${CUNQA_ROOT_DIR}/src}"

if command -v module >/dev/null 2>&1; then
    module --force purge >/dev/null 2>&1 || true
    module load qmio/hpc >/dev/null 2>&1 || true
    module load gcc/12.3.0 >/dev/null 2>&1 || true
    module load cmake/3.27.6 >/dev/null 2>&1 || true
    module load hpcx-ompi >/dev/null 2>&1 || true
    module load boost/1.85.0 >/dev/null 2>&1 || true
    module load eigen/5.0.0 >/dev/null 2>&1 || true
    module load nlohmann_json/3.12.0 >/dev/null 2>&1 || true
    module load python/3.11.9 >/dev/null 2>&1 || true
    module load pybind11/2.13.6-python-3.11.9 >/dev/null 2>&1 || true
    module load qiskit/1.2.4-python-3.11.9 >/dev/null 2>&1 || true
fi

echo "[INFO] Project dir: ${PROJECT_DIR}"
echo "[INFO] Build dir: ${BUILD_DIR}"
echo "[INFO] CUNQA root dir: ${CUNQA_ROOT_DIR}"
echo "[INFO] CUNQA build dir: ${CUNQA_BUILD_DIR}"
echo "[INFO] CUNQA include dir: ${CUNQA_INCLUDE_DIR}"
echo "[INFO] BUILD_TESTING: ON"

if [[ ! -d "${CUNQA_BUILD_DIR}" ]]; then
    echo "[ERROR] Compiled CUNQA build dir not found: ${CUNQA_BUILD_DIR}"
    echo "[ERROR] Compile CUNQA first or export QUANTUM_MPI_CUNQA_BUILD_DIR."
    exit 1
fi

if [[ ! -f "${CUNQA_INCLUDE_DIR}/comm/client.hpp" ]]; then
    echo "[ERROR] CUNQA headers not found: ${CUNQA_INCLUDE_DIR}/comm/client.hpp"
    echo "[ERROR] Export QUANTUM_MPI_CUNQA_INCLUDE_DIR (usually <cunqa>/src)."
    exit 1
fi

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    -DQUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON \
    -DQUANTUM_MPI_CUNQA_ROOT_DIR="${CUNQA_ROOT_DIR}" \
    -DQUANTUM_MPI_CUNQA_BUILD_DIR="${CUNQA_BUILD_DIR}" \
    -DQUANTUM_MPI_CUNQA_INCLUDE_DIR="${CUNQA_INCLUDE_DIR}" \
    -DBUILD_TESTING=ON \
    "$@"

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

add_ld_path() {
    local path="$1"

    if [[ -d "${path}" ]]; then
        export LD_LIBRARY_PATH="${path}:${LD_LIBRARY_PATH:-}"
    fi
}

add_ld_path "${CUNQA_BUILD_DIR}/lib"
add_ld_path "${CUNQA_BUILD_DIR}/_deps/maestro-build"
add_ld_path "${CUNQA_BUILD_DIR}/_deps/libzmq-build/lib"

echo
echo "[INFO] Build completed."
echo "[INFO] Available executables:"

find "${BUILD_DIR}" \
    -maxdepth 2 \
    -type f \
    -executable \
    ! -name "*.so" \
    ! -name "*.a" \
    ! -name "*.o" \
    ! -path "*/CMakeFiles/*" \
    -print | sort

echo
echo "[INFO] Example:"
echo "  ./run.sh simple_telegate"
echo "  ./run.sh distributed_qft --qpus 2 --qubits 4 --shots 1024 --repetitions 3"
