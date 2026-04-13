#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/quantum_mpi"
BUILD_DIR="${PROJECT_DIR}/build"

# Ensure qraise/qdrop are reachable
export PATH="${HOME}/bin:${PATH}"
export SBATCH_EXPORT=ALL
export SLURM_EXPORT_ENV=ALL

# Load the QMIO runtime stack needed by qraise/setup_qpus.
if command -v module >/dev/null 2>&1; then
    module --force purge >/dev/null 2>&1 || true
    module load qmio/hpc >/dev/null 2>&1 || true
    module load gcc/12.3.0 >/dev/null 2>&1 || true
    module load python/3.11.9 >/dev/null 2>&1 || true
    module load hpcx >/dev/null 2>&1 || module load hpcx-ompi >/dev/null 2>&1 || true
    module load imkl/2024.2 >/dev/null 2>&1 || true
    module load boost/1.85.0 >/dev/null 2>&1 || true
fi

GXX_LIBSTDCPP="$(g++ -print-file-name=libstdc++.so.6)"
export LD_LIBRARY_PATH="$(dirname "${GXX_LIBSTDCPP}"):${LD_LIBRARY_PATH:-}"

if ! command -v qraise >/dev/null 2>&1; then
    echo "[ERROR] qraise not found in PATH."
    exit 1
fi

MISSING_SETUP_QPUS_LIBS="$(ldd "${HOME}/bin/setup_qpus" 2>/dev/null | awk '/not found/{print $1}')"
if [[ -n "${MISSING_SETUP_QPUS_LIBS}" ]]; then
    echo "[ERROR] Missing runtime libraries for ${HOME}/bin/setup_qpus:"
    while IFS= read -r libname; do
        [[ -n "${libname}" ]] && echo "  - ${libname}"
    done <<< "${MISSING_SETUP_QPUS_LIBS}"
    echo "[ERROR] Aborting before launch."
    exit 1
fi

# CUNQA paths (compiled with $STORE/.cunqa)
STORE_DEFAULT="/mnt/netapp1/Store_CESGA/${HOME#/}"
STORE_PATH="${STORE:-${STORE_DEFAULT}}"

if ! mkdir -p "${STORE_PATH}/.cunqa/logs" 2>/dev/null; then
    echo "[WARN] STORE path is not writable: ${STORE_PATH}"
    echo "[WARN] Falling back to HOME for this launch."
    STORE_PATH="${HOME}"
fi

if ! touch "${STORE_PATH}/.cunqa/logs/.write_test_$$" 2>/dev/null; then
    echo "[WARN] STORE logs path is not writable: ${STORE_PATH}/.cunqa/logs"
    echo "[WARN] Falling back to HOME for this launch."
    STORE_PATH="${HOME}"
    mkdir -p "${STORE_PATH}/.cunqa/logs"
else
    rm -f "${STORE_PATH}/.cunqa/logs/.write_test_$$"
fi

export STORE="${STORE_PATH}"
mkdir -p "${STORE}/.cunqa/logs"
if [[ ! -f "${STORE}/.cunqa/qpus.json" ]]; then
    printf "{}\n" > "${STORE}/.cunqa/qpus.json"
fi

echo "[INFO] Project: ${PROJECT_DIR}"
echo "[INFO] Build dir: ${BUILD_DIR}"
echo "[INFO] STORE: ${STORE}"
echo "[INFO] libstdc++: ${GXX_LIBSTDCPP}"
echo "[INFO] qraise: $(command -v qraise || echo not-found)"
echo "[INFO] python3: $(command -v python3 || echo not-found)"
echo "[INFO] python3 version: $(python3 -V 2>/dev/null || echo unknown)"
echo "[INFO] mpirun: $(command -v mpirun || echo not-found)"
if [[ -n "${HPCX_MPI_DIR:-}" ]]; then
    MPI_LIB="$(find "${HPCX_MPI_DIR}" -name libmpi.so.40 2>/dev/null | head -n 1 || true)"
else
    MPI_LIB="$(ldconfig -p 2>/dev/null | awk '/libmpi.so.40/{print $NF; exit}')"
fi
echo "[INFO] libmpi.so.40: ${MPI_LIB:-not-found}"

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" -j

RUN_DIR="${PROJECT_DIR}/run"
mkdir -p "${RUN_DIR}"
echo "[INFO] Run dir: ${RUN_DIR}"
echo "[INFO] Running quantum_mpi_main from run dir (qraise_<jobid> will be created there)"
(
    cd "${RUN_DIR}"
    "${BUILD_DIR}/quantum_mpi_main"
)
